/**
 * @file persistence_service.hpp
 * @brief 持久化服务头文件
 *
 * @details 定义 PersistenceService 类，负责处理所有数据库读写操作。
 *          作为异步服务运行在独立线程中，通过消息总线接收 PersistRequest
 *          请求，处理完成后将 PersistResult 发送回请求方。
 *
 * @note 持久化服务是系统中唯一直接访问 SQLite 数据库的模块，
 *       其他服务通过异步消息与其通信，避免了数据库锁竞争。
 */

#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <unordered_map>

#include "core/module.hpp"
#include "storage/repository.hpp"

namespace mir2 {

/**
 * @class PersistenceService
 * @brief 持久化服务模块
 *
 * @details 提供异步的数据库访问接口，支持以下操作类型：
 *          - 账号管理：加载、认证、创建、更新、修改密码
 *          - 角色管理：加载(按ID或名称)、列表、创建、删除、保存
 *          - 公会与城堡：加载/保存公会快照、保存城堡状态
 *          - 商家状态：加载/保存商家状态
 *          - 审计记录：记录审计事件
 *          - 运行时初始化：建表、填充运行数据
 *
 *          所有请求通过 request_id 实现异步响应匹配。
 *          异常时返回 PersistResultKind::error 类型的结果。
 */
class PersistenceService : public Module {
 public:
  PersistenceService() = default;

  /**
   * @brief 析构函数，自动停止服务并等待线程结束
   */
  ~PersistenceService() override {
    stop();
    join();
  }

  /**
   * @brief 获取模块名称
   * @return "persistence_service"
   */
  [[nodiscard]] std::string name() const override { return "persistence_service"; }

  /**
   * @brief 启动持久化服务
   * @param context 宿主上下文
   */
  void start(HostContext& context) override;

  /**
   * @brief 停止持久化服务
   */
  void stop() override;

  /**
   * @brief 等待工作线程结束
   */
  void join() override;

  /**
   * @brief 获取服务快照
   * @return 包含运行状态、处理请求数等信息的键值对映射
   */
  [[nodiscard]] std::unordered_map<std::string, std::string> snapshot() const override;

 private:
  /**
   * @brief 工作线程主循环
   */
  void run();

  /**
   * @brief 处理单个持久化请求
   * @param request 持久化请求
   */
  void handle_request(const PersistRequest& request);

  HostContext* context_{nullptr};                    ///< 宿主上下文指针
  std::shared_ptr<LocalBus::Endpoint> endpoint_{};   ///< 消息总线端点
  std::unique_ptr<Repository> repository_{};         ///< 数据仓库(SQLite 数据库访问对象)
  std::thread worker_{};                             ///< 工作线程
  std::atomic_bool running_{false};                  ///< 运行状态标志
  std::size_t handled_requests_{0};                  ///< 已处理的请求计数
  PersistRequestKind last_request_kind_{PersistRequestKind::ensure_schema}; ///< 最后处理的请求类型
  std::string last_request_reply_to_{};              ///< 最后处理的请求的回复目标
  std::string last_request_id_{};                    ///< 最后处理的请求的ID
};

}  // namespace mir2
