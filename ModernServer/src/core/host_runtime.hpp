/**
 * @file host_runtime.hpp
 * @brief 主机运行时环境声明
 *
 * @details 该文件定义了 HostRuntime 类，它是整个游戏服务器的运行时容器。
 *          HostRuntime 负责管理所有服务模块的生命周期（注册、启动、停止、等待退出），
 *          维护运行时上下文（配置、消息总线、度量注册表、关闭令牌），
 *          并提供状态快照导出功能用于监控和调试。
 *
 * 设计模式：采用"容器模式"，将模块生命周期管理与运行时上下文分离，
 *          各模块通过 HostContext 访问共享资源，降低模块间的直接耦合。
 *
 * @see HostContext
 * @see Module
 * @see LocalBus
 * @see MetricsRegistry
 * @see ShutdownToken
 */

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

/**
 * @brief 主机运行时环境——服务器模块的容器与生命周期管理器
 *
 * @details HostRuntime 是整个游戏服务器的核心容器，负责：
 *
 * 1. 模块生命周期管理：
 *    - register_module()   - 注册模块到运行时
 *    - start_all()         - 启动所有已注册模块
 *    - stop_all()          - 停止所有模块（逆序，PersistenceService 特殊处理）
 *    - join_all()          - 等待所有模块线程退出
 *
 * 2. 运行时上下文管理：
 *    - HostContext 聚合了所有模块需要的共享资源指针
 *    - 包括 LocalBus（通信）、MetricsRegistry（度量）、ShutdownToken（关闭信号）
 *
 * 3. 状态监控：
 *    - write_status_snapshot() - 将运行时状态导出为 JSON 格式文件
 *    - 包含队列深度、度量数据、模块状态等信息
 *
 * 资源所有权：
 * - HostRuntime 拥有所有注册的 Module 对象（通过 unique_ptr）
 * - 内部组合了 LocalBus、MetricsRegistry、ShutdownToken 等核心组件
 * - 根目录路径（root_dir）的所有权由 HostRuntime 管理
 *
 * @see Module 基类接口定义
 * @see HostContext 运行时上下文结构体
 * @see register_default_modules() 默认模块注册函数
 */
class HostRuntime {
 public:
  /**
   * @brief 构造函数
   * @param root_dir 服务器根目录路径，用于存储配置文件和数据文件
   * @param config 主机配置，包含运行时参数（网关启用、网络设置等）
   * @param logger spdlog 日志记录器实例的共享指针
   *
   * @details 初始化时设置运行时上下文，包括根目录路径、配置、消息总线指针、
   *          度量注册表指针、关闭令牌指针和日志记录器。这些上下文信息
   *          将在模块启动时传递给各模块。
   */
  HostRuntime(std::filesystem::path root_dir, HostConfig config,
              std::shared_ptr<spdlog::logger> logger);

  /**
   * @brief 析构函数
   *
   * @details 自动调用 stop_all() 和 join_all() 确保所有模块被安全停止。
   *           析构过程中捕获所有异常，防止异常从析构函数中传播。
   */
  ~HostRuntime();

  /**
   * @brief 注册一个模块到运行时
   * @param module 模块的唯一指针，所有权转移给 HostRuntime
   *
   * @details 将模块添加到内部模块列表。模块的启动顺序即为注册顺序，
   *          停止顺序为注册顺序的逆序（PersistenceService 特殊处理）。
   *          注册应在调用 start_all() 之前完成。
   */
  void register_module(std::unique_ptr<Module> module);

  /**
   * @brief 启动所有已注册的模块
   *
   * @details 按注册顺序依次调用每个模块的 start() 方法。
   *          启动顺序影响模块间的依赖关系——先启动的模块可为后续模块提供服务。
   *          例如，LogService 最先启动以便其他模块在初始化时即可记录日志。
   *
   * @note 如果某个模块启动失败，已启动的模块不会自动停止。
   *       调用者需要在异常处理中手动调用 stop_all()。
   */
  void start_all();

  /**
   * @brief 停止所有模块
   *
   * @details 停止顺序采用特殊策略：
   *          1. 首先请求关闭令牌（ShutdownToken），通知所有模块准备退出
   *          2. 按注册顺序的逆序遍历模块（PersistenceService 跳过），
   *             依次调用 stop() 和 join() 等待模块线程退出
   *          3. 最后单独停止 PersistenceService，确保其他模块停止时
   *             持久化服务仍然可用，用于保存最终状态
   *          4. 关闭消息总线的所有端点，释放资源
   *
   * @note PersistenceService 最后停止是为了让其他模块在退出前
   *       有机会保存数据到数据库。
   */
  void stop_all();

  /**
   * @brief 等待所有模块线程退出
   *
   * @details 遍历所有模块并调用它们的 join() 方法。
   *          通常在 stop_all() 之后调用，确保所有模块线程已完全退出。
   */
  void join_all();

  /**
   * @brief 将运行时状态快照写入 JSON 文件
   *
   * @details 生成包含以下信息的 JSON 状态文件：
   *          - queues: 消息总线上各端点的队列深度
   *          - metrics: 当前所有度量指标的数值
   *          - modules: 各模块的自定义状态快照
   *
   *          文件路径由配置中的 runtime.status_file 指定，
   *          相对于 root_dir 目录。如果父目录不存在则自动创建。
   *
   * @note JSON 内容采用手动序列化以最小化依赖。
   *       字符串值会进行 JSON 转义处理以防止格式错误。
   */
  void write_status_snapshot() const;

  /**
   * @brief 获取运行时上下文的可变引用
   * @return HostContext 的可变引用
   */
  [[nodiscard]] HostContext& context() { return context_; }

  /**
   * @brief 获取运行时上下文的常量引用
   * @return HostContext 的常量引用
   */
  [[nodiscard]] const HostContext& context() const { return context_; }

 private:
  std::filesystem::path root_dir_{};       ///< 服务器根目录路径
  LocalBus bus_{};                         ///< 模块间通信的消息总线
  MetricsRegistry metrics_{};              ///< 运行时度量注册表
  ShutdownToken shutdown_{};               ///< 优雅关闭协调令牌
  HostContext context_{};                  ///< 运行时上下文聚合
  std::vector<std::unique_ptr<Module>> modules_{};  ///< 已注册的模块列表
};

}  // namespace mir2
