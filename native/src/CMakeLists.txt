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
    capture/gdi_screen_capture.cpp
    codec/jpeg_wic.cpp
    video/latest_frame_mailbox.cpp
    video/screen_producer.cpp
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
