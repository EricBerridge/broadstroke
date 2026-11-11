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

  void run(int begin, int end, int chunk, const std::function<void(int, int)>& fn) {
    std::lock_guard<std::mutex> serial(run_m_);  // one job at a time
    {
      std::lock_guard<std::mutex> lk(m_);
      fn_ = &fn; end_ = end; chunk_ = chunk;
      next_.store(begin);
      pending_ = static_cast<int>(workers_.size());
      ++generation_;
    }
    cv_job_.notify_all();
    drain();
    std::unique_lock<std::mutex> lk(m_);
    cv_done_.wait(lk, [this] { return pending_ == 0; });
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
    uint64_t seen = 0;
    for (;;) {
      {
        std::unique_lock<std::mutex> lk(m_);
        cv_job_.wait(lk, [&] { return stop_ || generation_ != seen; });
        if (stop_) return;
        seen = generation_;
      }
      drain();
      {
        std::lock_guard<std::mutex> lk(m_);
        --pending_;
      }
      cv_done_.notify_one();
    }
  }

  std::vector<std::thread> workers_;
  std::mutex m_, run_m_;
  std::condition_variable cv_job_, cv_done_;
  const std::function<void(int, int)>* fn_ = nullptr;
  std::atomic<int> next_{0};
  int end_ = 0, chunk_ = 1, pending_ = 0;
  uint64_t generation_ = 0;
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
