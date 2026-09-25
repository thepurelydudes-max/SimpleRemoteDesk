#pragma once

#include <filesystem>
#include <vector>

namespace srd::transfer {

std::vector<std::filesystem::path> get_clipboard_files();
bool set_clipboard_files(const std::vector<std::filesystem::path>& files);

std::filesystem::path make_transfer_temp_directory();

} // namespace srd::transfer
