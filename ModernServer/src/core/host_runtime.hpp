#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "config/models.hpp"
#include "core/local_bus.hpp"
#include "core/metrics_registry.hpp"
#include "core/module.hpp"
#include "core/shutdown_token.hpp"

namespace mir2 {

class HostRuntime {
 public:
  HostRuntime(std::filesystem::path root_dir, HostConfig config,
              std::shared_ptr<spdlog::logger> logger);
  ~HostRuntime();

  void register_module(std::unique_ptr<Module> module);
  void start_all();
  void stop_all();
  void join_all();
  void write_status_snapshot() const;

  [[nodiscard]] HostContext& context() { return context_; }
  [[nodiscard]] const HostContext& context() const { return context_; }

 private:
  std::filesystem::path root_dir_{};
  LocalBus bus_{};
  MetricsRegistry metrics_{};
  ShutdownToken shutdown_{};
  HostContext context_{};
  std::vector<std::unique_ptr<Module>> modules_{};
};

}  // namespace mir2
