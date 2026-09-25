#include "audio/audio_channel.h"

#include "audio/wave_player.h"
#include "core/session.h"
#include "core/tcp_socket.h"
#include "protocol/message.h"
#include "security/auth.h"
#include "security/secure_session.h"

#include <objbase.h>

#include <array>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace srd::audio {

namespace {

std::vector<std::byte> encode_format(AudioFormat format)
{
    std::vector<std::byte> out(8);

    out[0] = static_cast<std::byte>(format.sampleRate & 0xff);
    out[1] = static_cast<std::byte>((format.sampleRate >> 8) & 0xff);
    out[2] = static_cast<std::byte>((format.sampleRate >> 16) & 0xff);
    out[3] = static_cast<std::byte>((format.sampleRate >> 24) & 0xff);
    out[4] = static_cast<std::byte>(format.channels & 0xff);
    out[5] = static_cast<std::byte>((format.channels >> 8) & 0xff);
    out[6] = static_cast<std::byte>(format.bitsPerSample & 0xff);
    out[7] = static_cast<std::byte>((format.bitsPerSample >> 8) & 0xff);

    return out;
}

AudioFormat decode_format(std::span<const std::byte> payload)
{
    if (payload.size() != 8) {
        throw std::runtime_error("invalid audio format packet");
    }

    AudioFormat format;

    format.sampleRate =
        static_cast<std::uint32_t>(std::to_integer<unsigned char>(payload[0])) |
        (static_cast<std::uint32_t>(std::to_integer<unsigned char>(payload[1])) << 8) |
        (static_cast<std::uint32_t>(std::to_integer<unsigned char>(payload[2])) << 16) |
        (static_cast<std::uint32_t>(std::to_integer<unsigned char>(payload[3])) << 24);

    format.channels =
        static_cast<std::uint16_t>(
            std::to_integer<unsigned char>(payload[4]) |
            (std::to_integer<unsigned char>(payload[5]) << 8));

    format.bitsPerSample =
        static_cast<std::uint16_t>(
            std::to_integer<unsigned char>(payload[6]) |
            (std::to_integer<unsigned char>(payload[7]) << 8));

    if (format.sampleRate < 8000 ||
        format.sampleRate > 384000 ||
        format.channels == 0 ||
        format.channels > 16 ||
        format.bitsPerSample != 16) {
        throw std::runtime_error("unsupported audio format");
    }

    return format;
}

}

AudioServer::AudioServer(
    std::string password,
    std::uint16_t port)
    : password_(std::move(password)),
      port_(port)
{
}

AudioServer::~AudioServer()
{
    stop();
}

void AudioServer::start()
{
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }

    worker_ = std::thread(&AudioServer::run, this);
}

void AudioServer::stop() noexcept
{
    if (!running_.exchange(false)) {
        return;
    }

    {
        std::lock_guard lock(sessionMutex_);
        if (activeSession_) activeSession_->close();
    }

    try {
        net::SocketRuntime runtime;
        auto wake = net::TcpSocket::connect_to("127.0.0.1", port_);
        wake.close();
    }
    catch (...) {
    }

    if (worker_.joinable()) {
        worker_.join();
    }
}

void AudioServer::run()
{
    const HRESULT com = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comInitialized = SUCCEEDED(com);

    try {
        net::SocketRuntime runtime;
        auto listener = net::TcpSocket::listen_on(port_);

        while (running_.load(std::memory_order_acquire)) {
            auto socket = listener.accept_one();

            if (!running_.load(std::memory_order_acquire)) break;

            try {
                core::Session transport(std::move(socket));

                {
                    std::lock_guard lock(sessionMutex_);
                    activeSession_ = &transport;
                }

                auto keys = security::authenticate_server(
                    transport,
                    password_);

                security::SecureSession secure(
                    transport,
                    std::move(keys));

                LoopbackCapture capture;

                if (!capture.start()) {
                    throw std::runtime_error("WASAPI loopback unavailable");
                }

                const auto formatPayload =
                    encode_format(capture.format());

                secure.send(
                    protocol::MessageType::AudioFormat,
                    formatPayload);

                std::vector<std::byte> chunk;

                while (running_.load(std::memory_order_acquire)) {
                    if (!capture.read_chunk(chunk)) {
                        ::Sleep(3);
                        continue;
                    }

                    secure.send(
                        protocol::MessageType::AudioData,
                        chunk);
                }
            }
            catch (...) {
            }

            {
                std::lock_guard lock(sessionMutex_);
                activeSession_ = nullptr;
            }
        }
    }
    catch (...) {
        running_.store(false, std::memory_order_release);
    }

    if (comInitialized) {
        ::CoUninitialize();
    }
}

AudioClient::AudioClient(
    std::string host,
    std::string password,
    std::uint16_t port)
    : host_(std::move(host)),
      password_(std::move(password)),
      port_(port)
{
}

AudioClient::~AudioClient()
{
    stop();
}

void AudioClient::play_forever()
{
    running_.store(true, std::memory_order_release);
    runtime_ = std::make_unique<net::SocketRuntime>();

    auto socket = net::TcpSocket::connect_to(host_, port_);

    {
        std::lock_guard lock(sessionMutex_);
        session_ = std::make_unique<core::Session>(std::move(socket));
    }

    core::Session* transport = nullptr;

    {
        std::lock_guard lock(sessionMutex_);
        transport = session_.get();
    }

    if (!transport) {
        throw std::runtime_error("audio session unavailable");
    }

    auto keys = security::authenticate_client(
        *transport,
        password_);

    security::SecureSession secure(
        *transport,
        std::move(keys));

    auto formatMessage = secure.receive();

    if (formatMessage.type != protocol::MessageType::AudioFormat) {
        throw std::runtime_error("AudioFormat expected");
    }

    const AudioFormat format =
        decode_format(formatMessage.payload);

    WavePlayer player;

    if (!player.open(format)) {
        throw std::runtime_error("unable to open audio output");
    }

    try {
        while (running_.load(std::memory_order_acquire)) {
            auto message = secure.receive();

            if (message.type != protocol::MessageType::AudioData) {
                continue;
            }

            if (!player.write(message.payload)) {
                break;
            }
        }
    }
    catch (...) {
        player.close();

        {
            std::lock_guard lock(sessionMutex_);
            session_.reset();
        }

        runtime_.reset();
        running_.store(false, std::memory_order_release);
        throw;
    }

    player.close();

    {
        std::lock_guard lock(sessionMutex_);
        session_.reset();
    }

    runtime_.reset();
    running_.store(false, std::memory_order_release);
}

void AudioClient::stop() noexcept
{
    running_.store(false, std::memory_order_release);

    std::lock_guard lock(sessionMutex_);

    if (session_) {
        session_->close();
    }
}

} // namespace srd::audio
