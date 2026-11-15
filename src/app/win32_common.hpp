// Small things the windows of the application share (Windows only).
#pragma once

#include <windows.h>

#include <string>

namespace bs {

// The DPI of the monitor a window is on.
int window_dpi(HWND hwnd);

// The system's message font at a DPI, with its face name if asked for. The
// caller deletes the font. Null if it cannot be made.
HFONT create_message_font(int dpi, std::wstring* face = nullptr);

// The height of one line of text in a font, in pixels.
int font_line_height(HWND hwnd, HFONT font);

}  // namespace bs
