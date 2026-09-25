#pragma once

#include "core/session.h"
#include "core/socket_runtime.h"
#include "security/secure_session.h"
#include "input/control_message.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace srd::control {

class ControlClient {
public:
    ControlClient(std::string host, std::string password, std::uint16_t port = 45900);
    ~ControlClient();

    ControlClient(const ControlClient&) = delete;
    ControlClient& operator=(const ControlClient&) = delete;

    void connect();
    void disconnect() noexcept;

    [[nodiscard]] bool connected() const noexcept;

    void mouse_move(std::int32_t x, std::int32_t y);
    void mouse_button(input::MouseButton button, bool down);
    void mouse_wheel(std::int32_t delta);
    void key(std::uint16_t virtualKey, bool down);

private:
    void send(protocol::MessageType type, std::span<const std::byte> payload = {});

    std::string host_;
    std::string password_;
    std::uint16_t port_;

    mutable std::mutex mutex_;
    std::unique_ptr<net::SocketRuntime> socketRuntime_;
    std::unique_ptr<core::Session> transport_;
    std::unique_ptr<security::SecureSession> secure_;
    std::atomic<bool> connected_{false};
};

} // namespace srd::control
