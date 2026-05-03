#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>

namespace mir2 {

template <typename T>
class BoundedMpscQueue {
 public:
  explicit BoundedMpscQueue(std::size_t capacity) : capacity_(capacity) {}

  bool try_push(T value) {
    {
      std::scoped_lock lock(mutex_);
      if (queue_.size() >= capacity_) {
        return false;
      }
      queue_.push_back(std::move(value));
    }
    signal_.notify_one();
    return true;
  }

  std::optional<T> wait_pop_for(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    if (!signal_.wait_for(lock, timeout, [this] { return closed_ || !queue_.empty(); })) {
      return std::nullopt;
    }
    if (queue_.empty()) {
      return std::nullopt;
    }
    T value = std::move(queue_.front());
    queue_.pop_front();
    return value;
  }

  std::optional<T> try_pop() {
    std::scoped_lock lock(mutex_);
    if (queue_.empty()) {
      return std::nullopt;
    }
    T value = std::move(queue_.front());
    queue_.pop_front();
    return value;
  }

  void close() {
    {
      std::scoped_lock lock(mutex_);
      closed_ = true;
    }
    signal_.notify_all();
  }

  [[nodiscard]] std::size_t size() const {
    std::scoped_lock lock(mutex_);
    return queue_.size();
  }

  [[nodiscard]] std::size_t capacity() const { return capacity_; }

 private:
  std::size_t capacity_{0};
  mutable std::mutex mutex_{};
  std::condition_variable signal_{};
  std::deque<T> queue_{};
  bool closed_{false};
};

}  // namespace mir2
