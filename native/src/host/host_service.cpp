#include "host/host_service.h"

#include "capture/screen_capture.h"
#include "core/session.h"
#include "core/tcp_socket.h"
#include "input/control_message.h"
#include "input/injector.h"
#include "protocol/message.h"
#include "security/auth.h"
#include "security/secure_session.h"
#include "video/screen_producer.h"
#include "video/video_channel.h"

#include <cstddef>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace srd::host {

namespace {

std::span<const std::byte> as_bytes(std::string_view text)
{
    return {
        reinterpret_cast<const std::byte*>(text.data()),
        text.size()
    };
}

void run_control_session(
    security::SecureSession& secure,
    std::atomic<bool>& running)
{
    input::Injector injector;

    while (running.load(std::memory_order_acquire)) {
        auto message = secure.receive();

        switch (message.type) {
        case protocol::MessageType::Ping:
            secure.send(protocol::MessageType::Pong);
            break;

        case protocol::MessageType::MouseMove:
            injector.mouse_move(input::decode_mouse_move(message.payload));
            break;

        case protocol::MessageType::MouseButton:
            injector.mouse_button(input::decode_mouse_button(message.payload));
            break;

        case protocol::MessageType::MouseWheel:
            injector.mouse_wheel(input::decode_mouse_wheel(message.payload));
            break;

        case protocol::MessageType::Key:
            injector.key(input::decode_key(message.payload));
            break;

        case protocol::MessageType::Disconnect:
            return;

        default:
            break;
        }
    }
}

}

HostService::HostService() = default;

HostService::~HostService()
{
    stop();
}

void HostService::set_status_callback(StatusCallback callback)
{
    std::lock_guard lock(stateMutex_);
    statusCallback_ = std::move(callback);
}

void HostService::set_status(const std::wstring& text)
{
    StatusCallback callback;
    {
        std::lock_guard lock(stateMutex_);
        callback = statusCallback_;
    }

    if (callback) {
        callback(text);
    }
}

void HostService::start(HostConfig config)
{
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }

    try {
        config_ = std::move(config);

        if (config_.port < 1024 || config_.port > 65531) {
            throw std::invalid_argument("base port must be 1024..65531");
        }

        if (config_.password.size() < 6) {
            throw std::invalid_argument("password must have at least 6 characters");
        }

        socketRuntime_ = std::make_unique<net::SocketRuntime>();
        videoMailbox_ = std::make_unique<video::LatestFrameMailbox>();

        auto capture = capture::create_best_capture();
        if (!capture) {
            throw std::runtime_error("screen capture initialization failed");
        }

        producer_ = std::make_unique<video::ScreenProducer>(
            std::move(capture),
            *videoMailbox_);

        videoServer_ = std::make_unique<video::VideoServer>(
            *videoMailbox_,
            config_.password,
            static_cast<std::uint16_t>(config_.port + 2));

        producer_->start(config_.fps, config_.jpegQuality);
        videoServer_->start();

        controlThread_ = std::thread(
            &HostService::control_server_loop,
            this);

        set_status(L"Host запущен");
    }
    catch (...) {
        running_.store(false, std::memory_order_release);
        stop();
        throw;
    }
}

void HostService::stop() noexcept
{
    const bool wasRunning = running_.exchange(false, std::memory_order_acq_rel);

    clientConnected_.store(false, std::memory_order_release);

    {
        std::lock_guard lock(stateMutex_);
        if (activeControlSession_) {
            activeControlSession_->close();
        }
    }

    if (wasRunning && socketRuntime_) {
        try {
            auto wake = net::TcpSocket::connect_to("127.0.0.1", config_.port);
            wake.close();
        }
        catch (...) {
        }
    }

    if (videoServer_) {
        videoServer_->stop();
    }

    if (producer_) {
        producer_->stop();
    }

    if (videoMailbox_) {
        videoMailbox_->stop();
    }

    if (controlThread_.joinable()) {
        controlThread_.join();
    }

    videoServer_.reset();
    producer_.reset();
    videoMailbox_.reset();
    socketRuntime_.reset();

    set_status(L"Host остановлен");
}

void HostService::disconnect_client() noexcept
{
    std::lock_guard lock(stateMutex_);
    if (activeControlSession_) {
        activeControlSession_->close();
    }
}

bool HostService::running() const noexcept
{
    return running_.load(std::memory_order_acquire);
}

bool HostService::client_connected() const noexcept
{
    return clientConnected_.load(std::memory_order_acquire);
}

void HostService::control_server_loop()
{
    try {
        auto listener = net::TcpSocket::listen_on(config_.port);

        while (running_.load(std::memory_order_acquire)) {
            auto socket = listener.accept_one();

            if (!running_.load(std::memory_order_acquire)) {
                break;
            }

            try {
                core::Session transport(std::move(socket));

                {
                    std::lock_guard lock(stateMutex_);
                    activeControlSession_ = &transport;
                }

                auto clearActiveSession = [this]() {
                    std::lock_guard lock(stateMutex_);
                    activeControlSession_ = nullptr;
                };

                auto hello = transport.receive();
                if (hello.type != protocol::MessageType::Hello) {
                    continue;
                }

                constexpr std::string_view reply = "SimpleRemoteHost/native";
                transport.send(protocol::MessageType::HelloAck, as_bytes(reply));

                auto keys = security::authenticate_server(
                    transport,
                    config_.password);

                security::SecureSession secure(
                    transport,
                    std::move(keys));

                clientConnected_.store(true, std::memory_order_release);
                set_status(L"Клиент подключён");

                run_control_session(secure, running_);

                clearActiveSession();
                clientConnected_.store(false, std::memory_order_release);

                if (running_.load(std::memory_order_acquire)) {
                    set_status(L"Host запущен — ожидание подключения");
                }
            }
            catch (...) {
                {
                    std::lock_guard lock(stateMutex_);
                    activeControlSession_ = nullptr;
                }
                clientConnected_.store(false, std::memory_order_release);

                if (running_.load(std::memory_order_acquire)) {
                    set_status(L"Ошибка клиентской сессии — ожидание нового подключения");
                }
            }
        }
    }
    catch (...) {
        running_.store(false, std::memory_order_release);
        clientConnected_.store(false, std::memory_order_release);
        set_status(L"Ошибка запуска Host");
    }
}

} // namespace srd::host
