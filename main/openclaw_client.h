#ifndef OPENCLAW_CLIENT_H
#define OPENCLAW_CLIENT_H

#include <string>
#include <functional>
#include <memory>

class WebSocket;

class OpenClawClient {
public:
    OpenClawClient();
    ~OpenClawClient();

    bool Start();
    void Stop();
    bool IsConnected() const;
    bool SendMessage(const std::string& message);

    void OnConnected(std::function<void()> callback);
    void OnMessageReceived(std::function<void(const std::string& message)> callback);
    void OnError(std::function<void(const std::string& error)> callback);

private:
    std::unique_ptr<WebSocket> websocket_;
    std::string ws_url_;
    std::string token_;
    std::string session_key_;
    bool connected_;

    std::function<void()> on_connected_;
    std::function<void(const std::string& message)> on_message_received_;
    std::function<void(const std::string& error)> on_error_;

    std::string GenerateId();
    void HandleMessage(const char* data, size_t len, bool binary);
    void SendConnectRequest();
    void SendTestMessages();
};

#endif // OPENCLAW_CLIENT_H
