#pragma once

#include <atomic>

namespace mir2 {

class ShutdownToken {
 public:
  void request_stop() { stopped_.store(true, std::memory_order_relaxed); }
  [[nodiscard]] bool stop_requested() const { return stopped_.load(std::memory_order_relaxed); }

 private:
  std::atomic_bool stopped_{false};
};

}  // namespace mir2
