#pragma once

#include "video/latest_frame_mailbox.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

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

    void receive_forever(
        const std::function<void(EncodedFrame&&)>& onFrame);

private:
    std::string host_;
    std::string password_;
    std::uint16_t port_;
};

} // namespace srd::video
