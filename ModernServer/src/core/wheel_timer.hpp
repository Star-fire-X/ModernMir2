#pragma once

#include <cstdint>
#include <list>
#include <utility>
#include <vector>

namespace mir2 {

template <typename T>
class WheelTimer {
 public:
  explicit WheelTimer(std::size_t slots = 512) : slots_(slots), buckets_(slots) {}

  void schedule(std::uint64_t current_tick, std::uint64_t delay_ticks, T value) {
    const auto due_tick = current_tick + delay_ticks;
    buckets_[due_tick % slots_].push_back(Node{due_tick, std::move(value)});
  }

  [[nodiscard]] std::vector<T> pop_ready(std::uint64_t current_tick) {
    std::vector<T> ready;
    auto& bucket = buckets_[current_tick % slots_];
    for (auto it = bucket.begin(); it != bucket.end();) {
      if (it->due_tick <= current_tick) {
        ready.push_back(std::move(it->value));
        it = bucket.erase(it);
      } else {
        ++it;
      }
    }
    return ready;
  }

 private:
  struct Node {
    std::uint64_t due_tick{0};
    T value{};
  };

  std::size_t slots_{0};
  std::vector<std::list<Node>> buckets_{};
};

}  // namespace mir2
