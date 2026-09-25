#include "transfer/file_manifest.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

namespace srd::transfer {

namespace {

void write_u32(std::vector<std::byte>& out, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<std::byte>((value >> (i * 8)) & 0xff));
}

void write_u64(std::vector<std::byte>& out, std::uint64_t value)
{
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<std::byte>((value >> (i * 8)) & 0xff));
}

std::uint32_t read_u32(std::span<const std::byte> data, std::size_t& offset)
{
    if (offset + 4 > data.size()) throw std::runtime_error("manifest truncated");
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i)
        value |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(data[offset++])) << (i * 8);
    return value;
}

std::uint64_t read_u64(std::span<const std::byte> data, std::size_t& offset)
{
    if (offset + 8 > data.size()) throw std::runtime_error("manifest truncated");
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i)
        value |= static_cast<std::uint64_t>(std::to_integer<unsigned char>(data[offset++])) << (i * 8);
    return value;
}

void write_string(std::vector<std::byte>& out, const std::string& value)
{
    if (value.size() > 32768) throw std::runtime_error("manifest path too long");
    write_u32(out, static_cast<std::uint32_t>(value.size()));
    out.insert(
        out.end(),
        reinterpret_cast<const std::byte*>(value.data()),
        reinterpret_cast<const std::byte*>(value.data() + value.size()));
}

std::string read_string(std::span<const std::byte> data, std::size_t& offset)
{
    const std::uint32_t size = read_u32(data, offset);
    if (size > 32768 || offset + size > data.size())
        throw std::runtime_error("invalid manifest string");
    std::string value(
        reinterpret_cast<const char*>(data.data() + offset),
        size);
    offset += size;
    return value;
}

std::string path_utf8(const std::filesystem::path& path)
{
    const auto u8 = path.generic_u8string();
    return std::string(
        reinterpret_cast<const char*>(u8.data()),
        u8.size());
}

std::string unique_root_name(
    const std::filesystem::path& path,
    std::unordered_map<std::string, int>& used)
{
    std::string name = path_utf8(path.filename());
    if (name.empty()) name = "item";

    int& count = used[name];
    if (count++ == 0) return name;

    const auto stem = path_utf8(path.stem());
    const auto ext = path_utf8(path.extension());
    return stem + "_" + std::to_string(count) + ext;
}

}

bool safe_relative_path(const std::filesystem::path& path)
{
    if (path.empty() || path.is_absolute() || path.has_root_path())
        return false;

    for (const auto& part : path) {
        if (part == L".." || part == "..") return false;
    }

    return true;
}

FileManifest build_manifest(
    const std::vector<std::filesystem::path>& roots,
    std::vector<std::filesystem::path>& sourceFiles)
{
    FileManifest manifest;
    std::unordered_map<std::string, int> usedNames;

    for (const auto& root : roots) {
        std::error_code ec;
        if (!std::filesystem::exists(root, ec)) continue;

        const std::string rootName =
            unique_root_name(root, usedNames);

        manifest.roots.push_back(rootName);

        if (std::filesystem::is_directory(root, ec)) {
            manifest.entries.push_back({rootName, true, 0});
            sourceFiles.emplace_back();

            for (std::filesystem::recursive_directory_iterator it(
                     root,
                     std::filesystem::directory_options::skip_permission_denied,
                     ec),
                 end;
                 it != end && !ec;
                 it.increment(ec)) {
                const auto relative =
                    std::filesystem::relative(it->path(), root, ec);

                if (ec) continue;

                const std::filesystem::path manifestPath =
                    std::filesystem::path(
                        std::u8string(
                            reinterpret_cast<const char8_t*>(rootName.data()),
                            rootName.size())) /
                    relative;

                FileEntry entry;
                entry.relativePath = path_utf8(manifestPath);
                entry.directory = it->is_directory(ec);

                if (!entry.directory && it->is_regular_file(ec)) {
                    entry.size = it->file_size(ec);
                }

                manifest.entries.push_back(std::move(entry));
                sourceFiles.push_back(
                    it->is_regular_file(ec)
                        ? it->path()
                        : std::filesystem::path{});

                if (manifest.entries.size() > 10000)
                    throw std::runtime_error("too many clipboard entries");
            }
        } else if (std::filesystem::is_regular_file(root, ec)) {
            manifest.entries.push_back({
                rootName,
                false,
                std::filesystem::file_size(root, ec)});
            sourceFiles.push_back(root);
        }
    }

    return manifest;
}

std::vector<std::byte> serialize_manifest(const FileManifest& manifest)
{
    std::vector<std::byte> out;
    write_u32(out, static_cast<std::uint32_t>(manifest.roots.size()));

    for (const auto& root : manifest.roots)
        write_string(out, root);

    write_u32(out, static_cast<std::uint32_t>(manifest.entries.size()));

    for (const auto& entry : manifest.entries) {
        out.push_back(static_cast<std::byte>(entry.directory ? 1 : 0));
        write_u64(out, entry.size);
        write_string(out, entry.relativePath);
    }

    return out;
}

FileManifest deserialize_manifest(std::span<const std::byte> payload)
{
    std::size_t offset = 0;
    FileManifest manifest;

    const std::uint32_t rootCount = read_u32(payload, offset);
    if (rootCount > 256) throw std::runtime_error("too many manifest roots");

    for (std::uint32_t i = 0; i < rootCount; ++i)
        manifest.roots.push_back(read_string(payload, offset));

    const std::uint32_t count = read_u32(payload, offset);
    if (count > 10000) throw std::runtime_error("too many manifest entries");

    manifest.entries.reserve(count);

    for (std::uint32_t i = 0; i < count; ++i) {
        if (offset >= payload.size())
            throw std::runtime_error("manifest truncated");

        FileEntry entry;
        entry.directory =
            std::to_integer<unsigned char>(payload[offset++]) != 0;
        entry.size = read_u64(payload, offset);
        entry.relativePath = read_string(payload, offset);

        const std::filesystem::path relative =
            std::filesystem::u8path(entry.relativePath);

        if (!safe_relative_path(relative))
            throw std::runtime_error("unsafe manifest path");

        manifest.entries.push_back(std::move(entry));
    }

    if (offset != payload.size())
        throw std::runtime_error("manifest trailing data");

    return manifest;
}

} // namespace srd::transfer
