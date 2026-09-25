#pragma once

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace srd::audio {

struct AudioFormat {
    std::uint32_t sampleRate{};
    std::uint16_t channels{};
    std::uint16_t bitsPerSample{16};
};

class LoopbackCapture {
public:
    LoopbackCapture() = default;
    ~LoopbackCapture();

    bool start();
    void stop() noexcept;

    [[nodiscard]] AudioFormat format() const noexcept { return format_; }

    // Returns true when one PCM16 chunk is available.
    bool read_chunk(std::vector<std::byte>& pcm16);

private:
    bool convert_packet(
        const BYTE* data,
        UINT32 frames,
        DWORD flags,
        std::vector<std::byte>& pcm16);

    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator_;
    Microsoft::WRL::ComPtr<IMMDevice> device_;
    Microsoft::WRL::ComPtr<IAudioClient> audioClient_;
    Microsoft::WRL::ComPtr<IAudioCaptureClient> captureClient_;

    WAVEFORMATEX* mixFormat_{nullptr};
    AudioFormat format_{};
    bool sourceFloat_{false};
};

} // namespace srd::audio
