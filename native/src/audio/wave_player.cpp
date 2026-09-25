#include "audio/wave_player.h"

#include <algorithm>
#include <cstring>

namespace srd::audio {

WavePlayer::~WavePlayer()
{
    close();
}

bool WavePlayer::open(AudioFormat format)
{
    close();

    WAVEFORMATEX wave{};
    wave.wFormatTag = WAVE_FORMAT_PCM;
    wave.nChannels = format.channels;
    wave.nSamplesPerSec = format.sampleRate;
    wave.wBitsPerSample = 16;
    wave.nBlockAlign =
        static_cast<WORD>(wave.nChannels * wave.wBitsPerSample / 8);
    wave.nAvgBytesPerSec =
        wave.nSamplesPerSec * wave.nBlockAlign;

    const MMRESULT result = ::waveOutOpen(
        &waveOut_,
        WAVE_MAPPER,
        &wave,
        reinterpret_cast<DWORD_PTR>(&WavePlayer::callback),
        reinterpret_cast<DWORD_PTR>(this),
        CALLBACK_FUNCTION);

    if (result != MMSYSERR_NOERROR) {
        waveOut_ = nullptr;
        return false;
    }

    closing_ = false;

    buffers_.clear();
    buffers_.reserve(24);

    for (int i = 0; i < 24; ++i) {
        buffers_.push_back(std::make_unique<Buffer>());
    }

    return true;
}

void WavePlayer::close() noexcept
{
    HWAVEOUT handle = nullptr;

    {
        std::lock_guard lock(mutex_);
        closing_ = true;
        handle = waveOut_;
    }

    cv_.notify_all();

    if (!handle) return;

    ::waveOutReset(handle);

    for (auto& buffer : buffers_) {
        if (buffer->prepared) {
            ::waveOutUnprepareHeader(
                handle,
                &buffer->header,
                sizeof(WAVEHDR));

            buffer->prepared = false;
        }
    }

    ::waveOutClose(handle);

    {
        std::lock_guard lock(mutex_);
        waveOut_ = nullptr;
        buffers_.clear();
    }
}

bool WavePlayer::write(std::span<const std::byte> pcm)
{
    if (pcm.empty()) return true;

    std::unique_lock lock(mutex_);

    cv_.wait(lock, [&] {
        if (closing_ || !waveOut_) return true;

        return std::any_of(
            buffers_.begin(),
            buffers_.end(),
            [](const auto& buffer) {
                return !buffer->busy;
            });
    });

    if (closing_ || !waveOut_) return false;

    auto it = std::find_if(
        buffers_.begin(),
        buffers_.end(),
        [](const auto& buffer) {
            return !buffer->busy;
        });

    if (it == buffers_.end()) return false;

    Buffer& buffer = **it;

    if (buffer.prepared) {
        ::waveOutUnprepareHeader(
            waveOut_,
            &buffer.header,
            sizeof(WAVEHDR));

        buffer.prepared = false;
    }

    buffer.data.resize(pcm.size());
    std::memcpy(buffer.data.data(), pcm.data(), pcm.size());

    buffer.header = {};
    buffer.header.lpData = buffer.data.data();
    buffer.header.dwBufferLength =
        static_cast<DWORD>(buffer.data.size());

    MMRESULT result = ::waveOutPrepareHeader(
        waveOut_,
        &buffer.header,
        sizeof(WAVEHDR));

    if (result != MMSYSERR_NOERROR) {
        return false;
    }

    buffer.prepared = true;
    buffer.busy = true;

    result = ::waveOutWrite(
        waveOut_,
        &buffer.header,
        sizeof(WAVEHDR));

    if (result != MMSYSERR_NOERROR) {
        buffer.busy = false;
        cv_.notify_one();
        return false;
    }

    return true;
}

void CALLBACK WavePlayer::callback(
    HWAVEOUT,
    UINT message,
    DWORD_PTR instance,
    DWORD_PTR param1,
    DWORD_PTR)
{
    if (message != WOM_DONE || !instance || !param1) return;

    auto* self = reinterpret_cast<WavePlayer*>(instance);
    auto* header = reinterpret_cast<WAVEHDR*>(param1);

    self->on_done(header);
}

void WavePlayer::on_done(WAVEHDR* header)
{
    std::lock_guard lock(mutex_);

    for (auto& buffer : buffers_) {
        if (&buffer->header == header) {
            buffer->busy = false;
            cv_.notify_one();
            return;
        }
    }
}

} // namespace srd::audio
