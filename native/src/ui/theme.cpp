#include "ui/theme.h"

#include <dwmapi.h>

namespace srd::ui {

HFONT create_font(int pointSize, int weight)
{
    HDC dc = ::GetDC(nullptr);
    const int dpi = dc ? ::GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc) ::ReleaseDC(nullptr, dc);

    const int height = -::MulDiv(pointSize, dpi, 72);

    return ::CreateFontW(
        height,
        0,
        0,
        0,
        weight,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");
}

void fill_rect(HDC dc, const RECT& rect, COLORREF color)
{
    HBRUSH brush = ::CreateSolidBrush(color);
    ::FillRect(dc, &rect, brush);
    ::DeleteObject(brush);
}

void fill_round_rect(
    HDC dc,
    const RECT& rect,
    int radius,
    COLORREF fill,
    COLORREF border)
{
    HBRUSH brush = ::CreateSolidBrush(fill);
    HPEN pen = ::CreatePen(
        PS_SOLID,
        border == CLR_INVALID ? 0 : 1,
        border == CLR_INVALID ? fill : border);

    HGDIOBJ oldBrush = ::SelectObject(dc, brush);
    HGDIOBJ oldPen = ::SelectObject(dc, pen);

    ::RoundRect(
        dc,
        rect.left,
        rect.top,
        rect.right,
        rect.bottom,
        radius,
        radius);

    ::SelectObject(dc, oldBrush);
    ::SelectObject(dc, oldPen);
    ::DeleteObject(brush);
    ::DeleteObject(pen);
}

void draw_text(
    HDC dc,
    const wchar_t* text,
    RECT rect,
    COLORREF color,
    HFONT font,
    UINT format)
{
    const int oldBk = ::SetBkMode(dc, TRANSPARENT);
    const COLORREF oldColor = ::SetTextColor(dc, color);
    HGDIOBJ oldFont = ::SelectObject(dc, font);

    ::DrawTextW(dc, text, -1, &rect, format);

    ::SelectObject(dc, oldFont);
    ::SetTextColor(dc, oldColor);
    ::SetBkMode(dc, oldBk);
}

void enable_dark_title_bar(HWND hwnd)
{
    BOOL enabled = TRUE;
    HRESULT hr = ::DwmSetWindowAttribute(
        hwnd,
        20,
        &enabled,
        sizeof(enabled));

    if (FAILED(hr)) {
        ::DwmSetWindowAttribute(
            hwnd,
            19,
            &enabled,
            sizeof(enabled));
    }
}

} // namespace srd::ui
