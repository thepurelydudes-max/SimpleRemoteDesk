#include "control/control_client.h"

#include "core/socket_runtime.h"
#include "core/tcp_socket.h"
#include "protocol/message.h"
#include "security/auth.h"

#include <cstddef>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace srd::control {

namespace {

std::span<const std::byte> as_bytes(std::string_view text)
{
    return {
        reinterpret_cast<const std::byte*>(text.data()),
        text.size()
    };
}

}

ControlClient::ControlClient(
    std::string host,
    std::string password,
    std::uint16_t port)
    : host_(std::move(host)),
      password_(std::move(password)),
      port_(port)
{
}

ControlClient::~ControlClient()
{
    disconnect();
}

void ControlClient::connect()
{
    std::lock_guard lock(mutex_);

    if (connected_.load(std::memory_order_acquire)) {
        return;
    }

    auto runtime = std::make_unique<net::SocketRuntime>();
    auto socket = net::TcpSocket::connect_to(host_, port_);

    auto transport = std::make_unique<core::Session>(std::move(socket));

    constexpr std::string_view hello = "SimpleRemoteViewer/native/gui";
    transport->send(protocol::MessageType::Hello, as_bytes(hello));

    auto ack = transport->receive();
    if (ack.type != protocol::MessageType::HelloAck) {
        throw std::runtime_error("control channel HelloAck expected");
    }

    auto keys = security::authenticate_client(*transport, password_);
    auto secure = std::make_unique<security::SecureSession>(
        *transport,
        std::move(keys));

    socketRuntime_ = std::move(runtime);
    transport_ = std::move(transport);
    secure_ = std::move(secure);
    connected_.store(true, std::memory_order_release);
}

void ControlClient::disconnect() noexcept
{
    std::lock_guard lock(mutex_);

    if (secure_ && connected_.load(std::memory_order_acquire)) {
        try {
            secure_->send(protocol::MessageType::Disconnect);
        }
        catch (...) {
        }
    }

    connected_.store(false, std::memory_order_release);

    if (transport_) {
        transport_->close();
    }

    secure_.reset();
    transport_.reset();
    socketRuntime_.reset();
}

bool ControlClient::connected() const noexcept
{
    return connected_.load(std::memory_order_acquire);
}

void ControlClient::send(
    protocol::MessageType type,
    std::span<const std::byte> payload)
{
    std::lock_guard lock(mutex_);

    if (!secure_ || !connected_.load(std::memory_order_acquire)) {
        return;
    }

    try {
        secure_->send(type, payload);
    }
    catch (...) {
        connected_.store(false, std::memory_order_release);
        if (transport_) transport_->close();
        secure_.reset();
        transport_.reset();
        socketRuntime_.reset();
        throw;
    }
}

void ControlClient::mouse_move(std::int32_t x, std::int32_t y)
{
    auto payload = input::encode_mouse_move({x, y});
    send(protocol::MessageType::MouseMove, payload);
}

void ControlClient::mouse_button(input::MouseButton button, bool down)
{
    auto payload = input::encode_mouse_button({button, down});
    send(protocol::MessageType::MouseButton, payload);
}

void ControlClient::mouse_wheel(std::int32_t delta)
{
    auto payload = input::encode_mouse_wheel({delta});
    send(protocol::MessageType::MouseWheel, payload);
}

void ControlClient::key(std::uint16_t virtualKey, bool down)
{
    auto payload = input::encode_key({virtualKey, down});
    send(protocol::MessageType::Key, payload);
}

} // namespace srd::control
