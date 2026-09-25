add_library(srd_core STATIC
    core/socket_runtime.cpp
    core/tcp_socket.cpp
    core/session.cpp
    protocol/message.cpp
    security/crypto.cpp
    security/auth.cpp
    security/secure_session.cpp
    input/control_message.cpp
    input/injector.cpp
    control/control_client.cpp
    audio/audio_channel.cpp
    audio/wave_player.cpp
    audio/loopback_capture.cpp
    host/host_service.cpp
    ui/theme.cpp
    settings/settings.cpp
    settings/profiles.cpp
    network/discovery.cpp
    capture/gdi_screen_capture.cpp
    capture/dxgi_screen_capture.cpp
    codec/jpeg_wic.cpp
    codec/jpeg_wic_decoder.cpp
    video/latest_frame_mailbox.cpp
    video/screen_producer.cpp
    video/frame_packet.cpp
    video/video_channel.cpp
)

target_include_directories(srd_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(srd_core PUBLIC
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(srd_core PUBLIC
    ws2_32
    bcrypt
    user32
    gdi32
    ole32
    windowscodecs
    d3d11
    dxgi
    winmm
    uuid
)

add_executable(SimpleRemoteHost
    host/main.cpp
)

target_link_libraries(SimpleRemoteHost PRIVATE
    srd_core
)

add_executable(SimpleRemoteViewer
    viewer/main.cpp
)

target_link_libraries(SimpleRemoteViewer PRIVATE
    srd_core
)


add_executable(CaptureSmokeTest
    tools/capture_smoke_test.cpp
)

target_link_libraries(CaptureSmokeTest PRIVATE
    srd_core
)


add_executable(StreamSmokeTest
    tools/stream_smoke_test.cpp
)

target_link_libraries(StreamSmokeTest PRIVATE
    srd_core
)


add_executable(VideoStreamClientTest
    tools/video_stream_client_test.cpp
)

target_link_libraries(VideoStreamClientTest PRIVATE
    srd_core
)


add_executable(SimpleRemoteViewerGui WIN32
    viewer/gui_main.cpp
    viewer/live_view_window.cpp
    viewer/session_runner.cpp
    viewer/home_window.cpp
    viewer/profile_dialog.cpp
)

target_link_libraries(SimpleRemoteViewerGui PRIVATE
    srd_core
    shell32
    crypt32
    advapi32
    dwmapi
)


add_executable(SimpleRemoteHostGui WIN32
    host/gui_main.cpp
    host/host_window.cpp
)

target_link_libraries(SimpleRemoteHostGui PRIVATE
    srd_core
    shell32
    crypt32
    advapi32
    dwmapi
)
