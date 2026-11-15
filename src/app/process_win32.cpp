#include "app/process_win32.hpp"

#include <windows.h>

#include <algorithm>

namespace bs {

std::wstring quote_argument(const std::wstring& argument) {
  // The rules of the C runtime's command-line parser: backslashes are
  // literal unless they come before a quote, where each is doubled.
  std::wstring out = L"\"";
  size_t backslashes = 0;
  for (wchar_t c : argument) {
    if (c == L'\\') {
      ++backslashes;
    } else if (c == L'"') {
      out.append(backslashes + 1, L'\\');
      backslashes = 0;
    } else {
      backslashes = 0;
    }
    out += c;
  }
  out.append(backslashes, L'\\');
  out += L'"';
  return out;
}

ToolResult run_tool(const std::wstring& command_line, const std::wstring& directory,
                    const std::function<void(const std::string& line)>& on_line, const std::atomic<bool>& stop) {
  ToolResult result;

  SECURITY_ATTRIBUTES inherit{};
  inherit.nLength = sizeof(inherit);
  inherit.bInheritHandle = TRUE;
  HANDLE read_end = nullptr, write_end = nullptr;
  if (!CreatePipe(&read_end, &write_end, &inherit, 0)) {
    result.error = "cannot create a pipe";
    return result;
  }
  SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);
  HANDLE no_input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0,
                                nullptr);

  // A job holds the program and whatever it starts (a model server), so
  // that all of it ends when it is stopped or when this function returns.
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  if (job) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
  }

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = no_input != INVALID_HANDLE_VALUE ? no_input : nullptr;
  startup.hStdOutput = write_end;
  startup.hStdError = write_end;
  PROCESS_INFORMATION process{};
  std::wstring command = command_line;  // CreateProcessW may write into it
  const BOOL created = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                                      CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                                      directory.empty() ? nullptr : directory.c_str(), &startup, &process);
  const DWORD create_error = created ? 0 : GetLastError();
  CloseHandle(write_end);  // the child holds its own; ours would keep the pipe open for ever
  if (no_input != INVALID_HANDLE_VALUE) CloseHandle(no_input);
  if (!created) {
    result.error = "cannot start the program (Windows error " + std::to_string(create_error) + ")";
    CloseHandle(read_end);
    if (job) CloseHandle(job);
    return result;
  }
  result.started = true;
  if (job) AssignProcessToJobObject(job, process.hProcess);
  ResumeThread(process.hThread);
  CloseHandle(process.hThread);

  std::string pending;
  auto take = [&](const char* data, size_t n) {
    for (size_t i = 0; i < n; ++i) {
      const char c = data[i];
      if (c == '\n' || c == '\r') {  // a carriage return alone redraws a progress line
        if (!pending.empty() && on_line) on_line(pending);
        pending.clear();
      } else {
        pending += c;
      }
    }
  };

  char buffer[4096];
  bool exited = false;
  while (true) {
    if (stop.load()) {
      result.stopped = true;
      if (job) TerminateJobObject(job, 1);
      else TerminateProcess(process.hProcess, 1);
      break;
    }
    DWORD available = 0;
    if (!PeekNamedPipe(read_end, nullptr, 0, nullptr, &available, nullptr)) break;  // every writer has closed
    if (available == 0) {
      if (exited) break;  // it has ended and everything it printed has been read
      exited = WaitForSingleObject(process.hProcess, 50) == WAIT_OBJECT_0;
      continue;
    }
    DWORD got = 0;
    if (!ReadFile(read_end, buffer, std::min<DWORD>(available, sizeof(buffer)), &got, nullptr) || got == 0) break;
    take(buffer, got);
  }
  if (!pending.empty() && on_line && !result.stopped) on_line(pending);

  // It may have closed its output and still be running.
  while (WaitForSingleObject(process.hProcess, 50) != WAIT_OBJECT_0) {
    if (!stop.load() || result.stopped) continue;
    result.stopped = true;
    if (job) TerminateJobObject(job, 1);
    else TerminateProcess(process.hProcess, 1);
  }
  DWORD code = 0;
  if (!result.stopped && GetExitCodeProcess(process.hProcess, &code)) result.exit_code = static_cast<int>(code);
  CloseHandle(process.hProcess);
  CloseHandle(read_end);
  if (job) CloseHandle(job);  // ends whatever the program left running
  return result;
}

}  // namespace bs
