#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace spark {

// A small fixed-size thread pool. Jobs run on worker threads; their results
// are handed back to the UI thread through Post() + Drain().
class Worker {
 public:
  explicit Worker(std::function<void()> wake, int threads = 4);
  ~Worker();

  Worker(const Worker&) = delete;
  Worker& operator=(const Worker&) = delete;

  void Submit(std::function<void()> job);
  void Post(std::function<void()> fn);  // queued for the UI thread
  void Drain();                         // must be called from the UI thread
  void Stop();
  bool Stopping() const { return stop_.load(std::memory_order_relaxed); }

 private:
  void Run();

  std::function<void()> wake_;
  std::vector<std::thread> threads_;

  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> jobs_;

  std::mutex doneMutex_;
  std::vector<std::function<void()>> done_;

  std::atomic<bool> stop_{false};
};

}  // namespace spark
