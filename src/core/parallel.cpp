#include "core/parallel.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include <immintrin.h>

namespace bs {
namespace {

thread_local bool t_in_parallel = false;

// Jobs often arrive in quick succession (the watercolor step issues three per
// time step), and putting a thread to sleep and waking it again costs more
// than many of those jobs. So an idle worker first spins for a short while,
// watching for the next job, and only then sleeps; the caller likewise spins
// while the last workers finish. Joining a job is a single atomic update of
// `state_`, which holds the number of seats still open and the number of
// workers running chunks.
class Pool {
 public:
  Pool() {
    unsigned hw = std::thread::hardware_concurrency();
    int n = static_cast<int>(hw > 1 ? hw - 1 : 0);
    for (int i = 0; i < n; ++i) workers_.emplace_back([this] { worker(); });
  }
  ~Pool() {
    stop_.store(true);
    { std::lock_guard<std::mutex> lk(m_); }
    cv_job_.notify_all();
    for (auto& t : workers_) t.join();
  }
  int threads() const { return static_cast<int>(workers_.size()) + 1; }

  void run(int begin, int end, int chunk, const std::function<void(int, int)>& fn) {
    std::lock_guard<std::mutex> serial(run_m_);  // one job at a time
    const int chunks = (end - begin + chunk - 1) / chunk;
    const int workers = static_cast<int>(workers_.size());
    const int helpers = std::min(workers, chunks - 1);
    fn_ = &fn; end_ = end; chunk_ = chunk;
    next_.store(begin);
    state_.store(static_cast<uint32_t>(helpers));  // opens the seats; nobody is busy yet
    // Spinning workers take seats by themselves. Wake sleepers only for the
    // seats the spinning ones cannot fill.
    const int sleepers = sleepers_.load();
    const int wake = helpers - (workers - sleepers);
    if (wake > 0) {
      { std::lock_guard<std::mutex> lk(m_); }  // a worker about to sleep has checked for seats by now
      if (wake >= sleepers) cv_job_.notify_all();
      else for (int i = 0; i < wake; ++i) cv_job_.notify_one();
    }
    drain();
    state_.fetch_and(~kSeatMask);  // no more joining; late wakers find no seat
    for (int spins = 0; state_.load() != 0; ++spins) {
      if (spins < 20000) _mm_pause();
      else std::this_thread::yield();
    }
    fn_ = nullptr;
  }

 private:
  static constexpr uint32_t kSeatMask = 0xFFFFu;  // low bits of state_: seats open
  static constexpr uint32_t kBusyOne = 0x10000u;  // high bits: workers running chunks

  void drain() {
    t_in_parallel = true;
    for (;;) {
      int lo = next_.fetch_add(chunk_);
      if (lo >= end_) break;
      (*fn_)(lo, std::min(lo + chunk_, end_));
    }
    t_in_parallel = false;
  }
  void worker() {
    using Clock = std::chrono::steady_clock;
    const auto spin_for = std::chrono::microseconds(100);
    Clock::time_point idle_since = Clock::now();
    int polls = 0;
    for (;;) {
      uint32_t s = state_.load();
      if ((s & kSeatMask) != 0) {
        if (state_.compare_exchange_weak(s, s - 1 + kBusyOne)) {
          drain();
          state_.fetch_sub(kBusyOne);
          idle_since = Clock::now();
          polls = 0;
        }
        continue;
      }
      if (stop_.load()) return;
      if ((++polls & 31) != 0 || Clock::now() - idle_since < spin_for) {
        _mm_pause();
        continue;
      }
      std::unique_lock<std::mutex> lk(m_);
      sleepers_.fetch_add(1);
      cv_job_.wait(lk, [this] { return stop_.load() || (state_.load() & kSeatMask) != 0; });
      sleepers_.fetch_sub(1);
      lk.unlock();
      idle_since = Clock::now();
      polls = 0;
    }
  }

  std::vector<std::thread> workers_;
  std::mutex m_, run_m_;
  std::condition_variable cv_job_;
  const std::function<void(int, int)>* fn_ = nullptr;
  std::atomic<int> next_{0};
  int end_ = 0, chunk_ = 1;
  std::atomic<uint32_t> state_{0};
  std::atomic<int> sleepers_{0};
  std::atomic<bool> stop_{false};
};

Pool& pool() {
  static Pool p;
  return p;
}

}  // namespace

int parallel_threads() { return pool().threads(); }

void parallel_for(int begin, int end, const std::function<void(int, int)>& fn, int min_chunk) {
  if (end <= begin) return;
  int n = end - begin;
  int threads = pool().threads();
  if (t_in_parallel || threads == 1 || n <= min_chunk) {
    fn(begin, end);
    return;
  }
  int chunk = std::max(min_chunk, n / (threads * 4));
  if (chunk < 1) chunk = 1;
  pool().run(begin, end, chunk, fn);
}

}  // namespace bs
