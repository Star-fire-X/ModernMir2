#include "services/log_service.hpp"

#include <filesystem>

namespace mir2 {

void LogService::start(HostContext& context) {
  context_ = &context;
  endpoint_ = context.bus->register_endpoint(name(), context.config.runtime.default_queue_capacity);
  std::filesystem::create_directories(context.root_dir / context.config.runtime.log_dir);
  audit_log_.open(context.root_dir / context.config.runtime.log_dir / "audit.log",
                  std::ios::binary | std::ios::app);
  running_.store(true, std::memory_order_relaxed);
  worker_ = std::thread([this] { run(); });
}

void LogService::stop() { running_.store(false, std::memory_order_relaxed); }

void LogService::join() {
  if (worker_.joinable()) {
    worker_.join();
  }
}

std::unordered_map<std::string, std::string> LogService::snapshot() const {
  return {{"running", running_.load(std::memory_order_relaxed) ? "true" : "false"},
          {"written_lines", std::to_string(written_lines_)}};
}

void LogService::run() {
  while (running_.load(std::memory_order_relaxed)) {
    auto message = endpoint_->queue->wait_pop_for(std::chrono::milliseconds(100));
    if (!message.has_value()) {
      continue;
    }
    if (auto audit = std::get_if<AuditEvent>(&*message)) {
      audit_log_ << audit->category << '\t' << audit->session_key << '\t' << audit->message << '\n';
      audit_log_.flush();
      ++written_lines_;
    }
  }
}

}  // namespace mir2
