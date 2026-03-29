#include "openclaw_client.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "settings.h"

#include <cstring>
#include <cJSON.h>
#include <esp_log.h>
#include <freertos/task.h>
#include <esp_random.h>

#define TAG "OpenClaw"
#define DEFAULT_WS_URL "ws://127.0.0.1:18789/ws"
#define DEFAULT_TOKEN "311ff02922f5ae7b0dd265e000b30c3401c16667b32b6e48"

OpenClawClient::OpenClawClient() : connected_(false) {
    // Use the Linux system IP address instead of localhost
    ws_url_ = "ws://192.168.1.15:18789/ws";
    token_ = "311ff02922f5ae7b0dd265e000b30c3401c16667b32b6e48";
    ESP_LOGI(TAG, "OpenClaw configuration: URL=%s", ws_url_.c_str());
}

OpenClawClient::~OpenClawClient() {
    Stop();
}

bool OpenClawClient::Start() {
    auto network = Board::GetInstance().GetNetwork();
    websocket_ = network->CreateWebSocket(1);
    if (websocket_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create websocket");
        return false;
    }

    websocket_->OnData([this](const char* data, size_t len, bool binary) {
        if (!binary) {
            HandleMessage(data, len, binary);
        }
    });

    websocket_->OnDisconnected([this]() {
        ESP_LOGI(TAG, "OpenClaw websocket disconnected");
        connected_ = false;
    });

    ESP_LOGI(TAG, "Connecting to OpenClaw WebSocket: %s", ws_url_.c_str());
    if (!websocket_->Connect(ws_url_.c_str())) {
        ESP_LOGE(TAG, "Failed to connect to OpenClaw server, code=%d", websocket_->GetLastError());
        if (on_error_) {
            on_error_("Failed to connect to OpenClaw server");
        }
        return false;
    }

    connected_ = true;
    if (on_connected_) {
        on_connected_();
    }

    return true;
}

void OpenClawClient::Stop() {
    if (websocket_) {
        websocket_.reset();
    }
    connected_ = false;
}

bool OpenClawClient::IsConnected() const {
    return connected_ && websocket_ && websocket_->IsConnected();
}

bool OpenClawClient::SendMessage(const std::string& message) {
    if (!IsConnected()) {
        ESP_LOGE(TAG, "Not connected to OpenClaw server");
        return false;
    }

    if (session_key_.empty()) {
        ESP_LOGE(TAG, "No session key available, trying to create a new session...");
        // Try to create a new session
        if (!CreateSession()) {
            ESP_LOGE(TAG, "Failed to create new session");
            return false;
        }
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "req");
    cJSON_AddStringToObject(root, "id", GenerateId().c_str());
    cJSON_AddStringToObject(root, "method", "session.send");

    cJSON* params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "sessionKey", session_key_.c_str());
    cJSON_AddStringToObject(params, "content", message.c_str());
    cJSON_AddItemToObject(root, "params", params);

    auto json_str = cJSON_PrintUnformatted(root);
    std::string json_message(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Sending message to OpenClaw: %s", json_message.c_str());
    if (!websocket_->Send(json_message)) {
        ESP_LOGE(TAG, "Failed to send message to OpenClaw server");
        return false;
    }

    return true;
}

void OpenClawClient::OnConnected(std::function<void()> callback) {
    on_connected_ = callback;
}

void OpenClawClient::OnMessageReceived(std::function<void(const std::string& message)> callback) {
    on_message_received_ = callback;
}

void OpenClawClient::OnError(std::function<void(const std::string& error)> callback) {
    on_error_ = callback;
}

std::string OpenClawClient::GenerateId() {
    // Generate a random ID using ESP32's hardware random number generator
    uint32_t random1 = esp_random();
    uint32_t random2 = esp_random();
    char id_str[37]; // 36 characters + null terminator
    // Format: 8-4-4-4-12 hex characters (total 36 chars)
    snprintf(id_str, sizeof(id_str), "%08lx-%04lx-%04lx-%04lx-%012llx",
             (unsigned long)(random1 & 0xffffff),
             (unsigned long)((random1 >> 24) & 0xffff),
             (unsigned long)((random2 & 0xffff)),
             (unsigned long)((random2 >> 16) & 0xffff),
             (uint64_t)random1 << 32 | random2);
    // Ensure null termination
    id_str[36] = '\0';
    return std::string(id_str);
}

void OpenClawClient::HandleMessage(const char* data, size_t len, bool binary) {
    auto root = cJSON_Parse(data);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON message");
        return;
    }

    // Log the received message for debugging
    ESP_LOGI(TAG, "Received message: %s", cJSON_PrintUnformatted(root));

    auto event = cJSON_GetObjectItem(root, "event");
    if (cJSON_IsString(event) && strcmp(event->valuestring, "connect.challenge") == 0) {
        ESP_LOGI(TAG, "Received connect challenge, sending authentication request...");
        SendConnectRequest();
    } else if (cJSON_IsString(cJSON_GetObjectItem(root, "type")) &&
               strcmp(cJSON_GetObjectItem(root, "type")->valuestring, "res") == 0) {
        auto ok = cJSON_GetObjectItem(root, "ok");
        if (cJSON_IsBool(ok) && ok->valueint) {
                auto payload = cJSON_GetObjectItem(root, "payload");
                if (payload) {
                    auto type = cJSON_GetObjectItem(payload, "type");
                    if (cJSON_IsString(type) && strcmp(type->valuestring, "hello-ok") == 0) {
                        ESP_LOGI(TAG, "Connection authenticated successfully!");

                        auto snapshot = cJSON_GetObjectItem(payload, "snapshot");
                        if (snapshot) {
                            ESP_LOGI(TAG, "Snapshot found: %s", cJSON_PrintUnformatted(snapshot));
                            auto sessions = cJSON_GetObjectItem(snapshot, "sessions");
                            if (sessions) {
                                ESP_LOGI(TAG, "Sessions found: %s", cJSON_PrintUnformatted(sessions));
                                auto recent = cJSON_GetObjectItem(sessions, "recent");
                                if (cJSON_IsArray(recent)) {
                                    ESP_LOGI(TAG, "Recent sessions found, count: %d", cJSON_GetArraySize(recent));
                                    if (cJSON_GetArraySize(recent) > 0) {
                                        auto first_session = cJSON_GetArrayItem(recent, 0);
                                        ESP_LOGI(TAG, "First session: %s", cJSON_PrintUnformatted(first_session));
                                        auto key = cJSON_GetObjectItem(first_session, "key");
                                        if (cJSON_IsString(key)) {
                                            session_key_ = key->valuestring;
                                            ESP_LOGI(TAG, "Bound to existing session: %s", session_key_.c_str());

                                            // Send test messages after 1 second
                                            xTaskCreate([](void* arg) {
                                                OpenClawClient* client = static_cast<OpenClawClient*>(arg);
                                                vTaskDelay(pdMS_TO_TICKS(1000));
                                                client->SendTestMessages();
                                                vTaskDelete(nullptr);
                                            }, "send_test_messages", 4096, this, 5, nullptr);
                                        } else {
                                            ESP_LOGE(TAG, "Session key not found in first session");
                                        }
                                    } else {
                                        ESP_LOGE(TAG, "No recent sessions found");
                                    }
                                } else {
                                    ESP_LOGE(TAG, "Recent sessions not found or not an array");
                                }
                            } else {
                                ESP_LOGE(TAG, "Sessions not found in snapshot");
                            }
                        } else {
                            ESP_LOGE(TAG, "Snapshot not found in payload");
                        }
                    } else if (cJSON_IsString(type) && strcmp(type->valuestring, "session.create-ok") == 0) {
                        // Handle session create response
                        ESP_LOGI(TAG, "Session created successfully!");
                        auto session = cJSON_GetObjectItem(payload, "session");
                        if (session) {
                            auto key = cJSON_GetObjectItem(session, "key");
                            if (cJSON_IsString(key)) {
                                session_key_ = key->valuestring;
                                ESP_LOGI(TAG, "Created new session: %s", session_key_.c_str());
                            } else {
                                ESP_LOGE(TAG, "Session key not found in session.create-ok response");
                            }
                        } else {
                            ESP_LOGE(TAG, "Session not found in session.create-ok response");
                        }
                    }
                }
            }
    } else if (cJSON_IsString(cJSON_GetObjectItem(root, "type")) &&
               strcmp(cJSON_GetObjectItem(root, "type")->valuestring, "event") == 0) {
        auto event_type = cJSON_GetObjectItem(root, "event");
        if (cJSON_IsString(event_type) && strcmp(event_type->valuestring, "agent") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            if (payload) {
                auto stream = cJSON_GetObjectItem(payload, "stream");
                if (cJSON_IsString(stream)) {
                    if (strcmp(stream->valuestring, "assistant") == 0) {
                        auto data_content = cJSON_GetObjectItem(payload, "data");
                        if (data_content) {
                            auto text = cJSON_GetObjectItem(data_content, "text");
                            if (cJSON_IsString(text) && text->valuestring) {
                                ESP_LOGI(TAG, "🤖 AI: %s", text->valuestring);
                                if (on_message_received_) {
                                    on_message_received_(text->valuestring);
                                }
                                // Display the message on the device screen
                                auto& board = Board::GetInstance();
                                auto display = board.GetDisplay();
                                if (display) {
                                    display->SetChatMessage("assistant", text->valuestring);
                                }
                            }
                        }
                    } else if (strcmp(stream->valuestring, "lifecycle") == 0) {
                        auto data_content = cJSON_GetObjectItem(payload, "data");
                        if (data_content) {
                            auto phase = cJSON_GetObjectItem(data_content, "phase");
                            if (cJSON_IsString(phase) && strcmp(phase->valuestring, "end") == 0) {
                                ESP_LOGI(TAG, "   [Reply ended]");
                            }
                        }
                    }
                }
            }
        }
    }

    cJSON_Delete(root);
}

void OpenClawClient::SendConnectRequest() {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "req");
    cJSON_AddStringToObject(root, "id", GenerateId().c_str());
    cJSON_AddStringToObject(root, "method", "connect");

    cJSON* params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "minProtocol", 3);
    cJSON_AddNumberToObject(params, "maxProtocol", 3);

    cJSON* client = cJSON_CreateObject();
    cJSON_AddStringToObject(client, "id", "cli");
    cJSON_AddStringToObject(client, "version", "1.0");
    cJSON_AddStringToObject(client, "platform", "linux");
    cJSON_AddStringToObject(client, "mode", "cli");
    cJSON_AddItemToObject(params, "client", client);

    cJSON_AddStringToObject(params, "role", "operator");

    cJSON* scopes = cJSON_CreateArray();
    cJSON_AddItemToArray(scopes, cJSON_CreateString("operator.read"));
    cJSON_AddItemToArray(scopes, cJSON_CreateString("operator.write"));
    cJSON_AddItemToObject(params, "scopes", scopes);

    cJSON* auth = cJSON_CreateObject();
    cJSON_AddStringToObject(auth, "token", token_.c_str());
    cJSON_AddItemToObject(params, "auth", auth);

    cJSON_AddStringToObject(params, "locale", "en-US");
    cJSON_AddStringToObject(params, "userAgent", "python-openclaw");

    cJSON_AddItemToObject(root, "params", params);

    auto json_str = cJSON_PrintUnformatted(root);
    std::string json_message(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);

    if (!websocket_->Send(json_message)) {
        ESP_LOGE(TAG, "Failed to send connect request");
    }
}

void OpenClawClient::SendTestMessages() {
    if (!IsConnected() || session_key_.empty()) {
        ESP_LOGE(TAG, "Cannot send test messages: not connected or no session key");
        return;
    }

    std::vector<std::string> messages = {
        "你好",
        "你叫啥",
        "你能收到我发的消息吗"
    };

    for (const auto& msg_text : messages) {
        ESP_LOGI(TAG, "➡️  Sending message: %s", msg_text.c_str());
        SendMessage(msg_text);
        vTaskDelay(pdMS_TO_TICKS(500)); // Small delay to prevent message堆积
    }
}

bool OpenClawClient::CreateSession() {
    if (!IsConnected()) {
        ESP_LOGE(TAG, "Not connected to OpenClaw server");
        return false;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "req");
    cJSON_AddStringToObject(root, "id", GenerateId().c_str());
    cJSON_AddStringToObject(root, "method", "session.create");

    cJSON* params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", "ESP32 Session");
    cJSON_AddStringToObject(params, "description", "Session created from ESP32 device");
    cJSON_AddItemToObject(root, "params", params);

    auto json_str = cJSON_PrintUnformatted(root);
    std::string json_message(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Creating new session: %s", json_message.c_str());
    if (!websocket_->Send(json_message)) {
        ESP_LOGE(TAG, "Failed to send session.create request");
        return false;
    }

    // Wait for session creation response
    // Note: This is a simplified implementation, in a real application you would use a callback or event
    vTaskDelay(pdMS_TO_TICKS(1000));

    if (session_key_.empty()) {
        ESP_LOGE(TAG, "Session creation failed: no session key received");
        return false;
    }

    return true;
}
