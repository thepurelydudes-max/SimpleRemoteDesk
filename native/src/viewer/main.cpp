#include "core/session.h"
#include "core/socket_runtime.h"
#include "core/tcp_socket.h"
#include "input/control_message.h"
#include "security/auth.h"
#include "security/secure_session.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <sstream>
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

void print_help()
{
    std::cout
        << "Commands:\n"
        << "  move X Y\n"
        << "  ldown | lup | rdown | rup | mdown | mup\n"
        << "  wheel DELTA\n"
        << "  keydown VK\n"
        << "  keyup VK\n"
        << "  ping\n"
        << "  quit\n";
}

void send_button(
    srd::security::SecureSession& secure,
    srd::input::MouseButton button,
    bool down)
{
    auto payload = srd::input::encode_mouse_button({button, down});
    secure.send(srd::protocol::MessageType::MouseButton, payload);
}

}

int main(int argc, char** argv)
{
    const std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    const std::string password = argc > 2 ? argv[2] : "change-me";

    try {
        srd::net::SocketRuntime sockets;
        auto socket = srd::net::TcpSocket::connect_to(host, 45900);
        srd::core::Session transport(std::move(socket));

        constexpr std::string_view helloText = "SimpleRemoteViewer/native/M2";
        transport.send(srd::protocol::MessageType::Hello, as_bytes(helloText));

        auto ack = transport.receive();
        if (ack.type != srd::protocol::MessageType::HelloAck) {
            throw std::runtime_error("expected HelloAck");
        }

        std::cout << "Connected to " << host << ":45900\n";
        std::cout << "Host says: " << as_string(ack.payload) << "\n";

        auto keys = srd::security::authenticate_client(transport, password);
        srd::security::SecureSession secure(transport, std::move(keys));

        std::cout << "Authentication OK. Encrypted remote input ready.\n";
        print_help();

        std::string line;
        while (std::cout << "> " && std::getline(std::cin, line)) {
            std::istringstream input(line);
            std::string command;
            input >> command;

            if (command.empty()) continue;

            if (command == "move") {
                std::int32_t x = 0;
                std::int32_t y = 0;
                if (!(input >> x >> y)) {
                    std::cout << "usage: move X Y\n";
                    continue;
                }

                auto payload = srd::input::encode_mouse_move({x, y});
                secure.send(srd::protocol::MessageType::MouseMove, payload);
            }
            else if (command == "ldown") {
                send_button(secure, srd::input::MouseButton::Left, true);
            }
            else if (command == "lup") {
                send_button(secure, srd::input::MouseButton::Left, false);
            }
            else if (command == "rdown") {
                send_button(secure, srd::input::MouseButton::Right, true);
            }
            else if (command == "rup") {
                send_button(secure, srd::input::MouseButton::Right, false);
            }
            else if (command == "mdown") {
                send_button(secure, srd::input::MouseButton::Middle, true);
            }
            else if (command == "mup") {
                send_button(secure, srd::input::MouseButton::Middle, false);
            }
            else if (command == "wheel") {
                std::int32_t delta = 0;
                if (!(input >> delta)) {
                    std::cout << "usage: wheel DELTA\n";
                    continue;
                }

                auto payload = srd::input::encode_mouse_wheel({delta});
                secure.send(srd::protocol::MessageType::MouseWheel, payload);
            }
            else if (command == "keydown" || command == "keyup") {
                unsigned int vk = 0;
                if (!(input >> vk) || vk > 0xffffu) {
                    std::cout << "usage: " << command << " VK\n";
                    continue;
                }

                auto payload = srd::input::encode_key({
                    static_cast<std::uint16_t>(vk),
                    command == "keydown"
                });
                secure.send(srd::protocol::MessageType::Key, payload);
            }
            else if (command == "ping") {
                secure.send(srd::protocol::MessageType::Ping);
                auto pong = secure.receive();
                std::cout << (pong.type == srd::protocol::MessageType::Pong
                    ? "pong\n"
                    : "unexpected reply\n");
            }
            else if (command == "quit") {
                secure.send(srd::protocol::MessageType::Disconnect);
                break;
            }
            else {
                print_help();
            }
        }

        return 0;
    }
    catch (const std::exception& ex) {
        std::cerr << "Viewer error: " << ex.what() << "\n";
        return 1;
    }
}
