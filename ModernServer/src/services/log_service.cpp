/**
 * @file log_service.cpp
 * @brief 日志服务实现
 *
 * @details 实现 LogService 类，包括日志目录创建、文件打开、
 *          消息循环处理和审计事件写入等功能。
 */

#include "services/log_service.hpp"

#include <filesystem>

namespace mir2 {

/**
 * @brief 启动日志服务
 *
 * @details 创建日志目录(如果不存在)，以追加模式打开 audit.log 文件，
 *          然后启动工作线程监听审计事件。
 *
 * @param context 宿主上下文
 */
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

/**
 * @brief 工作线程主循环
 *
 * @details 从消息队列获取 AuditEvent，以制表符分隔的格式
 *          (类别\t会话键\t消息)写入日志文件并立即刷新。
 *
 *          使用 100ms 超时的等待弹出，避免忙等待消耗 CPU。
 */
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
