#pragma once

#include "video/latest_frame_mailbox.h"

#include <cstddef>
#include <span>
#include <vector>

namespace srd::video {

std::vector<std::byte> serialize_frame(const EncodedFrame& frame);
EncodedFrame deserialize_frame(std::span<const std::byte> payload);

} // namespace srd::video
