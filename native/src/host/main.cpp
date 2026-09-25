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

int main()
{
    try {
        srd::net::SocketRuntime sockets;
        auto listener = srd::net::TcpSocket::listen_on(45900);

        std::cout << "SimpleRemoteHost Native M1\n";
        std::cout << "Listening on TCP 45900...\n";

        while (true) {
            auto client = listener.accept_one();
            std::cout << "Viewer connected\n";

            try {
                srd::core::Session session(std::move(client));
                auto hello = session.receive();

                if (hello.type != srd::protocol::MessageType::Hello) {
                    throw std::runtime_error("expected Hello");
                }

                std::cout << "Viewer says: " << as_string(hello.payload) << "\n";

                constexpr std::string_view reply = "SimpleRemoteHost/native/M1";
                session.send(srd::protocol::MessageType::HelloAck, as_bytes(reply));

                auto ping = session.receive();
                if (ping.type == srd::protocol::MessageType::Ping) {
                    session.send(srd::protocol::MessageType::Pong);
                }

                std::cout << "Session completed cleanly\n";
            }
            catch (const std::exception& ex) {
                std::cerr << "Session error: " << ex.what() << "\n";
            }
        }
    }
    catch (const std::exception& ex) {
        std::cerr << "Fatal host error: " << ex.what() << "\n";
        return 1;
    }
}
