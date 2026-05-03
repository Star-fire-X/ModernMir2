#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

#include "config/models.hpp"
#include "core/local_bus.hpp"
#include "core/metrics_registry.hpp"
#include "core/shutdown_token.hpp"
#include "spdlog/spdlog.h"

namespace mir2 {

struct HostContext {
  std::filesystem::path root_dir{};
  HostConfig config{};
  LocalBus* bus{nullptr};
  MetricsRegistry* metrics{nullptr};
  ShutdownToken* shutdown{nullptr};
  std::shared_ptr<spdlog::logger> logger{};
};

class Module {
 public:
  virtual ~Module() = default;

  [[nodiscard]] virtual std::string name() const = 0;
  virtual void start(HostContext& context) = 0;
  virtual void stop() = 0;
  virtual void join() = 0;
  [[nodiscard]] virtual std::unordered_map<std::string, std::string> snapshot() const = 0;
};

}  // namespace mir2
