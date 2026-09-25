#include "viewer/session_runner.h"

#include "codec/jpeg_wic_decoder.h"
#include "control/control_client.h"
#include "video/video_channel.h"
#include "viewer/frame_mailbox.h"
#include "viewer/live_view_window.h"

#include <objbase.h>

#include <memory>
#include <thread>

namespace srd::viewer {

int run_live_session(
    HINSTANCE instance,
    const std::string& host,
    const std::string& password,
    int showCommand)
{
    control::ControlClient control(host, password, 45900);

    try {
        control.connect();
    }
    catch (...) {
        ::MessageBoxW(
            nullptr,
            L"Не удалось подключить канал управления. Проверьте адрес, пароль и запущенный Host.",
            L"Simple Remote Viewer",
            MB_OK | MB_ICONERROR);

        return 2;
    }

    DecodedFrameMailbox mailbox;
    LiveViewWindow window(mailbox, &control);
    video::VideoClient client(host, password, 45902);

    std::thread videoThread([&] {
        const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool comInitialized = SUCCEEDED(hr);

        try {
            client.receive_forever([&](video::EncodedFrame&& frame) {
                auto decoded = std::make_shared<codec::DecodedBitmap>(
                    codec::decode_jpeg_wic(frame.jpeg));

                mailbox.publish(std::move(decoded));
                window.notify_frame();
            });
        }
        catch (...) {
        }

        if (comInitialized) {
            ::CoUninitialize();
        }
    });

    const int exitCode = window.run(instance, showCommand);

    client.stop();
    control.disconnect();

    if (videoThread.joinable()) {
        videoThread.join();
    }

    return exitCode;
}

} // namespace srd::viewer
