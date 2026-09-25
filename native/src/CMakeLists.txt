add_executable(SimpleRemoteDeskNative WIN32
    app/main.cpp
)

target_compile_definitions(SimpleRemoteDeskNative PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(SimpleRemoteDeskNative PRIVATE
    user32
    gdi32
    shell32
    ole32
)
