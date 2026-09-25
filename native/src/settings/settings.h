#pragma once

#include <cstdint>
#include <string>

namespace srd::settings {

struct HostSettings {
    std::uint16_t port{45900};
    std::string password;
    unsigned int fps{30};
    unsigned int jpegQuality{90};
    bool autostart{false};
};

HostSettings load_host_settings();
void save_host_settings(const HostSettings& settings);

std::wstring app_data_directory();
std::wstring executable_path();

void set_autostart(bool enabled);
bool autostart_enabled();

std::string generate_password(std::size_t length = 16);

} // namespace srd::settings
