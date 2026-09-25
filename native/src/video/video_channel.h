#pragma once

#include "video/latest_frame_mailbox.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace srd::core {
class Session;
}

namespace srd::video {

class VideoServer {
public:
    VideoServer(
        LatestFrameMailbox& mailbox,
        std::string password,
        std::uint16_t port = 45902);

    ~VideoServer();

    VideoServer(const VideoServer&) = delete;
    VideoServer& operator=(const VideoServer&) = delete;

    void start();
    void stop() noexcept;

private:
    void run();

    LatestFrameMailbox& mailbox_;
    std::string password_;
    std::uint16_t port_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

class VideoClient {
public:
    VideoClient(std::string host, std::string password, std::uint16_t port = 45902);
    ~VideoClient();

    VideoClient(const VideoClient&) = delete;
    VideoClient& operator=(const VideoClient&) = delete;

    void receive_forever(
        const std::function<void(EncodedFrame&&)>& onFrame);

    void stop() noexcept;

private:
    std::string host_;
    std::string password_;
    std::uint16_t port_;
    std::atomic<bool> running_{false};
    std::mutex sessionMutex_;
    std::unique_ptr<core::Session> session_;
};

} // namespace srd::video
