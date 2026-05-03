#pragma once

#include <atomic>
#include <fstream>
#include <memory>
#include <thread>
#include <unordered_map>

#include "core/module.hpp"

namespace mir2 {

class LogService : public Module {
 public:
  LogService() = default;
  ~LogService() override {
    stop();
    join();
  }

  [[nodiscard]] std::string name() const override { return "log_service"; }
  void start(HostContext& context) override;
  void stop() override;
  void join() override;
  [[nodiscard]] std::unordered_map<std::string, std::string> snapshot() const override;

 private:
  void run();

  HostContext* context_{nullptr};
  std::shared_ptr<LocalBus::Endpoint> endpoint_{};
  std::thread worker_{};
  std::ofstream audit_log_{};
  std::atomic_bool running_{false};
  std::size_t written_lines_{0};
};

}  // namespace mir2
