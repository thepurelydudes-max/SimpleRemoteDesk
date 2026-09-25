#pragma once

#include "capture/screen_capture.h"

#include <cstddef>
#include <span>
#include <vector>

namespace srd::codec {

std::vector<std::byte> encode_jpeg_wic(
    const capture::Frame& frame,
    float quality = 0.80f);

} // namespace srd::codec
