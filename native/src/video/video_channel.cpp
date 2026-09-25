#include "video/video_channel.h"

#include "core/session.h"
#include "core/socket_runtime.h"
#include "core/tcp_socket.h"
#include "protocol/message.h"
#include "security/auth.h"
#include "security/secure_session.h"
#include "video/frame_packet.h"

#include <windows.h>

#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>

namespace srd::video {

namespace {

DWORD WINAPI video_server_thread_proc(void* context)
{
    static_cast<VideoServer*>(context)->start();
    return 0;
}

}

VideoServer::VideoServer(
    LatestFrameMailbox& mailbox,
    std::string password,
    std::uint16_t port)
    : mailbox_(mailbox),
      password_(std::move(password)),
      port_(port)
{
}

VideoServer::~VideoServer()
{
    stop();
}

void VideoServer::start()
{
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }

    std::thread([this] { run(); }).detach();
}

void VideoServer::stop() noexcept
{
    running_.store(false, std::memory_order_relaxed);
}

void VideoServer::run()
{
    try {
        net::SocketRuntime sockets;
        auto listener = net::TcpSocket::listen_on(port_);

        while (running_.load(std::memory_order_relaxed)) {
            auto socket = listener.accept_one();

            try {
                core::Session transport(std::move(socket));
                auto keys = security::authenticate_server(transport, password_);
                security::SecureSession secure(transport, std::move(keys));

                std::uint64_t lastSequence = 0;

                while (running_.load(std::memory_order_relaxed)) {
                    bool stopping = false;
                    auto frame = mailbox_.wait_for_newer(lastSequence, stopping);

                    if (stopping || !frame) {
                        break;
                    }

                    auto payload = serialize_frame(*frame);
                    secure.send(protocol::MessageType::ScreenFrame, payload);
                    lastSequence = frame->sequence;
                }
            }
            catch (...) {
                // A dropped viewer must not stop the video listener.
            }
        }
    }
    catch (...) {
        running_.store(false, std::memory_order_relaxed);
    }
}

VideoClient::VideoClient(
    std::string host,
    std::string password,
    std::uint16_t port)
    : host_(std::move(host)),
      password_(std::move(password)),
      port_(port)
{
}

} // namespace srd::video
