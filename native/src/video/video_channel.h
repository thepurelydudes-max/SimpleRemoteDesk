#pragma once

#include "video/latest_frame_mailbox.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

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
    void* threadHandle_{nullptr};
};

class VideoClient {
public:
    VideoClient(std::string host, std::string password, std::uint16_t port = 45902);

    // Receives frames until the connection is closed or an error occurs.
    // onFrame is invoked on the caller thread for each decoded frame packet.
    template <typename Callback>
    void receive_loop(Callback&& onFrame);

private:
    std::string host_;
    std::string password_;
    std::uint16_t port_;
};

} // namespace srd::video
