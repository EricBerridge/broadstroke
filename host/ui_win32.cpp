#include "ui.hpp"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace broadstroke::host {

namespace {

constexpr int kIdInput = 1001;
constexpr int kIdStyle = 1002;
constexpr int kIdSubstrate = 1003;
constexpr int kIdRender = 1004;
constexpr int kIdValidate = 1005;
constexpr int kIdStatus = 1006;
constexpr int kIdPreview = 1007;

struct UiState {
  HINSTANCE instance = nullptr;
  HWND window = nullptr;
  HWND input = nullptr;
  HWND style = nullptr;
  HWND substrate = nullptr;
  HWND validate = nullptr;
  HWND status = nullptr;
  HWND preview = nullptr;
  HBITMAP bitmap = nullptr;
  std::wstring exePath;
  std::wstring outputPath;
};

std::wstring widen(const char* text) {
  if (text == nullptr || *text == '\0') {
    return {};
  }
  const int needed = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
  if (needed <= 1) {
    return {};
  }
  std::wstring result(static_cast<std::size_t>(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), needed);
  result.resize(static_cast<std::size_t>(needed - 1));
  return result;
}

std::wstring getWindowText(HWND hwnd) {
  const int length = GetWindowTextLengthW(hwnd);
  std::wstring text(static_cast<std::size_t>(length + 1), L'\0');
  if (length > 0) {
    GetWindowTextW(hwnd, text.data(), length + 1);
  }
  text.resize(static_cast<std::size_t>(length));
  return text;
}

std::wstring comboText(HWND combo) {
  const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
  if (index == CB_ERR) {
    return {};
  }
  const LRESULT length = SendMessageW(combo, CB_GETLBTEXTLEN,
                                      static_cast<WPARAM>(index), 0);
  std::wstring text(static_cast<std::size_t>(length), L'\0');
  SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(index),
               reinterpret_cast<LPARAM>(text.data()));
  return text;
}

void setStatus(UiState& state, const wchar_t* text) {
  SetWindowTextW(state.status, text);
}

void addComboItems(HWND combo, const std::vector<const wchar_t*>& items,
                   int selected) {
  for (const wchar_t* item : items) {
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
  }
  SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(selected), 0);
}

HMENU controlId(int id) {
  return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

std::wstring quote(std::wstring value) {
  std::wstring escaped;
  escaped.reserve(value.size() + 2);
  escaped.push_back(L'"');
  for (wchar_t ch : value) {
    if (ch == L'"') {
      escaped.push_back(L'\\');
    }
    escaped.push_back(ch);
  }
  escaped.push_back(L'"');
  return escaped;
}

void loadPreview(UiState& state) {
  if (state.bitmap != nullptr) {
    DeleteObject(state.bitmap);
    state.bitmap = nullptr;
  }
  state.bitmap = static_cast<HBITMAP>(LoadImageW(nullptr, state.outputPath.c_str(),
                                                 IMAGE_BITMAP, 0, 0,
                                                 LR_LOADFROMFILE));
  SendMessageW(state.preview, STM_SETIMAGE, IMAGE_BITMAP,
               reinterpret_cast<LPARAM>(state.bitmap));
}

void renderAsync(UiState& state) {
  const std::wstring input = getWindowText(state.input);
  const std::wstring style = comboText(state.style);
  const std::wstring substrate = comboText(state.substrate);
  const bool validate = SendMessageW(state.validate, BM_GETCHECK, 0, 0) == BST_CHECKED;

  if (input.empty()) {
    setStatus(state, L"Choose an input image first.");
    return;
  }

  EnableWindow(GetDlgItem(state.window, kIdRender), FALSE);
  setStatus(state, L"Rendering...");

  std::thread([input, style, substrate, validate, &state] {
    std::wstring command = quote(state.exePath) + L" paint " + quote(input) +
        L" --style " + style + L" --substrate " + substrate +
        L" --output " + quote(state.outputPath);
    if (validate) {
      command += L" --validate";
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> commandLine(command.begin(), command.end());
    commandLine.push_back(L'\0');

    const BOOL ok = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr,
                                   FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                                   &startup, &process);
    DWORD exitCode = 1;
    if (ok) {
      WaitForSingleObject(process.hProcess, INFINITE);
      GetExitCodeProcess(process.hProcess, &exitCode);
      CloseHandle(process.hProcess);
      CloseHandle(process.hThread);
    }

    PostMessageW(state.window, WM_APP + 1, static_cast<WPARAM>(ok ? exitCode : 1), 0);
  }).detach();
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  auto* state = reinterpret_cast<UiState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  switch (msg) {
  case WM_NCCREATE: {
    const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    return TRUE;
  }
  case WM_CREATE: {
    state = reinterpret_cast<UiState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    state->window = hwnd;

    CreateWindowW(L"STATIC", L"Input", WS_CHILD | WS_VISIBLE,
                  16, 18, 80, 20, hwnd, nullptr, state->instance, nullptr);
    state->input = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                 92, 16, 450, 24, hwnd, controlId(kIdInput),
                                 state->instance, nullptr);

    CreateWindowW(L"STATIC", L"Style", WS_CHILD | WS_VISIBLE,
                  16, 54, 80, 20, hwnd, nullptr, state->instance, nullptr);
    state->style = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                 92, 52, 180, 120, hwnd, controlId(kIdStyle),
                                 state->instance, nullptr);
    addComboItems(state->style,
                  {L"watercolor", L"pencil_sketch", L"pastel_drawing"}, 0);

    CreateWindowW(L"STATIC", L"Substrate", WS_CHILD | WS_VISIBLE,
                  292, 54, 80, 20, hwnd, nullptr, state->instance, nullptr);
    state->substrate = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                     372, 52, 170, 120, hwnd, controlId(kIdSubstrate),
                                     state->instance, nullptr);
    addComboItems(state->substrate,
                  {L"cold_press", L"canvas", L"smooth_bristol"}, 0);

    state->validate = CreateWindowW(L"BUTTON", L"Validation", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                    92, 88, 120, 24, hwnd, controlId(kIdValidate),
                                    state->instance, nullptr);
    CreateWindowW(L"BUTTON", L"Render", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                  422, 86, 120, 28, hwnd, controlId(kIdRender),
                  state->instance, nullptr);

    state->preview = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_BITMAP | SS_CENTERIMAGE | WS_BORDER,
                                   16, 130, 526, 420, hwnd, controlId(kIdPreview),
                                   state->instance, nullptr);
    state->status = CreateWindowW(L"STATIC", L"Ready", WS_CHILD | WS_VISIBLE,
                                  16, 562, 526, 20, hwnd, controlId(kIdStatus),
                                  state->instance, nullptr);
    return 0;
  }
  case WM_COMMAND:
    if (LOWORD(wparam) == kIdRender && state != nullptr) {
      renderAsync(*state);
      return 0;
    }
    break;
  case WM_APP + 1:
    if (state != nullptr) {
      EnableWindow(GetDlgItem(hwnd, kIdRender), TRUE);
      if (wparam == 0) {
        loadPreview(*state);
        setStatus(*state, L"Rendered broadstroke_ui_preview.bmp");
      } else {
        setStatus(*state, L"Render failed. Run the same command in a terminal for details.");
      }
      return 0;
    }
    break;
  case WM_DESTROY:
    if (state != nullptr && state->bitmap != nullptr) {
      DeleteObject(state->bitmap);
      state->bitmap = nullptr;
    }
    PostQuitMessage(0);
    return 0;
  }

  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace

int runPresetExplorerUi(const char* initialImagePath) {
  UiState state;
  state.instance = GetModuleHandleW(nullptr);

  std::array<wchar_t, MAX_PATH> exe{};
  GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
  state.exePath = exe.data();

  const auto output = std::filesystem::current_path() / "broadstroke_ui_preview.bmp";
  state.outputPath = output.wstring();

  const WNDCLASSW wc{
      CS_HREDRAW | CS_VREDRAW,
      windowProc,
      0,
      0,
      state.instance,
      LoadIconW(nullptr, MAKEINTRESOURCEW(32512)),
      LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)),
      reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
      nullptr,
      L"BroadstrokePresetExplorer"};
  RegisterClassW(&wc);

  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Broadstroke Preset Explorer",
                              WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                              CW_USEDEFAULT, CW_USEDEFAULT, 580, 640,
                              nullptr, nullptr, state.instance, &state);
  if (hwnd == nullptr) {
    return 1;
  }

  const std::wstring initial = initialImagePath != nullptr ? widen(initialImagePath) : L"test.jpg";
  SetWindowTextW(state.input, initial.c_str());
  ShowWindow(hwnd, SW_SHOWDEFAULT);
  UpdateWindow(hwnd);

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}

}  // namespace broadstroke::host

#else

#include <cstdio>

namespace broadstroke::host {

int runPresetExplorerUi(const char*) {
  std::fprintf(stderr, "broadstroke ui is available on Windows builds only.\n");
  return 3;
}

}  // namespace broadstroke::host

#endif
