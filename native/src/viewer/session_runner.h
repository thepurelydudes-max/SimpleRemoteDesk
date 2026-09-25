#pragma once

#include <windows.h>
#include <string>

namespace srd::viewer {

int run_live_session(
    HINSTANCE instance,
    const std::string& host,
    const std::string& password,
    int showCommand = SW_SHOW);

} // namespace srd::viewer
