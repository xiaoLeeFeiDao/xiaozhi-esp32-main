#ifndef _PRIZE_WEBSOCKET_H_
#define _PRIZE_WEBSOCKET_H_

#include "protocol.h"
#include <web_socket.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <string>
#include <memory>

#define PRIZE_WEBSOCKET_SERVER_HELLO_EVENT (1 << 0)

/**
 * @brief Prize WebSocket协议类
 * 实现与Prize服务器(ASR/TTS/STT)的WebSocket连接
 */
class PrizeWebsocket : public Protocol {
public:
    PrizeWebsocket();
    ~PrizeWebsocket();

    bool Start() override;
    bool OpenAudioChannel() override;
    void CloseAudioChannel(bool send_goodbye = true) override;
    bool IsAudioChannelOpened() const override;
    bool SendAudio(std::unique_ptr<AudioStreamPacket> packet) override;

    void SendWakeWordDetected(const std::string& wake_word) override;
    void SendStartListening(ListeningMode mode) override;
    void SendStopListening() override;
    void SendAbortSpeaking(AbortReason reason) override;

private:
    std::unique_ptr<WebSocket> websocket_;
    EventGroupHandle_t event_group_handle_;
    bool audio_channel_opened_ = false;
    int version_ = 6;
    std::string session_id_;
    int server_sample_rate_ = 24000;
    int server_frame_duration_ = 60;

    bool Connect();
    std::string GetInitMessage();
    void ParseServerHello(const cJSON* root);
    void HandleTextMessage(const char* data, size_t len);
    void HandleBinaryMessage(const char* data, size_t len);

    std::string GetServerAddress();
    std::string GetServerPort();
    std::string GetServerPath();

protected:
    bool SendText(const std::string& text) override;
    void SetError(const std::string& message) override;
};

#endif // _PRIZE_WEBSOCKET_H_