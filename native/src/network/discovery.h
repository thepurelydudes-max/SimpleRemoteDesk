#pragma once

#include "core/socket_runtime.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace srd::network {

struct DiscoveredHost {
    std::string hostId;
    std::wstring name;
    std::string address;
    std::uint16_t port{45900};
    std::uint64_t lastSeenTick{0};
};

class DiscoveryBeacon {
public:
    DiscoveryBeacon();
    ~DiscoveryBeacon();

    void start(
        std::string hostId,
        std::uint16_t servicePort);

    void stop() noexcept;

private:
    void run();

    std::unique_ptr<net::SocketRuntime> runtime_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::string hostId_;
    std::uint16_t servicePort_{45900};
};

class DiscoveryListener {
public:
    using Callback = std::function<void(DiscoveredHost)>;

    DiscoveryListener();
    ~DiscoveryListener();

    void start(Callback callback);
    void stop() noexcept;

private:
    void run();

    std::unique_ptr<net::SocketRuntime> runtime_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    Callback callback_;
    std::uintptr_t socketValue_{static_cast<std::uintptr_t>(-1)};
};

} // namespace srd::network
