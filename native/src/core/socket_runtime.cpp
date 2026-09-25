#include "core/socket_runtime.h"

#include <winsock2.h>
#include <stdexcept>

namespace srd::net {

SocketRuntime::SocketRuntime()
{
    WSADATA data{};
    const int rc = ::WSAStartup(MAKEWORD(2, 2), &data);
    if (rc != 0) {
        throw std::runtime_error("WSAStartup failed");
    }
    initialized_ = true;
}

SocketRuntime::~SocketRuntime() noexcept
{
    if (initialized_) {
        ::WSACleanup();
    }
}

} // namespace srd::net
