#pragma once

#include "audio/loopback_capture.h"

#include <windows.h>
#include <mmsystem.h>

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace srd::audio {

class WavePlayer {
public:
    WavePlayer() = default;
    ~WavePlayer();

    bool open(AudioFormat format);
    void close() noexcept;
    bool write(std::span<const std::byte> pcm);

private:
    struct Buffer {
        WAVEHDR header{};
        std::vector<char> data;
        bool prepared{false};
        bool busy{false};
    };

    static void CALLBACK callback(
        HWAVEOUT waveOut,
        UINT message,
        DWORD_PTR instance,
        DWORD_PTR param1,
        DWORD_PTR param2);

    void on_done(WAVEHDR* header);

    HWAVEOUT waveOut_{nullptr};
    std::vector<std::unique_ptr<Buffer>> buffers_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool closing_{false};
};

} // namespace srd::audio
