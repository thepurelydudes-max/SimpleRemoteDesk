#include "core/session.h"
#include "core/socket_runtime.h"
#include "core/tcp_socket.h"
#include "security/auth.h"
#include "security/secure_session.h"

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
    const std::string password = argc > 1 ? argv[1] : "change-me";

    try {
        srd::net::SocketRuntime sockets;
        auto listener = srd::net::TcpSocket::listen_on(45900);

        std::cout << "SimpleRemoteHost Native M1\n";
        std::cout << "Listening on TCP 45900...\n";
        std::cout << "Authentication enabled\n";

        while (true) {
            auto client = listener.accept_one();
            std::cout << "Viewer connected\n";

            try {
                srd::core::Session transport(std::move(client));

                auto hello = transport.receive();
                if (hello.type != srd::protocol::MessageType::Hello) {
                    throw std::runtime_error("expected Hello");
                }

                std::cout << "Viewer says: " << as_string(hello.payload) << "\n";

                constexpr std::string_view reply = "SimpleRemoteHost/native/M1";
                transport.send(srd::protocol::MessageType::HelloAck, as_bytes(reply));

                auto keys = srd::security::authenticate_server(transport, password);
                srd::security::SecureSession secure(transport, std::move(keys));

                auto ping = secure.receive();
                if (ping.type != srd::protocol::MessageType::Ping) {
                    throw std::runtime_error("expected encrypted Ping");
                }

                secure.send(srd::protocol::MessageType::Pong);

                std::cout << "Authenticated encrypted session completed cleanly\n";
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
