#include "openclaw_client.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "settings.h"

#include <cstring>
#include <cJSON.h>
#include <esp_log.h>

#define TAG "OpenClaw"

OpenClawClient::OpenClawClient() : connected_(true) {
    // Use HTTP POST to xiaozhi-mcp-server
    server_url_ = "http://192.168.1.15:28765/mcp";
    token_ = "80325f7143ffc06d5563641b6b0d608eca4228546727c7db";
    ESP_LOGI(TAG, "OpenClaw configuration: URL=%s", server_url_.c_str());
}

OpenClawClient::~OpenClawClient() {
    Stop();
}

bool OpenClawClient::Start() {
    // No need to start anything for HTTP client
    connected_ = true;
    if (on_connected_) {
        on_connected_();
    }
    return true;
}

void OpenClawClient::Stop() {
    connected_ = false;
}

bool OpenClawClient::IsConnected() const {
    return connected_;
}

bool OpenClawClient::SendMessage(const std::string& message) {
    if (!IsConnected()) {
        ESP_LOGE(TAG, "Not connected to OpenClaw server");
        return false;
    }

    // Create HTTP client
    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp();

    if (!http) {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        return false;
    }

    // Build JSON request
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddStringToObject(root, "id", "1");
    cJSON_AddStringToObject(root, "method", "tools/call");

    cJSON* params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", "run_agent");

    cJSON* arguments = cJSON_CreateObject();
    cJSON_AddStringToObject(arguments, "message", message.c_str());
    cJSON_AddBoolToObject(arguments, "async", true);
    cJSON_AddItemToObject(params, "arguments", arguments);

    cJSON_AddItemToObject(root, "params", params);

    auto json_str = cJSON_PrintUnformatted(root);
    std::string json_message(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);

    // Set request headers
    http->SetHeader("Content-Type", "application/json");
    http->SetHeader("Authorization", ("Bearer " + token_).c_str());

    // Send POST request
    ESP_LOGI(TAG, "Sending message to OpenClaw: %s", message.c_str());
    http->SetContent(std::move(json_message));

    if (!http->Open("POST", server_url_.c_str())) {
        int last_error = http->GetLastError();
        ESP_LOGE(TAG, "Failed to open HTTP connection, code=0x%x", last_error);
        return false;
    }

    auto status_code = http->GetStatusCode();
    if (status_code != 200) {
        ESP_LOGE(TAG, "Failed to send message, status code: %d", status_code);
        std::string response_body = http->ReadAll();
        if (!response_body.empty()) {
            ESP_LOGE(TAG, "Response: %s", response_body.c_str());
        }
        http->Close();
        return false;
    }

    std::string response_body = http->ReadAll();
    http->Close();

    ESP_LOGI(TAG, "Message sent successfully");
    if (!response_body.empty()) {
        ESP_LOGI(TAG, "Response: %s", response_body.c_str());
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

void OpenClawClient::SendTestMessages() {
    if (!IsConnected()) {
        ESP_LOGE(TAG, "Cannot send test messages: not connected");
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
