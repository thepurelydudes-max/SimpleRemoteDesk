#include "core/session.h"

#include <array>
#include <utility>
#include <vector>

namespace srd::core {

Session::Session(net::TcpSocket socket)
    : socket_(std::move(socket))
{
    socket_.set_no_delay(true);
}

void Session::send(protocol::MessageType type, std::span<const std::byte> payload)
{
    auto bytes = protocol::encode(type, payload);
    socket_.send_all(bytes);
}

protocol::Message Session::receive()
{
    std::array<std::byte, protocol::kHeaderSize> header{};
    socket_.recv_all(header);

    const std::uint32_t payloadSize = protocol::payload_size_from_header(header);
    std::vector<std::byte> payload(payloadSize);

    if (!payload.empty()) {
        socket_.recv_all(payload);
    }

    return protocol::decode(header, payload);
}

} // namespace srd::core
