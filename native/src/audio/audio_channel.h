#pragma once

#include "core/socket_runtime.h"
#include "audio/loopback_capture.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace srd::core { class Session; }

namespace srd::audio {

class AudioServer {
public:
    AudioServer(std::string password, std::uint16_t port);
    ~AudioServer();

    void start();
    void stop() noexcept;

private:
    void run();

    std::string password_;
    std::uint16_t port_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::mutex sessionMutex_;
    core::Session* activeSession_{nullptr};
};

class AudioClient {
public:
    AudioClient(std::string host, std::string password, std::uint16_t port);
    ~AudioClient();

    void play_forever();
    void stop() noexcept;

private:
    std::string host_;
    std::string password_;
    std::uint16_t port_;

    std::atomic<bool> running_{false};
    std::unique_ptr<net::SocketRuntime> runtime_;
    std::mutex sessionMutex_;
    std::unique_ptr<core::Session> session_;
};

} // namespace srd::audio
