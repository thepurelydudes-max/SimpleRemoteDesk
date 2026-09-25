#include "audio/loopback_capture.h"

#include <windows.h>
#include <ks.h>
#include <ksmedia.h>
#include <mmreg.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace srd::audio {

LoopbackCapture::~LoopbackCapture()
{
    stop();
}

bool LoopbackCapture::start()
{
    stop();

    HRESULT hr = ::CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(enumerator_.GetAddressOf()));

    if (FAILED(hr)) return false;

    hr = enumerator_->GetDefaultAudioEndpoint(
        eRender,
        eMultimedia,
        device_.GetAddressOf());

    if (FAILED(hr)) return false;

    hr = device_->Activate(
        __uuidof(IAudioClient),
        CLSCTX_INPROC_SERVER,
        nullptr,
        reinterpret_cast<void**>(audioClient_.GetAddressOf()));

    if (FAILED(hr)) return false;

    hr = audioClient_->GetMixFormat(&mixFormat_);
    if (FAILED(hr) || !mixFormat_) return false;

    format_.sampleRate = mixFormat_->nSamplesPerSec;
    format_.channels = mixFormat_->nChannels;
    format_.bitsPerSample = 16;

    sourceFloat_ =
        mixFormat_->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;

    if (mixFormat_->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        mixFormat_->cbSize >= 22) {
        auto* extensible =
            reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mixFormat_);

        sourceFloat_ =
            extensible->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    }

    constexpr REFERENCE_TIME bufferDuration = 10000000; // 1 second

    hr = audioClient_->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        bufferDuration,
        0,
        mixFormat_,
        nullptr);

    if (FAILED(hr)) return false;

    hr = audioClient_->GetService(
        IID_PPV_ARGS(captureClient_.GetAddressOf()));

    if (FAILED(hr)) return false;

    hr = audioClient_->Start();
    return SUCCEEDED(hr);
}

void LoopbackCapture::stop() noexcept
{
    if (audioClient_) {
        audioClient_->Stop();
    }

    captureClient_.Reset();
    audioClient_.Reset();
    device_.Reset();
    enumerator_.Reset();

    if (mixFormat_) {
        ::CoTaskMemFree(mixFormat_);
        mixFormat_ = nullptr;
    }

    format_ = {};
    sourceFloat_ = false;
}

bool LoopbackCapture::read_chunk(std::vector<std::byte>& pcm16)
{
    pcm16.clear();

    if (!captureClient_ || !mixFormat_) return false;

    UINT32 packetFrames = 0;

    HRESULT hr = captureClient_->GetNextPacketSize(&packetFrames);
    if (FAILED(hr) || packetFrames == 0) return false;

    BYTE* data = nullptr;
    UINT32 frames = 0;
    DWORD flags = 0;

    hr = captureClient_->GetBuffer(
        &data,
        &frames,
        &flags,
        nullptr,
        nullptr);

    if (FAILED(hr)) return false;

    const bool ok = convert_packet(
        data,
        frames,
        flags,
        pcm16);

    captureClient_->ReleaseBuffer(frames);
    return ok && !pcm16.empty();
}

bool LoopbackCapture::convert_packet(
    const BYTE* data,
    UINT32 frames,
    DWORD flags,
    std::vector<std::byte>& pcm16)
{
    const std::size_t channels = mixFormat_->nChannels;
    const std::size_t sampleCount =
        static_cast<std::size_t>(frames) * channels;

    pcm16.resize(sampleCount * sizeof(std::int16_t));

    auto* output =
        reinterpret_cast<std::int16_t*>(pcm16.data());

    if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || !data) {
        std::fill(output, output + sampleCount, 0);
        return true;
    }

    const unsigned int bits = mixFormat_->wBitsPerSample;
    const std::size_t bytesPerSample =
        std::max<std::size_t>(1, bits / 8);

    for (std::size_t i = 0; i < sampleCount; ++i) {
        const BYTE* src = data + i * bytesPerSample;
        std::int16_t value = 0;

        if (sourceFloat_ && bytesPerSample >= 4) {
            float sample = 0.0f;
            std::memcpy(&sample, src, sizeof(float));

            if (!std::isfinite(sample)) sample = 0.0f;
            sample = std::clamp(sample, -1.0f, 1.0f);

            value = static_cast<std::int16_t>(
                std::lround(sample * 32767.0f));
        }
        else if (bits == 16 && bytesPerSample >= 2) {
            std::memcpy(&value, src, sizeof(value));
        }
        else if (bits == 24 && bytesPerSample >= 3) {
            std::int32_t raw =
                static_cast<std::int32_t>(src[0]) |
                (static_cast<std::int32_t>(src[1]) << 8) |
                (static_cast<std::int32_t>(src[2]) << 16);

            if (raw & 0x00800000) raw |= 0xff000000;
            value = static_cast<std::int16_t>(raw >> 8);
        }
        else if (bits == 32 && bytesPerSample >= 4) {
            std::int32_t raw = 0;
            std::memcpy(&raw, src, sizeof(raw));
            value = static_cast<std::int16_t>(raw >> 16);
        }
        else if (bits == 8) {
            value = static_cast<std::int16_t>(
                (static_cast<int>(src[0]) - 128) << 8);
        }

        output[i] = value;
    }

    return true;
}

} // namespace srd::audio
