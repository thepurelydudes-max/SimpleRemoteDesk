#pragma once

#include <windows.h>

namespace srd::ui {

void enable_dpi_awareness() noexcept;
UINT system_dpi() noexcept;
UINT window_dpi(HWND hwnd) noexcept;
int scale_value(int value, UINT dpi) noexcept;
RECT scale_rect(RECT rect, UINT dpi) noexcept;

} // namespace srd::ui
