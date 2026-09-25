#pragma once

#include "core/socket_runtime.h"
#include "video/latest_frame_mailbox.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace srd::video {
class ScreenProducer;
class VideoServer;
}

namespace srd::host {

struct HostConfig {
    std::uint16_t port{45900};
    std::string password{"change-me"};
    unsigned int fps{30};
    float jpegQuality{0.90f};
};

class HostService {
public:
    using StatusCallback = std::function<void(const std::wstring&)>;

    HostService();
    ~HostService();

    HostService(const HostService&) = delete;
    HostService& operator=(const HostService&) = delete;

    void start(HostConfig config);
    void stop() noexcept;

    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] bool client_connected() const noexcept;

    void set_status_callback(StatusCallback callback);

private:
    void control_server_loop();
    void set_status(const std::wstring& text);

    mutable std::mutex stateMutex_;
    HostConfig config_;
    StatusCallback statusCallback_;

    std::unique_ptr<net::SocketRuntime> socketRuntime_;
    std::unique_ptr<video::LatestFrameMailbox> videoMailbox_;
    std::unique_ptr<video::ScreenProducer> producer_;
    std::unique_ptr<video::VideoServer> videoServer_;

    std::thread controlThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> clientConnected_{false};
};

} // namespace srd::host
