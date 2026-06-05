/**
 * @file shutdown_token.hpp
 * @brief 优雅关闭协调令牌
 *
 * @details 该文件定义了 ShutdownToken 类，提供一种轻量级的线程间关闭信号传递机制。
 *          当系统需要关闭时，HostRuntime 通过 ShutdownToken 通知所有模块停止工作。
 *
 * 设计特点：
 * - 基于 std::atomic_bool 实现，无锁操作，性能开销极低
 * - 支持一写多读模式（一个线程设置停止标志，多个线程检测）
 * - 使用 memory_order_relaxed 内存序，在 x86/ARM 上均有良好性能
 *
 * 使用场景：
 * - 工作线程在主循环中检查 stop_requested() 决定是否退出
 * - 消息处理循环在每次迭代前检查关闭信号
 * - 模块的 stop() 实现中调用 request_stop() 触发关闭
 *
 * @note 这是一个"一次性"令牌——一旦请求停止就不能撤销。
 *       如果需要重新启动，应创建新的 ShutdownToken 实例。
 *
 * @see Module::stop() 模块停止时通常通过上下文访问 ShutdownToken
 * @see HostRuntime::stop_all() 停止所有模块时触发 ShutdownToken
 */

#pragma once

#include <atomic>

namespace mir2 {

/**
 * @brief 优雅关闭协调令牌
 *
 * @details 提供线程安全的关闭信号传递机制，用于协调多个模块的优雅关闭过程。
 *
 * 实现原理：
 * - 内部使用 std::atomic_bool 存储停止状态
 * - request_stop() 使用 memory_order_relaxed 进行原子写
 * - stop_requested() 使用 memory_order_relaxed 进行原子读
 *
 * 为什么使用 memory_order_relaxed：
 * - 关闭信号不依赖其他内存操作的顺序约束
 * - relaxed 序在 x86 上编译为普通 mov 指令，无额外开销
 * - 在 ARM 上也仅需单向屏障，比 acquire/release 更高效
 * - 关闭操作的逻辑正确性不依赖严格的内存序保证
 *
 * 典型用法：
 * @code
 * // 在工作线程中：
 * while (!shutdown_token_.stop_requested()) {
 *     ProcessNextMessage();
 * }
 *
 * // 在关闭触发处：
 * shutdown_token_.request_stop();
 * @endcode
 *
 * @warning 此令牌一旦请求停止即不可逆转。设计上不支持"取消关闭"场景。
 */
class ShutdownToken {
 public:
  /**
   * @brief 请求停止
   *
   * @details 将停止标志设置为 true。
   *          所有后续检查 stop_requested() 的线程都将收到停止信号。
   *          此操作是线程安全的，可在任意线程中调用。
   *
   * @note 使用 memory_order_relaxed 内存序，因为在关闭场景中，
   *       读线程不需要看到写线程的其他内存操作。
   */
  void request_stop() { stopped_.store(true, std::memory_order_relaxed); }

  /**
   * @brief 检查是否已请求停止
   * @return true 已请求停止；false 仍在运行
   *
   * @details 原子读取停止标志的当前值。
   *          通常在工作线程的主循环中调用，用于决定是否退出循环。
   */
  [[nodiscard]] bool stop_requested() const { return stopped_.load(std::memory_order_relaxed); }

 private:
  std::atomic_bool stopped_{false};  ///< 原子停用标志（初始为 false，即运行状态）
};

}  // namespace mir2
