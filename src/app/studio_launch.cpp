#include "app/studio_launch.hpp"

#include <windows.h>

#include <shellapi.h>

#include <cstdio>
#include <string>

#include "app/process_win32.hpp"

namespace bs {

int run_studio() {
  // The studio is looked for from where this program is, not from the
  // current folder.
  std::wstring self(32768, L'\0');
  const DWORD n = GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size()));
  if (n == 0 || n >= self.size()) {
    std::fprintf(stderr, "cannot find where this program is\n");
    return 1;
  }
  self.resize(n);
  const size_t slash = self.find_last_of(L"/\\");
  const std::wstring studio =
      (slash == std::wstring::npos ? std::wstring() : self.substr(0, slash + 1)) + L"studio\\broadstroke_studio.exe";
  if (GetFileAttributesW(studio.c_str()) == INVALID_FILE_ATTRIBUTES) {
    std::fwprintf(stderr, L"the studio window was not found: %ls\nIt is built with the rest of the solution.\n",
                  studio.c_str());
    return 1;
  }

  // The photo is taken from the wide command line, so that its name may
  // contain any character.
  std::wstring command = quote_argument(studio);
  int argc = 0;
  if (wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
    if (argc >= 3) command += L" " + quote_argument(argv[2]);
    LocalFree(argv);
  }

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(studio.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup,
                      &process)) {
    std::fwprintf(stderr, L"the studio window could not be started: %ls\n", studio.c_str());
    return 1;
  }
  CloseHandle(process.hThread);

  // This is a console program. If the console was created just for it
  // (started from Explorer or a shortcut), let it go so that no empty
  // console window lingers next to the studio window. A console shared with
  // a shell is left alone.
  DWORD console_processes[2] = {};
  if (GetConsoleProcessList(console_processes, 2) == 1) FreeConsole();

  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(process.hProcess, &code);
  CloseHandle(process.hProcess);
  return static_cast<int>(code);
}

}  // namespace bs
