#include "video/video_channel.h"

#include "core/session.h"
#include "core/socket_runtime.h"
#include "core/tcp_socket.h"
#include "protocol/message.h"
#include "security/auth.h"
#include "security/secure_session.h"
#include "video/frame_packet.h"

#include <stdexcept>
#include <thread>
#include <utility>

namespace srd::video {

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

    worker_ = std::thread(&VideoServer::run, this);
}

void VideoServer::stop() noexcept
{
    if (!running_.exchange(false)) {
        return;
    }

    try {
        net::SocketRuntime sockets;
        auto wake = net::TcpSocket::connect_to("127.0.0.1", port_);
        wake.close();
    }
    catch (...) {
    }

    mailbox_.stop();

    if (worker_.joinable()) {
        worker_.join();
    }
}

void VideoServer::run()
{
    try {
        net::SocketRuntime sockets;
        auto listener = net::TcpSocket::listen_on(port_);

        while (running_.load(std::memory_order_relaxed)) {
            auto socket = listener.accept_one();

            if (!running_.load(std::memory_order_relaxed)) {
                break;
            }

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
                // A dropped viewer must not stop the listener.
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

VideoClient::~VideoClient()
{
    stop();
}

void VideoClient::receive_forever(
    const std::function<void(EncodedFrame&&)>& onFrame)
{
    running_.store(true, std::memory_order_release);

    net::SocketRuntime sockets;
    auto socket = net::TcpSocket::connect_to(host_, port_);

    {
        std::lock_guard lock(sessionMutex_);
        session_ = std::make_unique<core::Session>(std::move(socket));
    }

    try {
        core::Session* transport = nullptr;
        {
            std::lock_guard lock(sessionMutex_);
            transport = session_.get();
        }

        if (!transport) {
            throw std::runtime_error("video session unavailable");
        }

        auto keys = security::authenticate_client(*transport, password_);
        security::SecureSession secure(*transport, std::move(keys));

        while (running_.load(std::memory_order_acquire)) {
            auto message = secure.receive();

            if (message.type != protocol::MessageType::ScreenFrame) {
                throw std::runtime_error("unexpected video channel message");
            }

            onFrame(deserialize_frame(message.payload));
        }
    }
    catch (...) {
        {
            std::lock_guard lock(sessionMutex_);
            session_.reset();
        }
        running_.store(false, std::memory_order_release);
        throw;
    }

    {
        std::lock_guard lock(sessionMutex_);
        session_.reset();
    }
    running_.store(false, std::memory_order_release);
}

void VideoClient::stop() noexcept
{
    running_.store(false, std::memory_order_release);

    std::lock_guard lock(sessionMutex_);
    if (session_) {
        session_->close();
    }
}

} // namespace srd::video
