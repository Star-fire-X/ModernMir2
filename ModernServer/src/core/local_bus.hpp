/**
 * @file local_bus.hpp
 * @brief 本地消息总线——模块间通信的核心机制
 *
 * @details 该文件定义了 LocalBus 类，实现了一个轻量级的进程内消息总线，
 *          用于服务器各模块之间的异步通信。总线采用"端点-队列"模型：
 *          每个接收方注册一个命名端点，发送方通过端点名称定向投递消息。
 *
 * 设计特点：
 * - 基于有界 MPSC 队列实现，每个端点拥有独立的队列
 * - 支持点对点通信（通过端点名称精确投递）
 * - 线程安全：支持多生产者同时投递消息
 * - 队列深度监控：支持查询单个或所有端点的队列深度
 * - 优雅关闭：支持关闭所有端点，唤醒等待中的消费者
 *
 * 消息流示例：
 *   模块A（生产者）——> LocalBus.post("模块B", 消息) ——> 模块B的端点队列 ——> 模块B（消费者）
 *
 * @see BoundedMpscQueue 底层有界队列实现
 * @see BusMessage 消息类型定义
 */

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "core/bounded_mpsc_queue.hpp"
#include "core/messages.hpp"

namespace mir2 {

/**
 * @brief 本地消息总线——模块间异步消息传递的中介
 *
 * @details LocalBus 提供了以下核心能力：
 *
 * 1. 端点注册（register_endpoint）
 *    - 模块在启动时注册自己的命名端点
 *    - 每个端点包含一个独立的有界 MPSC 队列
 *    - 端点的容量在注册时指定，防止无限制的消息积压
 *
 * 2. 消息投递（post）
 *    - 通过目标端点名称投递消息
 *    - 如果目标端点不存在，投递失败并返回 false
 *    - 线程安全，可被多个发送者同时调用
 *
 * 3. 队列监控（queue_depth / queue_depths）
 *    - 查询单个或所有端点的当前队列深度
 *    - 用于监控消息处理延迟和系统负载
 *
 * 4. 优雅关闭（close_all）
 *    - 关闭所有端点的队列
 *    - 唤醒所有正在等待消息的消费者线程
 *
 * 线程安全模型：
 * - 端点的注册/查找操作受互斥锁保护，这是查表操作的关键区
 * - 消息投递仅在查找端点时加锁，实际入队操作在锁外执行（避免锁持有时间过长）
 * - 队列深度查询读操作受互斥锁保护，保证数据一致性
 *
 * @note LocalBus 仅支持进程内通信。对于跨进程或跨服务器的通信，
 *       需要使用网络层组件（如网关服务）进行桥接。
 */
class LocalBus {
 public:
  /**
   * @brief 总线端点结构体
   *
   * @details 每个端点包含一个名称标识和一个有界消息队列。
   *          端点名称通常与模块名称一致，便于消息路由。
   */
  struct Endpoint {
    std::string name{};                                        ///< 端点名称（唯一标识）
    std::shared_ptr<BoundedMpscQueue<BusMessage>> queue{};     ///< 有界消息队列
  };

  /**
   * @brief 注册一个命名端点
   * @param name 端点名称（应全局唯一）
   * @param capacity 端点队列的最大容量
   * @return 指向新注册端点的共享指针
   *
   * @details 创建一个具有指定容量的新端点并注册到总线中。
   *          调用者（通常是模块）持有返回的 Endpoint 指针，
   *          用于从队列中消费消息。
   *
   * @note 如果同名端点已存在，旧端点将被新端点替换。
   *       建议在模块初始化阶段注册端点。
   */
  std::shared_ptr<Endpoint> register_endpoint(const std::string& name, std::size_t capacity);

  /**
   * @brief 向指定端点投递消息
   * @param target 目标端点名称
   * @param message 要投递的消息（通过右值引用转移所有权）
   * @return true 投递成功；false 目标端点不存在或队列已满
   *
   * @details 根据目标端点名称查找对应的消息队列并尝试入队。
   *          如果目标端点不存在或队列已满，操作失败并返回 false。
   *          此函数是线程安全的，可被多个生产者并发调用。
   *
   * @note 投递是异步的，post() 返回仅表示消息已进入目标队列，
   *       不保证消息已被处理。
   */
  bool post(const std::string& target, BusMessage message);

  /**
   * @brief 查询指定端点的当前队列深度
   * @param target 端点名称
   * @return 队列中待处理的消息数量（如果端点不存在返回 0）
   */
  [[nodiscard]] std::size_t queue_depth(const std::string& target) const;

  /**
   * @brief 获取所有端点的队列深度映射
   * @return 端点名称到队列深度的映射表
   *
   * @details 遍历所有注册的端点，收集每个端点的当前队列深度。
   *          返回的映射可用于监控系统负载和检测消息积压。
   */
  [[nodiscard]] std::unordered_map<std::string, std::size_t> queue_depths() const;

  /**
   * @brief 关闭所有端点的队列
   *
   * @details 遍历所有端点并关闭它们的消息队列。
   *          关闭后，正在等待消息的消费者线程将被唤醒并收到空结果，
   *          从而安全退出等待循环。此操作用于优雅关闭场景。
   */
  void close_all();

 private:
  mutable std::mutex mutex_{};                                      ///< 保护端点映射表的互斥锁
  std::unordered_map<std::string, std::shared_ptr<Endpoint>> endpoints_{};  ///< 端点名称到端点的映射表
};

/**
 * @brief 注册端点实现
 * @param name 端点名称
 * @param capacity 队列容量
 * @return 新创建端点的共享指针
 *
 * @details 创建一个新的 Endpoint 对象，包含一个指定容量的 BoundedMpscQueue，
 *          并将其注册到 endpoints_ 映射表中。返回的共享指针使调用者可以
 *          保留对端点的引用，用于后续的消息消费。
 */
inline std::shared_ptr<LocalBus::Endpoint> LocalBus::register_endpoint(const std::string& name,
                                                                       std::size_t capacity) {
  std::scoped_lock lock(mutex_);
  auto endpoint = std::make_shared<Endpoint>();
  endpoint->name = name;
  endpoint->queue = std::make_shared<BoundedMpscQueue<BusMessage>>(capacity);
  endpoints_[name] = endpoint;
  return endpoint;
}

/**
 * @brief 投递消息实现
 * @param target 目标端点名称
 * @param message 消息
 * @return 投递是否成功
 *
 * @details 实现要点：
 * 1. 在锁保护下查找目标端点
 * 2. 找到后将端点引用复制到局部变量
 * 3. 在锁外执行 try_push 操作，减少锁持有时间
 * 4. 这种"先查后发"的模式降低了锁竞争，提高了并发性能
 */
inline bool LocalBus::post(const std::string& target, BusMessage message) {
  std::shared_ptr<Endpoint> endpoint;
  {
    std::scoped_lock lock(mutex_);
    auto it = endpoints_.find(target);
    if (it == endpoints_.end()) {
      return false;
    }
    endpoint = it->second;
  }
  return endpoint->queue->try_push(std::move(message));
}

/**
 * @brief 查询队列深度实现
 * @param target 端点名称
 * @return 队列深度
 *
 * @details 在锁保护下查找端点并查询队列大小。
 *          如果端点不存在，返回 0 而不是报错，简化调用方的处理逻辑。
 */
inline std::size_t LocalBus::queue_depth(const std::string& target) const {
  std::scoped_lock lock(mutex_);
  auto it = endpoints_.find(target);
  if (it == endpoints_.end()) {
    return 0;
  }
  return it->second->queue->size();
}

/**
 * @brief 获取所有端点队列深度实现
 * @return 端点名称到队列深度的映射
 *
 * @details 在锁保护下遍历整个端点映射表，收集所有端点的队列深度。
 *          返回的映射是调用时的一个快照，后续的队列变化不会反映在其中。
 */
inline std::unordered_map<std::string, std::size_t> LocalBus::queue_depths() const {
  std::unordered_map<std::string, std::size_t> result;
  std::scoped_lock lock(mutex_);
  for (const auto& [name, endpoint] : endpoints_) {
    result[name] = endpoint->queue->size();
  }
  return result;
}

/**
 * @brief 关闭所有端点实现
 *
 * @details 遍历端点映射表，对每个端点的队列调用 close() 方法。
 *          关闭操作会唤醒所有正在等待消息的消费者线程。
 *          此函数通常在系统关闭时调用。
 */
inline void LocalBus::close_all() {
  std::scoped_lock lock(mutex_);
  for (const auto& [_, endpoint] : endpoints_) {
    endpoint->queue->close();
  }
}

}  // namespace mir2
