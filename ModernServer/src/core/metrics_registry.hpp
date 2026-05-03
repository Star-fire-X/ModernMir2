#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace mir2 {

class MetricsRegistry {
 public:
  void increment_counter(const std::string& name, std::int64_t delta = 1);
  void set_gauge(const std::string& name, std::int64_t value);
  [[nodiscard]] std::unordered_map<std::string, std::int64_t> snapshot() const;

 private:
  mutable std::mutex mutex_{};
  std::unordered_map<std::string, std::int64_t> counters_{};
  std::unordered_map<std::string, std::int64_t> gauges_{};
};

inline void MetricsRegistry::increment_counter(const std::string& name, std::int64_t delta) {
  std::scoped_lock lock(mutex_);
  counters_[name] += delta;
}

inline void MetricsRegistry::set_gauge(const std::string& name, std::int64_t value) {
  std::scoped_lock lock(mutex_);
  gauges_[name] = value;
}

inline std::unordered_map<std::string, std::int64_t> MetricsRegistry::snapshot() const {
  std::scoped_lock lock(mutex_);
  std::unordered_map<std::string, std::int64_t> combined = counters_;
  for (const auto& [key, value] : gauges_) {
    combined[key] = value;
  }
  return combined;
}

}  // namespace mir2
