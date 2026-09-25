#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace srd::settings {

struct ConnectionProfile {
    std::string id;
    std::string hostId;
    std::wstring name{L"Компьютер"};
    std::string host;
    std::uint16_t port{45900};
    std::string password;
};

std::vector<ConnectionProfile> load_profiles();
void save_profiles(const std::vector<ConnectionProfile>& profiles);
std::string generate_profile_id();

} // namespace srd::settings
