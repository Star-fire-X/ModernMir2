/**
 * @file module.hpp
 * @brief 模块基类接口定义
 *
 * @details 该文件定义了 mir2 服务器中所有服务模块的抽象基类 Module，
 *          以及模块运行时上下文结构体 HostContext。
 *
 * Module 类采用"模板方法"设计模式，定义了模块的标准生命周期：
 * - start()  ：初始化并启动模块
 * - stop()   ：请求模块停止
 * - join()   ：等待模块线程结束
 * - name()   ：返回模块名称（用于识别和路由）
 * - snapshot()：返回模块状态快照（用于监控）
 *
 * HostContext 聚合了模块运行所需的所有共享资源指针，
 * 通过这种方式，模块可以访问消息总线、度量注册表、
 * 关闭令牌和日志记录器，而无需直接依赖 HostRuntime。
 *
 * @see HostRuntime 模块容器与生命周期管理器
 * @see LocalBus 消息总线
 * @see MetricsRegistry 度量注册表
 * @see ShutdownToken 关闭令牌
 */

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

/**
 * @brief 模块运行时上下文
 *
 * @details 聚合了模块在运行期间需要访问的所有共享资源和配置信息。
 *           各模块通过 start() 方法接收此上下文，并在整个生命周期内持有它。
 *
 * 包含的资源：
 * - root_dir：服务器根目录路径，用于文件操作
 * - config：主机配置，包含运行时参数
 * - bus：消息总线指针，用于模块间通信
 * - metrics：度量注册表指针，用于指标上报
 * - shutdown：关闭令牌指针，用于检测关闭信号
 * - logger：日志记录器，用于日志输出
 *
 * @note 除 root_dir 和 config 外，其他字段均为指针类型，
 *       指向由 HostRuntime 管理的对象。模块不应释放这些指针。
 */
struct HostContext {
  std::filesystem::path root_dir{};                    ///< 服务器根目录路径
  HostConfig config{};                                 ///< 主机运行时配置
  LocalBus* bus{nullptr};                              ///< 消息总线指针（用于模块间通信）
  MetricsRegistry* metrics{nullptr};                   ///< 度量注册表指针（用于上报指标）
  ShutdownToken* shutdown{nullptr};                    ///< 关闭令牌指针（用于检测关闭信号）
  std::shared_ptr<spdlog::logger> logger{};            ///< spdlog 日志记录器共享指针
};

/**
 * @brief 模块抽象基类
 *
 * @details Module 是所有服务模块的抽象接口，定义了以下生命周期方法：
 *
 * 生命周期管理（按调用顺序）：
 * 1. start(context) —— 模块初始化入口
 *    - 注册消息总线端点
 *    - 创建内部工作线程
 *    - 初始化模块所需资源
 *
 * 2. stop() —— 请求模块停止
 *    - 设置停止标志
 *    - 可能关闭总线端点以唤醒等待线程
 *    - 非阻塞，立即返回
 *
 * 3. join() —— 等待模块线程结束
 *    - 阻塞直到模块的所有工作线程退出
 *    - 保证资源被正确释放
 *
 * 信息查询：
 * 4. name() —— 返回模块唯一名称
 *    - 用于消息路由（作为总线端点名称）
 *    - 用于日志和监控标识
 *
 * 5. snapshot() —— 返回模块状态快照
 *    - 键值对形式的状态信息
 *    - 用于 write_status_snapshot() 生成状态报告
 *
 * @note 所有模块必须实现所有纯虚函数。对于不需要多线程的模块，
 *       stop() 和 join() 可以是空操作。
 *
 * @see HostRuntime 负责调用模块生命周期方法
 * @see register_default_modules() 注册默认模块实现
 */
class Module {
 public:
  /**
   * @brief 虚析构函数
   *
   * @details 确保派生类对象被正确析构。
   *          所有模块实现应确保在析构函数中释放已分配的资源。
   */
  virtual ~Module() = default;

  /**
   * @brief 获取模块名称
   * @return 模块的唯一名称（字符串）
   *
   * @details 名称用于：
   * - 在消息总线上注册端点（通常以名称作为端点名）
   * - 在状态快照中标识模块
   * - 在日志输出中标记消息来源
   * - 在 stop_all() 中识别特殊模块（如 persistence_service）
   *
   * @warning 模块名称应在进程内保持全局唯一。
   *          建议使用小写字母和下划线命名，如 "auth_service"、"world_service"。
   */
  [[nodiscard]] virtual std::string name() const = 0;

  /**
   * @brief 启动模块
   * @param context 运行时上下文引用
   *
   * @details 模块初始化入口，在此方法中模块应：
   * 1. 保存上下文引用以备后续使用
   * 2. 在消息总线上注册端点
   * 3. 创建内部工作线程（如果需要）
   * 4. 初始化模块所需的资源
   *
   * @note 此方法会在 HostRuntime::start_all() 中被顺序调用。
   *       如果模块启动失败应抛出异常。
   */
  virtual void start(HostContext& context) = 0;

  /**
   * @brief 请求模块停止
   *
   * @details 通知模块停止工作。此方法应：
   * 1. 设置内部停止标志
   * 2. 关闭总线端点以唤醒等待的线程
   * 3. 不等待线程退出（非阻塞设计）
   *
   * @note stop() 不应阻塞等待线程结束——那是 join() 的职责。
   *       stop() 和 join() 分离的设计允许并发停止多个模块。
   */
  virtual void stop() = 0;

  /**
   * @brief 等待模块线程完全退出
   *
   * @details 阻塞调用线程，直到模块的所有工作线程完成执行并退出。
   *          通常在 stop() 之后调用。对于不使用独立线程的模块，
   *          此方法可以是空操作（no-op）。
   */
  virtual void join() = 0;

  /**
   * @brief 获取模块当前状态快照
   * @return 键值对形式的状态信息
   *
   * @details 返回模块的当前内部状态，用于生成状态监控报告。
   *          返回的内容因模块而异，可能包括：
   *          - 活跃连接数
   *          - 消息处理计数
   *          - 内部状态标志
   *          - 错误统计
   *
   * @note 此方法不应修改模块状态（const 语义）。
   *       如果模块没有特定的状态需要报告，可以返回空映射。
   */
  [[nodiscard]] virtual std::unordered_map<std::string, std::string> snapshot() const = 0;
};

}  // namespace mir2
