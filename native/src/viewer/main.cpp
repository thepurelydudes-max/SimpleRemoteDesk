#include "core/session.h"
#include "core/socket_runtime.h"
#include "core/tcp_socket.h"

#include <cstddef>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::span<const std::byte> as_bytes(std::string_view text)
{
    return {
        reinterpret_cast<const std::byte*>(text.data()),
        text.size()
    };
}

std::string as_string(const std::vector<std::byte>& bytes)
{
    return {
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()
    };
}

}

int main(int argc, char** argv)
{
    const std::string host = argc > 1 ? argv[1] : "127.0.0.1";

    try {
        srd::net::SocketRuntime sockets;
        auto socket = srd::net::TcpSocket::connect_to(host, 45900);
        srd::core::Session session(std::move(socket));

        constexpr std::string_view helloText = "SimpleRemoteViewer/native/M1";
        session.send(srd::protocol::MessageType::Hello, as_bytes(helloText));

        auto ack = session.receive();
        if (ack.type != srd::protocol::MessageType::HelloAck) {
            throw std::runtime_error("expected HelloAck");
        }

        std::cout << "Connected to " << host << ":45900\n";
        std::cout << "Host says: " << as_string(ack.payload) << "\n";

        session.send(srd::protocol::MessageType::Ping);
        auto pong = session.receive();

        if (pong.type != srd::protocol::MessageType::Pong) {
            throw std::runtime_error("expected Pong");
        }

        std::cout << "Ping/Pong OK. M1 transport works.\n";
        return 0;
    }
    catch (const std::exception& ex) {
        std::cerr << "Viewer error: " << ex.what() << "\n";
        return 1;
    }
}
