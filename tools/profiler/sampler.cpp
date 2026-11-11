// Sampling profiler for Broadstroke (Windows). It launches a program,
// periodically suspends its threads, walks their stacks with dbghelp and
// writes a report of where the main thread's wall time went: a call tree,
// self and inclusive time per function (inlined functions are reported
// separately) and the hottest source lines. It needs no elevation.
//
// usage: broadstroke_profiler <report.txt> <interval_ms> <exe> [args...]
//
// Profile a build that has symbols and release optimization (the
// msvc-profile preset). Reading the report:
// - Times are wall time of the main thread, each sample weighted by the time
//   since the previous one. Sampling slows the program (about 20% at 2 ms),
//   so trust the shares and take absolute times from an unprofiled run.
// - Process and per-thread CPU times come from the operating system and are
//   exact. The breakdown of what the other threads run is biased toward
//   "blocked": they are sampled just after the main thread was suspended,
//   when they have often run out of work.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <timeapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace {

HANDLE g_proc = nullptr;
std::vector<std::string> g_names;
std::unordered_map<std::string, int> g_name_id;

int name_id(const std::string& s) {
  auto it = g_name_id.find(s);
  if (it != g_name_id.end()) return it->second;
  int id = static_cast<int>(g_names.size());
  g_names.push_back(s);
  g_name_id.emplace(s, id);
  return id;
}

struct Frame {
  int fn;    // function name id
  int file;  // file name id (-1 unknown)
  int line;
  bool in_exe;
};

std::unordered_map<DWORD64, std::vector<Frame>> g_cache;
std::string g_exe_module;

std::string base_name(const char* path) {
  std::string s = path ? path : "";
  size_t p = s.find_last_of("/\\");
  return p == std::string::npos ? s : s.substr(p + 1);
}

// Logical frames at an address, innermost inline frame first, the real
// function last.
const std::vector<Frame>& resolve(DWORD64 pc, bool is_return) {
  DWORD64 a = is_return && pc > 0 ? pc - 1 : pc;
  auto it = g_cache.find(a);
  if (it != g_cache.end()) return it->second;
  std::vector<Frame> out;

  IMAGEHLP_MODULE64 mod{};
  mod.SizeOfStruct = sizeof mod;
  std::string module = "?";
  if (SymGetModuleInfo64(g_proc, a, &mod)) module = mod.ModuleName;
  bool in_exe = _stricmp(module.c_str(), g_exe_module.c_str()) == 0;
  std::string prefix = in_exe ? "" : module + "!";

  char buf[sizeof(SYMBOL_INFO) + 2048];
  SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
  IMAGEHLP_LINE64 line{};
  DWORD ldisp = 0;
  DWORD64 disp = 0;

  DWORD n = SymAddrIncludeInlineTrace(g_proc, a);
  if (n) {
    DWORD ctx = 0, idx = 0;
    if (SymQueryInlineTrace(g_proc, a, 0, a, a, &ctx, &idx)) {
      for (DWORD i = 0; i < n; ++i) {
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 2047;
        if (!SymFromInlineContext(g_proc, a, ctx + i, &disp, sym)) continue;
        Frame f{name_id(prefix + sym->Name), -1, 0, in_exe};
        line = {};
        line.SizeOfStruct = sizeof line;
        if (SymGetLineFromInlineContext(g_proc, a, ctx + i, 0, &ldisp, &line)) {
          f.file = name_id(base_name(line.FileName));
          f.line = static_cast<int>(line.LineNumber);
        }
        out.push_back(f);
      }
    }
  }
  sym->SizeOfStruct = sizeof(SYMBOL_INFO);
  sym->MaxNameLen = 2047;
  Frame f{-1, -1, 0, in_exe};
  if (SymFromAddr(g_proc, a, &disp, sym)) {
    f.fn = name_id(prefix + sym->Name);
  } else {
    char tmp[64];
    std::snprintf(tmp, sizeof tmp, "%s!0x%llx", module.c_str(), static_cast<unsigned long long>(a));
    f.fn = name_id(tmp);
  }
  line = {};
  line.SizeOfStruct = sizeof line;
  if (SymGetLineFromAddr64(g_proc, a, &ldisp, &line)) {
    f.file = name_id(base_name(line.FileName));
    f.line = static_cast<int>(line.LineNumber);
  }
  out.push_back(f);
  return g_cache.emplace(a, std::move(out)).first->second;
}

// Suspends a thread and walks its stack. Returns program counters, leaf first.
bool walk(HANDLE thread, int max_frames, std::vector<DWORD64>& pcs) {
  pcs.clear();
  if (SuspendThread(thread) == static_cast<DWORD>(-1)) return false;
  CONTEXT ctx{};
  ctx.ContextFlags = CONTEXT_FULL;
  bool ok = GetThreadContext(thread, &ctx) != 0;
  if (ok) {
    STACKFRAME64 sf{};
    sf.AddrPC.Offset = ctx.Rip; sf.AddrPC.Mode = AddrModeFlat;
    sf.AddrFrame.Offset = ctx.Rbp; sf.AddrFrame.Mode = AddrModeFlat;
    sf.AddrStack.Offset = ctx.Rsp; sf.AddrStack.Mode = AddrModeFlat;
    for (int i = 0; i < max_frames; ++i) {
      if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, g_proc, thread, &sf, &ctx, nullptr,
                       SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
        break;
      if (sf.AddrPC.Offset == 0) break;
      pcs.push_back(sf.AddrPC.Offset);
    }
  }
  ResumeThread(thread);
  return ok && !pcs.empty();
}

double now() {
  static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
  LARGE_INTEGER c;
  QueryPerformanceCounter(&c);
  return static_cast<double>(c.QuadPart) / static_cast<double>(freq.QuadPart);
}

double ft_seconds(const FILETIME& ft) {
  ULARGE_INTEGER u;
  u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
  return static_cast<double>(u.QuadPart) * 1e-7;
}

struct Node {
  double t = 0.0;
  std::map<int, int> kids;  // fn id -> node index
};
std::vector<Node> g_tree(1);

void print_tree(FILE* f, int node, int depth, double total, double min_share) {
  std::vector<std::pair<double, int>> order;
  for (auto& kv : g_tree[node].kids) order.push_back({g_tree[kv.second].t, kv.first});
  std::sort(order.rbegin(), order.rend());
  for (auto& o : order) {
    if (o.first < min_share * total) continue;
    int child = g_tree[node].kids[o.second];
    std::fprintf(f, "%6.2f%% %8.2fs %*s%s\n", 100.0 * o.first / total, o.first, depth * 2, "",
                 g_names[o.second].c_str());
    print_tree(f, child, depth + 1, total, min_share);
  }
}

bool is_wait(const std::string& name) {
  return name.rfind("ntdll!Nt", 0) == 0 || name.rfind("ntdll!Zw", 0) == 0 || name.rfind("win32u!Nt", 0) == 0;
}

std::string quote(const std::wstring& a) {
  std::string s;
  for (wchar_t c : a) s += static_cast<char>(c);
  return s;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: broadstroke_profiler <report.txt> <interval_ms> <exe> [args...]\n");
    return 2;
  }
  std::wstring report = argv[1];
  int interval_ms = std::max(1, _wtoi(argv[2]));
  std::wstring cmd;
  for (int i = 3; i < argc; ++i) {
    if (i > 3) cmd += L' ';
    cmd += L'"';
    cmd += argv[i];
    cmd += L'"';
  }
  {
    std::wstring exe = argv[3];
    size_t p = exe.find_last_of(L"/\\");
    std::wstring b = p == std::wstring::npos ? exe : exe.substr(p + 1);
    size_t d = b.find_last_of(L'.');
    if (d != std::wstring::npos) b = b.substr(0, d);
    g_exe_module = quote(b);
  }

  timeBeginPeriod(1);
  STARTUPINFOW si{};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi{};
  std::vector<wchar_t> cmdbuf(cmd.begin(), cmd.end());
  cmdbuf.push_back(0);
  double t_start = now();
  if (!CreateProcessW(nullptr, cmdbuf.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
    std::fprintf(stderr, "CreateProcess failed (%lu)\n", GetLastError());
    return 1;
  }
  g_proc = pi.hProcess;
  Sleep(300);
  SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
  if (!SymInitialize(g_proc, nullptr, TRUE)) {
    std::fprintf(stderr, "SymInitialize failed (%lu)\n", GetLastError());
  }

  struct Worker {
    HANDLE h;
    double cpu = 0.0;
  };
  std::map<DWORD, Worker> workers;
  double main_cpu = 0.0;
  auto refresh_threads = [&] {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te{};
    te.dwSize = sizeof te;
    if (Thread32First(snap, &te)) {
      do {
        if (te.th32OwnerProcessID != pi.dwProcessId || te.th32ThreadID == pi.dwThreadId) continue;
        if (workers.count(te.th32ThreadID)) continue;
        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE,
                              te.th32ThreadID);
        if (h) workers[te.th32ThreadID] = Worker{h};
      } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    FILETIME c, e, k, u;
    for (auto& kv : workers)
      if (GetThreadTimes(kv.second.h, &c, &e, &k, &u)) kv.second.cpu = ft_seconds(k) + ft_seconds(u);
    if (GetThreadTimes(pi.hThread, &c, &e, &k, &u)) main_cpu = ft_seconds(k) + ft_seconds(u);
  };

  std::unordered_map<int, double> self, incl, worker_run;
  std::map<std::tuple<int, int, int>, double> self_line;  // fn, file, line
  double main_total = 0.0, main_wait = 0.0, worker_total = 0.0, worker_blocked = 0.0;
  long long main_samples = 0, worker_samples = 0, main_failed = 0;
  std::vector<DWORD64> pcs;
  std::vector<int> seen;
  std::vector<Frame> logical;
  double last_main = now(), last_worker = last_main;
  const int worker_every = 8;
  long long tick = 0;

  while (WaitForSingleObject(g_proc, static_cast<DWORD>(interval_ms)) == WAIT_TIMEOUT) {
    if (tick % 500 == 0) {
      refresh_threads();
      if (tick <= 5000) SymRefreshModuleList(g_proc);
    }
    ++tick;

    // Main thread: full stack, weighted by the wall time since the last sample.
    if (walk(pi.hThread, 64, pcs)) {
      double t = now();
      double w = t - last_main;
      last_main = t;
      main_total += w;
      ++main_samples;
      logical.clear();
      for (size_t i = 0; i < pcs.size(); ++i) {
        const std::vector<Frame>& fr = resolve(pcs[i], i > 0);
        logical.insert(logical.end(), fr.begin(), fr.end());
      }
      // Self time: the innermost frame; if it is outside the exe (CRT math,
      // memcpy, a wait), say who called it.
      const Frame& leaf = logical.front();
      std::string key = g_names[leaf.fn];
      bool waiting = is_wait(key);
      if (waiting) main_wait += w;
      if (!leaf.in_exe) {
        for (const Frame& f : logical)
          if (f.in_exe) { key += "  <-  " + g_names[f.fn]; break; }
      }
      self[name_id(key)] += w;
      // Hot lines: the innermost frame that is in the exe.
      for (const Frame& f : logical)
        if (f.in_exe) {
          self_line[{f.fn, f.file, f.line}] += w;
          break;
        }
      seen.clear();
      for (const Frame& f : logical) seen.push_back(f.fn);
      std::sort(seen.begin(), seen.end());
      seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
      for (int id : seen) incl[id] += w;
      int node = 0;
      g_tree[0].t += w;
      for (size_t i = logical.size(); i-- > 0;) {
        int fn = logical[i].fn;
        auto it = g_tree[node].kids.find(fn);
        int child;
        if (it == g_tree[node].kids.end()) {
          child = static_cast<int>(g_tree.size());
          g_tree[node].kids[fn] = child;
          g_tree.emplace_back();
        } else {
          child = it->second;
        }
        g_tree[child].t += w;
        node = child;
      }
    } else {
      ++main_failed;
    }

    // Other threads, less often: running or blocked, and in what.
    if (tick % worker_every == 0) {
      double t = now();
      double w = t - last_worker;
      last_worker = t;
      for (auto& kv : workers) {
        if (!walk(kv.second.h, 6, pcs)) continue;
        ++worker_samples;
        worker_total += w;
        logical.clear();
        for (size_t i = 0; i < pcs.size(); ++i) {
          const std::vector<Frame>& fr = resolve(pcs[i], i > 0);
          logical.insert(logical.end(), fr.begin(), fr.end());
        }
        std::string key = g_names[logical.front().fn];
        if (is_wait(key)) {
          worker_blocked += w;
          continue;
        }
        if (!logical.front().in_exe) {
          for (const Frame& f : logical)
            if (f.in_exe) { key += "  <-  " + g_names[f.fn]; break; }
        }
        worker_run[name_id(key)] += w;
      }
    }
  }
  double wall = now() - t_start;
  refresh_threads();
  FILETIME c, e, k, u;
  double proc_cpu = 0.0, proc_kernel = 0.0;
  if (GetProcessTimes(g_proc, &c, &e, &k, &u)) {
    proc_kernel = ft_seconds(k);
    proc_cpu = proc_kernel + ft_seconds(u);
  }
  DWORD code = 0;
  GetExitCodeProcess(g_proc, &code);

  FILE* f = _wfopen(report.c_str(), L"w");
  if (!f) {
    std::fprintf(stderr, "cannot write report\n");
    return 1;
  }
  std::fprintf(f, "command: %s\nexit code: %lu\n", quote(cmd).c_str(), code);
  std::fprintf(f, "wall: %.1f s   process CPU: %.1f s (kernel %.1f s)   average busy cores: %.2f\n", wall, proc_cpu,
               proc_kernel, proc_cpu / wall);
  double workers_cpu = 0.0;
  for (auto& kv : workers) workers_cpu += kv.second.cpu;
  std::fprintf(f, "main thread CPU (last reading): %.1f s   other threads CPU: %.1f s over %zu threads\n", main_cpu,
               workers_cpu, workers.size());
  std::fprintf(f, "main thread samples: %lld (failed %lld), mean interval %.2f ms, sampled %.1f s, blocked %.1f s\n",
               main_samples, main_failed, main_samples ? 1000.0 * main_total / main_samples : 0.0, main_total,
               main_wait);
  std::fprintf(f, "other-thread samples: %lld; sampled thread-seconds %.1f, of which blocked %.1f (%.1f%%)\n\n",
               worker_samples, worker_total, worker_blocked,
               worker_total > 0 ? 100.0 * worker_blocked / worker_total : 0.0);

  std::fprintf(f, "== MAIN THREAD, CALL TREE (wall time, inclusive; nodes under 0.4%% hidden) ==\n");
  print_tree(f, 0, 0, main_total, 0.004);

  auto dump = [&](const char* title, const std::unordered_map<int, double>& m, double total, int top) {
    std::vector<std::pair<double, int>> v;
    for (auto& kv : m) v.push_back({kv.second, kv.first});
    std::sort(v.rbegin(), v.rend());
    std::fprintf(f, "\n== %s ==\n", title);
    for (int i = 0; i < top && i < static_cast<int>(v.size()); ++i)
      std::fprintf(f, "%6.2f%% %8.2fs  %s\n", 100.0 * v[i].first / total, v[i].first, g_names[v[i].second].c_str());
  };
  dump("MAIN THREAD, SELF TIME BY FUNCTION (innermost frame, inlined functions separate)", self, main_total, 60);
  dump("MAIN THREAD, INCLUSIVE TIME BY FUNCTION", incl, main_total, 70);

  {
    std::vector<std::pair<double, std::tuple<int, int, int>>> v;
    for (auto& kv : self_line) v.push_back({kv.second, kv.first});
    std::sort(v.rbegin(), v.rend());
    std::fprintf(f, "\n== MAIN THREAD, HOT LINES (innermost frame inside the exe) ==\n");
    for (int i = 0; i < 90 && i < static_cast<int>(v.size()); ++i) {
      auto [fn, file, line] = v[i].second;
      std::fprintf(f, "%6.2f%% %8.2fs  %s:%d  %s\n", 100.0 * v[i].first / main_total, v[i].first,
                   file >= 0 ? g_names[file].c_str() : "?", line, g_names[fn].c_str());
    }
  }
  if (worker_total > 0.0)
    dump("OTHER THREADS, RUNNING TIME BY FUNCTION (share of all sampled thread time)", worker_run, worker_total, 30);
  std::fclose(f);
  std::printf("wall %.1f s, cpu %.1f s, %lld samples\n", wall, proc_cpu, main_samples);
  return static_cast<int>(code);
}
