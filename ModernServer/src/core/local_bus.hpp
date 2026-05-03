#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "core/bounded_mpsc_queue.hpp"
#include "core/messages.hpp"

namespace mir2 {

class LocalBus {
 public:
  struct Endpoint {
    std::string name{};
    std::shared_ptr<BoundedMpscQueue<BusMessage>> queue{};
  };

  std::shared_ptr<Endpoint> register_endpoint(const std::string& name, std::size_t capacity);
  bool post(const std::string& target, BusMessage message);
  [[nodiscard]] std::size_t queue_depth(const std::string& target) const;
  [[nodiscard]] std::unordered_map<std::string, std::size_t> queue_depths() const;
  void close_all();

 private:
  mutable std::mutex mutex_{};
  std::unordered_map<std::string, std::shared_ptr<Endpoint>> endpoints_{};
};

inline std::shared_ptr<LocalBus::Endpoint> LocalBus::register_endpoint(const std::string& name,
                                                                       std::size_t capacity) {
  std::scoped_lock lock(mutex_);
  auto endpoint = std::make_shared<Endpoint>();
  endpoint->name = name;
  endpoint->queue = std::make_shared<BoundedMpscQueue<BusMessage>>(capacity);
  endpoints_[name] = endpoint;
  return endpoint;
}

inline bool LocalBus::post(const std::string& target, BusMessage message) {
  std::shared_ptr<Endpoint> endpoint;
  {
    std::scoped_lock lock(mutex_);
    auto it = endpoints_.find(target);
    if (it == endpoints_.end()) {
      return false;
    }
    endpoint = it->second;
  }
  return endpoint->queue->try_push(std::move(message));
}

inline std::size_t LocalBus::queue_depth(const std::string& target) const {
  std::scoped_lock lock(mutex_);
  auto it = endpoints_.find(target);
  if (it == endpoints_.end()) {
    return 0;
  }
  return it->second->queue->size();
}

inline std::unordered_map<std::string, std::size_t> LocalBus::queue_depths() const {
  std::unordered_map<std::string, std::size_t> result;
  std::scoped_lock lock(mutex_);
  for (const auto& [name, endpoint] : endpoints_) {
    result[name] = endpoint->queue->size();
  }
  return result;
}

inline void LocalBus::close_all() {
  std::scoped_lock lock(mutex_);
  for (const auto& [_, endpoint] : endpoints_) {
    endpoint->queue->close();
  }
}

}  // namespace mir2
