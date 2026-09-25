#include "network/discovery.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <array>
#include <chrono>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace srd::network {

namespace {

constexpr std::uint16_t kDiscoveryPort = 45901;

std::string machine_name_utf8()
{
    wchar_t buffer[256]{};
    DWORD size = static_cast<DWORD>(std::size(buffer));

    if (!::GetComputerNameW(buffer, &size)) {
        return "Computer";
    }

    const int bytes = ::WideCharToMultiByte(
        CP_UTF8,
        0,
        buffer,
        static_cast<int>(size),
        nullptr,
        0,
        nullptr,
        nullptr);

    std::string result(static_cast<std::size_t>(bytes), '\0');

    ::WideCharToMultiByte(
        CP_UTF8,
        0,
        buffer,
        static_cast<int>(size),
        result.data(),
        bytes,
        nullptr,
        nullptr);

    return result;
}

std::wstring utf8_to_wide(const std::string& value)
{
    if (value.empty()) return {};

    const int chars = ::MultiByteToWideChar(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);

    std::wstring result(static_cast<std::size_t>(chars), L'\0');

    ::MultiByteToWideChar(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        chars);

    return result;
}

std::vector<std::string> split(const std::string& value, char delimiter)
{
    std::vector<std::string> parts;
    std::string current;

    for (char ch : value) {
        if (ch == delimiter) {
            parts.push_back(std::move(current));
            current.clear();
        } else {
            current.push_back(ch);
        }
    }

    parts.push_back(std::move(current));
    return parts;
}

}

DiscoveryBeacon::DiscoveryBeacon() = default;

DiscoveryBeacon::~DiscoveryBeacon()
{
    stop();
}

void DiscoveryBeacon::start(
    std::string hostId,
    std::uint16_t servicePort)
{
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }

    hostId_ = std::move(hostId);
    servicePort_ = servicePort;
    runtime_ = std::make_unique<net::SocketRuntime>();
    worker_ = std::thread(&DiscoveryBeacon::run, this);
}

void DiscoveryBeacon::stop() noexcept
{
    if (!running_.exchange(false)) {
        return;
    }

    if (worker_.joinable()) {
        worker_.join();
    }

    runtime_.reset();
}

void DiscoveryBeacon::run()
{
    SOCKET sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        running_.store(false);
        return;
    }

    BOOL broadcast = TRUE;
    ::setsockopt(
        sock,
        SOL_SOCKET,
        SO_BROADCAST,
        reinterpret_cast<const char*>(&broadcast),
        sizeof(broadcast));

    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(kDiscoveryPort);
    target.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    const std::string payload =
        "SRD3|" + hostId_ + "|" +
        machine_name_utf8() + "|" +
        std::to_string(servicePort_);

    while (running_.load(std::memory_order_acquire)) {
        ::sendto(
            sock,
            payload.data(),
            static_cast<int>(payload.size()),
            0,
            reinterpret_cast<const sockaddr*>(&target),
            sizeof(target));

        for (int i = 0; i < 20 &&
             running_.load(std::memory_order_acquire); ++i) {
            ::Sleep(100);
        }
    }

    ::closesocket(sock);
}

DiscoveryListener::DiscoveryListener() = default;

DiscoveryListener::~DiscoveryListener()
{
    stop();
}

void DiscoveryListener::start(Callback callback)
{
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }

    callback_ = std::move(callback);
    runtime_ = std::make_unique<net::SocketRuntime>();
    worker_ = std::thread(&DiscoveryListener::run, this);
}

void DiscoveryListener::stop() noexcept
{
    if (!running_.exchange(false)) {
        return;
    }

    const SOCKET sock = static_cast<SOCKET>(socketValue_);
    if (sock != INVALID_SOCKET) {
        ::closesocket(sock);
    }

    if (worker_.joinable()) {
        worker_.join();
    }

    socketValue_ = static_cast<std::uintptr_t>(INVALID_SOCKET);
    callback_ = {};
    runtime_.reset();
}

void DiscoveryListener::run()
{
    SOCKET sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        running_.store(false);
        return;
    }

    socketValue_ = static_cast<std::uintptr_t>(sock);

    BOOL reuse = TRUE;
    ::setsockopt(
        sock,
        SOL_SOCKET,
        SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuse),
        sizeof(reuse));

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(kDiscoveryPort);
    local.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(
            sock,
            reinterpret_cast<const sockaddr*>(&local),
            sizeof(local)) == SOCKET_ERROR) {
        ::closesocket(sock);
        socketValue_ = static_cast<std::uintptr_t>(INVALID_SOCKET);
        running_.store(false);
        return;
    }

    std::array<char, 2048> buffer{};

    while (running_.load(std::memory_order_acquire)) {
        sockaddr_in remote{};
        int remoteSize = sizeof(remote);

        const int read = ::recvfrom(
            sock,
            buffer.data(),
            static_cast<int>(buffer.size() - 1),
            0,
            reinterpret_cast<sockaddr*>(&remote),
            &remoteSize);

        if (read <= 0) {
            if (!running_.load(std::memory_order_acquire)) break;
            continue;
        }

        std::string text(buffer.data(), buffer.data() + read);
        auto parts = split(text, '|');

        if (parts.size() != 4 || parts[0] != "SRD3") {
            continue;
        }

        int port = 0;
        try {
            port = std::stoi(parts[3]);
        }
        catch (...) {
            continue;
        }

        if (port < 1024 || port > 65531) {
            continue;
        }

        char addressBuffer[INET_ADDRSTRLEN]{};
        if (!::inet_ntop(
                AF_INET,
                &remote.sin_addr,
                addressBuffer,
                sizeof(addressBuffer))) {
            continue;
        }

        DiscoveredHost host;
        host.hostId = parts[1];
        host.name = utf8_to_wide(parts[2]);
        host.address = addressBuffer;
        host.port = static_cast<std::uint16_t>(port);
        host.lastSeenTick = ::GetTickCount64();

        if (callback_) {
            callback_(std::move(host));
        }
    }

    ::closesocket(sock);
    socketValue_ = static_cast<std::uintptr_t>(INVALID_SOCKET);
}

} // namespace srd::network
