#pragma once

#include "settings/profiles.h"
#include "network/discovery.h"

#include <windows.h>
#include <optional>

namespace srd::viewer {

bool edit_profile(
    HWND owner,
    settings::ConnectionProfile& profile,
    const std::optional<network::DiscoveredHost>& suggested = std::nullopt);

} // namespace srd::viewer
