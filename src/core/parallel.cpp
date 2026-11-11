#include "core/parallel.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace bs {
namespace {

thread_local bool t_in_parallel = false;

class Pool {
 public:
  Pool() {
    unsigned hw = std::thread::hardware_concurrency();
    int n = static_cast<int>(hw > 1 ? hw - 1 : 0);
    for (int i = 0; i < n; ++i) workers_.emplace_back([this] { worker(); });
  }
  ~Pool() {
    {
      std::lock_guard<std::mutex> lk(m_);
      stop_ = true;
    }
    cv_job_.notify_all();
    for (auto& t : workers_) t.join();
  }
  int threads() const { return static_cast<int>(workers_.size()) + 1; }

  // Only as many workers as there are chunks to share are woken, and the
  // caller waits only for workers that actually joined: one that wakes after
  // the job is finished finds no seat left and goes back to sleep.
  void run(int begin, int end, int chunk, const std::function<void(int, int)>& fn) {
    std::lock_guard<std::mutex> serial(run_m_);  // one job at a time
    const int chunks = (end - begin + chunk - 1) / chunk;
    const int helpers = std::min(static_cast<int>(workers_.size()), chunks - 1);
    {
      std::lock_guard<std::mutex> lk(m_);
      fn_ = &fn; end_ = end; chunk_ = chunk;
      next_.store(begin);
      seats_ = helpers;
    }
    if (helpers >= static_cast<int>(workers_.size())) cv_job_.notify_all();
    else for (int i = 0; i < helpers; ++i) cv_job_.notify_one();
    drain();
    std::unique_lock<std::mutex> lk(m_);
    seats_ = 0;
    cv_done_.wait(lk, [this] { return busy_ == 0; });
    fn_ = nullptr;
  }

 private:
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
    std::unique_lock<std::mutex> lk(m_);
    for (;;) {
      cv_job_.wait(lk, [this] { return stop_ || seats_ > 0; });
      if (stop_) return;
      --seats_;
      ++busy_;
      lk.unlock();
      drain();
      lk.lock();
      if (--busy_ == 0) cv_done_.notify_one();
    }
  }

  std::vector<std::thread> workers_;
  std::mutex m_, run_m_;
  std::condition_variable cv_job_, cv_done_;
  const std::function<void(int, int)>* fn_ = nullptr;
  std::atomic<int> next_{0};
  int end_ = 0, chunk_ = 1;
  int seats_ = 0;  // workers that may still join the current job
  int busy_ = 0;   // workers currently running chunks
  bool stop_ = false;
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
