#pragma once

#include "core/session.h"
#include "security/crypto.h"

#include <array>
#include <cstddef>
#include <string_view>

namespace srd::security {

struct SessionKeys {
    Bytes32 encryption{};
    Bytes32 authentication{};
};

SessionKeys authenticate_server(core::Session& session, std::string_view password);
SessionKeys authenticate_client(core::Session& session, std::string_view password);

} // namespace srd::security
