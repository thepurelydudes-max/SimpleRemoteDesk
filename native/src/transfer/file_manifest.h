#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace srd::transfer {

struct FileEntry {
    std::string relativePath;
    bool directory{false};
    std::uint64_t size{0};
};

struct FileManifest {
    std::vector<std::string> roots;
    std::vector<FileEntry> entries;
};

FileManifest build_manifest(
    const std::vector<std::filesystem::path>& roots,
    std::vector<std::filesystem::path>& sourceFiles);

std::vector<std::byte> serialize_manifest(const FileManifest& manifest);
FileManifest deserialize_manifest(std::span<const std::byte> payload);

bool safe_relative_path(const std::filesystem::path& path);

} // namespace srd::transfer
