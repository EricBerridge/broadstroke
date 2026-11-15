// Running a command-line program for the studio window (spec 2.10): the
// steps outside the engine (observe, interpret) are programs of their own,
// and the window starts them and reads the files they write.
#pragma once

#include <atomic>
#include <functional>
#include <string>

namespace bs {

struct ToolResult {
  bool started = false;  // the program was started
  bool stopped = false;  // it was ended because `stop` was set
  int exit_code = -1;    // what it returned, if it ran to its end
  std::string error;     // why it could not be started
  bool ok() const { return started && !stopped && exit_code == 0; }
};

// Runs a program without a window and waits for it. Each line it prints (to
// standard output or standard error) is handed to `on_line`, on the calling
// thread. When `stop` becomes true the program is ended together with every
// program it started. Call it from a worker thread: it blocks.
ToolResult run_tool(const std::wstring& command_line, const std::wstring& directory,
                    const std::function<void(const std::string& line)>& on_line, const std::atomic<bool>& stop);

// An argument as it must be written in a command line.
std::wstring quote_argument(const std::wstring& argument);

}  // namespace bs
