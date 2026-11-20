#include "worker.h"

#include <utility>

namespace spark {

Worker::Worker(std::function<void()> wake, int threads)
    : wake_(std::move(wake)) {
  if (threads < 1) threads = 1;
  threads_.reserve(static_cast<std::size_t>(threads));
  for (int i = 0; i < threads; ++i) {
    threads_.emplace_back([this] { Run(); });
  }
}

Worker::~Worker() { Stop(); }

void Worker::Submit(std::function<void()> job) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stop_.load(std::memory_order_relaxed)) return;
    jobs_.push_back(std::move(job));
  }
  cv_.notify_one();
}

void Worker::Post(std::function<void()> fn) {
  {
    std::lock_guard<std::mutex> lock(doneMutex_);
    done_.push_back(std::move(fn));
  }
  if (wake_) wake_();
}

void Worker::Drain() {
  std::vector<std::function<void()>> pending;
  {
    std::lock_guard<std::mutex> lock(doneMutex_);
    pending.swap(done_);
  }
  for (auto& fn : pending) {
    if (fn) fn();
  }
}

void Worker::Stop() {
  if (stop_.exchange(true, std::memory_order_relaxed)) return;
  cv_.notify_all();
  for (auto& thread : threads_) {
    if (thread.joinable()) thread.join();
  }
  threads_.clear();
}

void Worker::Run() {
  for (;;) {
    std::function<void()> job;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this] {
        return stop_.load(std::memory_order_relaxed) || !jobs_.empty();
      });
      if (stop_.load(std::memory_order_relaxed) && jobs_.empty()) return;
      job = std::move(jobs_.front());
      jobs_.pop_front();
    }
    if (job) job();
  }
}

}  // namespace spark
