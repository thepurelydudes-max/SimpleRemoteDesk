cmake_minimum_required(VERSION 3.24)
project(SimpleRemoteDeskNative VERSION 3.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(SimpleRemoteDesk WIN32
  src/native.cpp
  src/app.rc
)

target_compile_definitions(SimpleRemoteDesk PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00
)

if(MSVC)
  target_compile_options(SimpleRemoteDesk PRIVATE /O2 /Ob3 /GL /Gy /Gw /permissive- /W4 /EHsc /utf-8)
  target_link_options(SimpleRemoteDesk PRIVATE /LTCG /OPT:REF /OPT:ICF)
endif()

target_link_libraries(SimpleRemoteDesk PRIVATE
  ws2_32 bcrypt crypt32 gdiplus ole32 shell32 shlwapi comctl32 dwmapi
  winmm avrt uuid iphlpapi user32 gdi32 advapi32
)

set_target_properties(SimpleRemoteDesk PROPERTIES OUTPUT_NAME "SimpleRemoteDesk")
