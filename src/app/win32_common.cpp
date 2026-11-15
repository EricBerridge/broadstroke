#include "app/win32_common.hpp"

#include <algorithm>

namespace bs {

int window_dpi(HWND hwnd) {
  // Looked up at run time so that the program still starts on versions of
  // Windows that do not have per-monitor awareness.
  HMODULE user = GetModuleHandleW(L"user32.dll");
  using GetDpiFn = UINT(WINAPI*)(HWND);
  GetDpiFn get_dpi = user ? reinterpret_cast<GetDpiFn>(GetProcAddress(user, "GetDpiForWindow")) : nullptr;
  if (get_dpi) {
    UINT dpi = get_dpi(hwnd);
    if (dpi != 0) return static_cast<int>(dpi);
  }
  HDC dc = GetDC(hwnd);
  int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 0;
  if (dc) ReleaseDC(hwnd, dc);
  return dpi > 0 ? dpi : 96;
}

HFONT create_message_font(int dpi, std::wstring* face) {
  NONCLIENTMETRICSW ncm{};
  ncm.cbSize = sizeof(ncm);
  bool ok = false;
  HMODULE user = GetModuleHandleW(L"user32.dll");
  using InfoForDpiFn = BOOL(WINAPI*)(UINT, UINT, PVOID, UINT, UINT);
  InfoForDpiFn info_for_dpi =
      user ? reinterpret_cast<InfoForDpiFn>(GetProcAddress(user, "SystemParametersInfoForDpi")) : nullptr;
  if (info_for_dpi)
    ok = info_for_dpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, static_cast<UINT>(dpi)) != FALSE;
  if (!ok) {
    ok = SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0) != FALSE;
    if (ok) {
      // These metrics are for the system DPI; rescale them for this window.
      HDC screen = GetDC(nullptr);
      int system_dpi = screen ? GetDeviceCaps(screen, LOGPIXELSY) : 0;
      if (screen) ReleaseDC(nullptr, screen);
      if (system_dpi > 0) ncm.lfMessageFont.lfHeight = MulDiv(ncm.lfMessageFont.lfHeight, dpi, system_dpi);
    }
  }
  if (!ok) return nullptr;
  if (face) *face = ncm.lfMessageFont.lfFaceName;
  return CreateFontIndirectW(&ncm.lfMessageFont);
}

int font_line_height(HWND hwnd, HFONT font) {
  int height = 16;
  if (HDC dc = GetDC(hwnd)) {
    HGDIOBJ prev = font ? SelectObject(dc, font) : nullptr;
    TEXTMETRICW tm{};
    if (GetTextMetricsW(dc, &tm)) height = std::max(8, static_cast<int>(tm.tmHeight + tm.tmExternalLeading));
    if (prev) SelectObject(dc, prev);
    ReleaseDC(hwnd, dc);
  }
  return height;
}

}  // namespace bs
