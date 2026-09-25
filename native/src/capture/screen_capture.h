#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace srd::capture {

struct Frame {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t stride{};
    std::vector<std::byte> pixels;
};

class IScreenCapture {
public:
    virtual ~IScreenCapture() = default;
    virtual bool start() = 0;
    virtual void stop() noexcept = 0;
    virtual bool next_frame(Frame& frame) = 0;
};

std::unique_ptr<IScreenCapture> create_best_capture();

} // namespace srd::capture
