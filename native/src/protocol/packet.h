#pragma once
#include <cstdint>

namespace srd::protocol {

enum class PacketType : std::uint8_t {
    Screen = 1,
    MouseMove = 2,
    MouseButton = 3,
    MouseWheel = 4,
    Key = 5,
    AudioFormat = 6,
    Audio = 7,
    KeyCombination = 8,
    Preview = 9,
    Cursor = 10,
    FileClipboardGet = 20,
    FileManifest = 21,
    FileChunk = 22,
    FileTransferEnd = 23,
    FileClipboardPut = 24,
    FileTransferAck = 25,
};

} // namespace srd::protocol
