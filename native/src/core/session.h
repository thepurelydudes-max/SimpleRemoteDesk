#pragma once

#include "core/tcp_socket.h"
#include "protocol/message.h"

#include <span>

namespace srd::core {

class Session {
public:
    explicit Session(net::TcpSocket socket);

    void send(protocol::MessageType type, std::span<const std::byte> payload = {});
    protocol::Message receive();
    void close() noexcept;

private:
    net::TcpSocket socket_;
};

} // namespace srd::core
