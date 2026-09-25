#include "core/session.h"
#include "core/socket_runtime.h"
#include "core/tcp_socket.h"
#include "input/control_message.h"
#include "input/injector.h"
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

void run_control_loop(srd::security::SecureSession& secure)
{
    srd::input::Injector injector;

    for (;;) {
        auto message = secure.receive();

        switch (message.type) {
        case srd::protocol::MessageType::Ping:
            secure.send(srd::protocol::MessageType::Pong);
            break;

        case srd::protocol::MessageType::MouseMove:
            injector.mouse_move(srd::input::decode_mouse_move(message.payload));
            break;

        case srd::protocol::MessageType::MouseButton:
            injector.mouse_button(srd::input::decode_mouse_button(message.payload));
            break;

        case srd::protocol::MessageType::MouseWheel:
            injector.mouse_wheel(srd::input::decode_mouse_wheel(message.payload));
            break;

        case srd::protocol::MessageType::Key:
            injector.key(srd::input::decode_key(message.payload));
            break;

        case srd::protocol::MessageType::Disconnect:
            return;

        default:
            throw std::runtime_error("unsupported secure message");
        }
    }
}

}

int main(int argc, char** argv)
{
    const std::string password = argc > 1 ? argv[1] : "change-me";

    try {
        srd::net::SocketRuntime sockets;
        auto listener = srd::net::TcpSocket::listen_on(45900);

        std::cout << "SimpleRemoteHost Native M2\n";
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

                constexpr std::string_view reply = "SimpleRemoteHost/native/M2";
                transport.send(srd::protocol::MessageType::HelloAck, as_bytes(reply));

                auto keys = srd::security::authenticate_server(transport, password);
                srd::security::SecureSession secure(transport, std::move(keys));

                std::cout << "Authenticated. Remote input active.\n";
                run_control_loop(secure);
                std::cout << "Viewer disconnected cleanly\n";
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
