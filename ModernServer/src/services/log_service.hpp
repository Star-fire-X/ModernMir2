/**
 * @file log_service.hpp
 * @brief 日志服务头文件
 *
 * @details 定义 LogService 类，提供集中式的审计日志记录功能。
 *          通过消息总线接收 AuditEvent 事件，将审计信息写入文件。
 *
 * @note 日志以制表符分隔的格式写入，包含类别、会话键和消息三个字段。
 *       文件采用追加写入模式，每次写入后立即刷新。
 */

#pragma once

#include <atomic>
#include <fstream>
#include <memory>
#include <thread>
#include <unordered_map>

#include "core/module.hpp"

namespace mir2 {

/**
 * @class LogService
 * @brief 日志服务模块
 *
 * @details 在独立线程中运行，监听消息总线上的 AuditEvent，
 *          将审计日志写入到配置指定的日志目录下的 audit.log 文件。
 *
 *          日志格式：每行由制表符分隔的三字段组成：
 *          <类别>\t<会话键>\t<消息>\n
 *
 *          @warning 该服务仅在文件写入模式下工作，不支持控制台输出。
 *                   日志目录在启动时自动创建。
 */
class LogService : public Module {
 public:
  LogService() = default;

  /**
   * @brief 析构函数，自动停止服务并等待线程结束
   */
  ~LogService() override {
    stop();
    join();
  }

  /**
   * @brief 获取模块名称
   * @return "log_service"
   */
  [[nodiscard]] std::string name() const override { return "log_service"; }

  /**
   * @brief 启动日志服务
   * @param context 宿主上下文，包含日志目录配置
   */
  void start(HostContext& context) override;

  /**
   * @brief 停止日志服务
   */
  void stop() override;

  /**
   * @brief 等待工作线程结束
   */
  void join() override;

  /**
   * @brief 获取服务快照
   * @return 包含运行状态和写入行数的键值对映射
   */
  [[nodiscard]] std::unordered_map<std::string, std::string> snapshot() const override;

 private:
  /**
   * @brief 工作线程主循环
   */
  void run();

  HostContext* context_{nullptr};                    ///< 宿主上下文指针
  std::shared_ptr<LocalBus::Endpoint> endpoint_{};   ///< 消息总线端点
  std::thread worker_{};                             ///< 工作线程
  std::ofstream audit_log_{};                        ///< 审计日志文件输出流
  std::atomic_bool running_{false};                  ///< 运行状态标志
  std::size_t written_lines_{0};                     ///< 已写入的行数计数
};

}  // namespace mir2
