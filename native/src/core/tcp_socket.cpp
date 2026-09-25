#include "core/tcp_socket.h"

#include <ws2tcpip.h>
#include <stdexcept>
#include <utility>

namespace srd::net {

namespace {

[[noreturn]] void throw_socket_error(const char* message)
{
    throw std::runtime_error(std::string(message) + " (WSA=" + std::to_string(::WSAGetLastError()) + ")");
}

}

TcpSocket::TcpSocket(SOCKET socket) noexcept : socket_(socket) {}

TcpSocket::~TcpSocket() noexcept
{
    close();
}

TcpSocket::TcpSocket(TcpSocket&& other) noexcept
    : socket_(std::exchange(other.socket_, INVALID_SOCKET))
{
}

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept
{
    if (this != &other) {
        close();
        socket_ = std::exchange(other.socket_, INVALID_SOCKET);
    }
    return *this;
}

TcpSocket TcpSocket::listen_on(std::uint16_t port, int backlog)
{
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        throw_socket_error("socket failed");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (::bind(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        ::closesocket(s);
        throw_socket_error("bind failed");
    }

    if (::listen(s, backlog) == SOCKET_ERROR) {
        ::closesocket(s);
        throw_socket_error("listen failed");
    }

    return TcpSocket{s};
}

TcpSocket TcpSocket::connect_to(const std::string& host, std::uint16_t port)
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    const std::string portText = std::to_string(port);
    const int rc = ::getaddrinfo(host.c_str(), portText.c_str(), &hints, &result);
    if (rc != 0 || result == nullptr) {
        throw std::runtime_error("getaddrinfo failed");
    }

    SOCKET connected = INVALID_SOCKET;
    for (addrinfo* p = result; p != nullptr; p = p->ai_next) {
        SOCKET s = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s == INVALID_SOCKET) {
            continue;
        }

        if (::connect(s, p->ai_addr, static_cast<int>(p->ai_addrlen)) == 0) {
            connected = s;
            break;
        }

        ::closesocket(s);
    }

    ::freeaddrinfo(result);

    if (connected == INVALID_SOCKET) {
        throw_socket_error("connect failed");
    }

    return TcpSocket{connected};
}

TcpSocket TcpSocket::accept_one() const
{
    SOCKET s = ::accept(socket_, nullptr, nullptr);
    if (s == INVALID_SOCKET) {
        throw_socket_error("accept failed");
    }
    return TcpSocket{s};
}

void TcpSocket::send_all(std::span<const std::byte> data) const
{
    std::size_t sent = 0;
    while (sent < data.size()) {
        const int chunk = static_cast<int>(data.size() - sent);
        const int rc = ::send(
            socket_,
            reinterpret_cast<const char*>(data.data() + sent),
            chunk,
            0);

        if (rc == SOCKET_ERROR || rc == 0) {
            throw_socket_error("send failed");
        }

        sent += static_cast<std::size_t>(rc);
    }
}

void TcpSocket::recv_all(std::span<std::byte> data) const
{
    std::size_t received = 0;
    while (received < data.size()) {
        const int chunk = static_cast<int>(data.size() - received);
        const int rc = ::recv(
            socket_,
            reinterpret_cast<char*>(data.data() + received),
            chunk,
            0);

        if (rc == 0) {
            throw std::runtime_error("peer disconnected");
        }

        if (rc == SOCKET_ERROR) {
            throw_socket_error("recv failed");
        }

        received += static_cast<std::size_t>(rc);
    }
}

void TcpSocket::set_no_delay(bool enabled) const
{
    const BOOL value = enabled ? TRUE : FALSE;
    if (::setsockopt(
            socket_,
            IPPROTO_TCP,
            TCP_NODELAY,
            reinterpret_cast<const char*>(&value),
            sizeof(value)) == SOCKET_ERROR) {
        throw_socket_error("setsockopt(TCP_NODELAY) failed");
    }
}

void TcpSocket::close() noexcept
{
    if (socket_ != INVALID_SOCKET) {
        ::shutdown(socket_, SD_BOTH);
        ::closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
}

bool TcpSocket::valid() const noexcept
{
    return socket_ != INVALID_SOCKET;
}

SOCKET TcpSocket::native_handle() const noexcept
{
    return socket_;
}

} // namespace srd::net
