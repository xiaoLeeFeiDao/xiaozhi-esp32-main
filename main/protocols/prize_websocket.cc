#include "prize_websocket.h"
#include "board.h"
#include "system_info.h"
#include "settings.h"
#include "assets/lang_config.h"

#include <cstring>
#include <cJSON.h>
#include <esp_log.h>

#define TAG "PrizeWS"

PrizeWebsocket::PrizeWebsocket() {
    event_group_handle_ = xEventGroupCreate();
}

PrizeWebsocket::~PrizeWebsocket() {
    if (event_group_handle_ != nullptr) {
        vEventGroupDelete(event_group_handle_);
    }
}

std::string PrizeWebsocket::GetServerAddress() {
#ifdef CONFIG_PRIZE_WEBSOCKET_ADDR
    return CONFIG_PRIZE_WEBSOCKET_ADDR;
#else
    return "rbt-tnet.coosealab.com";
#endif
}

std::string PrizeWebsocket::GetServerPort() {
#ifdef CONFIG_PRIZE_WEBSOCKET_PORT
    return CONFIG_PRIZE_WEBSOCKET_PORT;
#else
    return "443";
#endif
}

std::string PrizeWebsocket::GetServerPath() {
#ifdef CONFIG_PRIZE_WEBSOCKET_PATH
    return CONFIG_PRIZE_WEBSOCKET_PATH;
#else
    return "/talknet/ws/v1";
#endif
}

std::string PrizeWebsocket::GetInitMessage() {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "init");
    cJSON_AddNumberToObject(root, "version", version_);
    cJSON_AddStringToObject(root, "transport", "websocket");

    cJSON* audio_params = cJSON_CreateObject();
    cJSON_AddStringToObject(audio_params, "format", "opus");
    cJSON_AddNumberToObject(audio_params, "sample_rate", 16000);
    cJSON_AddNumberToObject(audio_params, "channels", 1);
    cJSON_AddNumberToObject(audio_params, "frame_duration", 60);
    cJSON_AddItemToObject(root, "audio_params", audio_params);

    cJSON_AddStringToObject(root, "wakeup_voice", "");
    cJSON_AddNumberToObject(root, "wakeup_doa", 0);

    auto json_str = cJSON_PrintUnformatted(root);
    std::string message(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);
    return message;
}

void PrizeWebsocket::ParseServerHello(const cJSON* root) {
    auto transport = cJSON_GetObjectItem(root, "transport");
    if (transport == nullptr || strcmp(transport->valuestring, "websocket") != 0) {
        ESP_LOGE(TAG, "Unsupported transport");
        return;
    }

    auto session_id = cJSON_GetObjectItem(root, "session_id");
    if (cJSON_IsString(session_id)) {
        session_id_ = session_id->valuestring;
        ESP_LOGI(TAG, "Session ID: %s", session_id_.c_str());
    }

    auto audio_params = cJSON_GetObjectItem(root, "audio_params");
    if (cJSON_IsObject(audio_params)) {
        auto sample_rate = cJSON_GetObjectItem(audio_params, "sample_rate");
        if (cJSON_IsNumber(sample_rate)) {
            server_sample_rate_ = sample_rate->valueint;
        }
        auto frame_duration = cJSON_GetObjectItem(audio_params, "frame_duration");
        if (cJSON_IsNumber(frame_duration)) {
            server_frame_duration_ = frame_duration->valueint;
        }
    }

    xEventGroupSetBits(event_group_handle_, PRIZE_WEBSOCKET_SERVER_HELLO_EVENT);
}

void PrizeWebsocket::HandleTextMessage(const char* data, size_t len) {
    auto root = cJSON_Parse(data);
    if (root == nullptr) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return;
    }

    auto type = cJSON_GetObjectItem(root, "type");
    if (cJSON_IsString(type)) {
        if (strcmp(type->valuestring, "hello") == 0) {
            ParseServerHello(root);
        } else if (on_incoming_json_ != nullptr) {
            on_incoming_json_(root);
        }
    }

    cJSON_Delete(root);
}

void PrizeWebsocket::HandleBinaryMessage(const char* data, size_t len) {
    if (on_incoming_audio_ != nullptr) {
        on_incoming_audio_(std::make_unique<AudioStreamPacket>(AudioStreamPacket{
            .sample_rate = server_sample_rate_,
            .frame_duration = server_frame_duration_,
            .timestamp = 0,
            .payload = std::vector<uint8_t>((uint8_t*)data, (uint8_t*)data + len)
        }));
    }
}

bool PrizeWebsocket::Start() {
    ESP_LOGI(TAG, "Starting Prize WebSocket protocol");
    return true;
}

bool PrizeWebsocket::Connect() {
    auto network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        ESP_LOGE(TAG, "Network interface not available");
        SetError("Network interface not available");
        return false;
    }

    websocket_ = network->CreateWebSocket(1);
    if (websocket_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create websocket");
        SetError("Failed to create websocket");
        return false;
    }

    std::string url = "wss://" + GetServerAddress() + ":" + GetServerPort() + GetServerPath();
    ESP_LOGI(TAG, "Connecting to %s", url.c_str());

    std::string device_id = SystemInfo::GetMacAddress();
    websocket_->SetHeader("Authorization", "Bearer ");
    websocket_->SetHeader("Protocol-Version", std::to_string(version_).c_str());
    websocket_->SetHeader("Device-Id", device_id.c_str());
    websocket_->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());

    websocket_->OnData([this](const char* data, size_t len, bool binary) {
        if (binary) {
            HandleBinaryMessage(data, len);
        } else {
            HandleTextMessage(data, len);
        }
    });

    websocket_->OnDisconnected([this]() {
        ESP_LOGI(TAG, "WebSocket disconnected");
        audio_channel_opened_ = false;
        if (on_audio_channel_closed_ != nullptr) {
            on_audio_channel_closed_();
        }
    });

    if (!websocket_->Connect(url.c_str())) {
        ESP_LOGE(TAG, "Failed to connect, code=%d", websocket_->GetLastError());
        SetError(Lang::Strings::SERVER_NOT_CONNECTED);
        return false;
    }

    return true;
}

bool PrizeWebsocket::OpenAudioChannel() {
    if (!Connect()) {
        return false;
    }

    auto init_msg = GetInitMessage();
    ESP_LOGI(TAG, "Sending init: %s", init_msg.c_str());
    if (!SendText(init_msg)) {
        SetError("Failed to send init message");
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(event_group_handle_, PRIZE_WEBSOCKET_SERVER_HELLO_EVENT,
                                            pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
    if (!(bits & PRIZE_WEBSOCKET_SERVER_HELLO_EVENT)) {
        ESP_LOGE(TAG, "Failed to receive server hello");
        SetError(Lang::Strings::SERVER_TIMEOUT);
        return false;
    }

    audio_channel_opened_ = true;

    if (on_audio_channel_opened_ != nullptr) {
        on_audio_channel_opened_();
    }

    return true;
}

void PrizeWebsocket::CloseAudioChannel(bool send_goodbye) {
    if (websocket_ != nullptr && audio_channel_opened_) {
        websocket_.reset();
        audio_channel_opened_ = false;
    }
}

bool PrizeWebsocket::IsAudioChannelOpened() const {
    return audio_channel_opened_;
}

bool PrizeWebsocket::SendAudio(std::unique_ptr<AudioStreamPacket> packet) {
    if (websocket_ == nullptr || !audio_channel_opened_) {
        return false;
    }
    return websocket_->Send(packet->payload.data(), packet->payload.size(), true);
}

void PrizeWebsocket::SendWakeWordDetected(const std::string& wake_word) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "listen");
    cJSON_AddStringToObject(root, "state", "detect");
    cJSON_AddStringToObject(root, "wake_word", wake_word.c_str());

    auto json_str = cJSON_PrintUnformatted(root);
    SendText(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);
}

void PrizeWebsocket::SendStartListening(ListeningMode mode) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "dev_listen");
    cJSON_AddStringToObject(root, "state", "start");

    const char* mode_str = "auto";
    switch (mode) {
        case kListeningModeAutoStop: mode_str = "auto"; break;
        case kListeningModeManualStop: mode_str = "manual"; break;
        case kListeningModeRealtime: mode_str = "realtime"; break;
    }
    cJSON_AddStringToObject(root, "mode", mode_str);

    auto json_str = cJSON_PrintUnformatted(root);
    SendText(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);
}

void PrizeWebsocket::SendStopListening() {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "dev_listen");
    cJSON_AddStringToObject(root, "state", "stop");

    auto json_str = cJSON_PrintUnformatted(root);
    SendText(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);
}

void PrizeWebsocket::SendAbortSpeaking(AbortReason reason) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "abort");
    cJSON_AddNumberToObject(root, "reason", reason);

    auto json_str = cJSON_PrintUnformatted(root);
    SendText(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);
}

bool PrizeWebsocket::SendText(const std::string& text) {
    if (websocket_ == nullptr) {
        return false;
    }
    return websocket_->Send(text.c_str(), text.size(), false);
}

void PrizeWebsocket::SetError(const std::string& message) {
    error_occurred_ = true;
    if (on_network_error_ != nullptr) {
        on_network_error_(message);
    }
}