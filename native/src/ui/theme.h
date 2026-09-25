#pragma once

#include <windows.h>

namespace srd::ui {

constexpr COLORREF Bg = RGB(10, 20, 36);
constexpr COLORREF Header = RGB(13, 27, 46);
constexpr COLORREF Surface = RGB(17, 34, 58);
constexpr COLORREF Surface2 = RGB(23, 43, 71);
constexpr COLORREF Surface3 = RGB(31, 57, 92);
constexpr COLORREF Border = RGB(66, 106, 151);
constexpr COLORREF BorderSoft = RGB(49, 79, 115);
constexpr COLORREF Accent = RGB(30, 132, 255);
constexpr COLORREF AccentHover = RGB(54, 151, 255);
constexpr COLORREF AccentPressed = RGB(14, 106, 224);
constexpr COLORREF Text = RGB(245, 248, 253);
constexpr COLORREF Muted = RGB(156, 177, 207);
constexpr COLORREF Muted2 = RGB(118, 143, 177);
constexpr COLORREF Success = RGB(51, 211, 153);
constexpr COLORREF Offline = RGB(126, 145, 174);
constexpr COLORREF Danger = RGB(246, 87, 100);

HFONT create_font(int pointSize, int weight = FW_NORMAL);
void fill_rect(HDC dc, const RECT& rect, COLORREF color);
void fill_round_rect(HDC dc, const RECT& rect, int radius, COLORREF fill, COLORREF border = CLR_INVALID);
void draw_text(HDC dc, const wchar_t* text, RECT rect, COLORREF color, HFONT font, UINT format);
void enable_dark_title_bar(HWND hwnd);

} // namespace srd::ui
