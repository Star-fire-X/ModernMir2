/**
 * @file bounded_mpsc_queue.hpp
 * @brief 有界多生产者单消费者队列（Bounded Multi-Producer Single-Consumer Queue）
 *
 * @details 该文件实现了一个线程安全的有界队列，支持多个生产者线程同时向队列中推送数据，
 * 但仅允许单个消费者线程从队列中获取数据。这种设计模式适用于模块间通信场景，
 * 其中多个发送者（生产者）可以向同一个接收者（消费者）发送消息。
 *
 * 设计特点：
 * - 使用 std::mutex 保证多生产者线程安全
 * - 使用 std::condition_variable 支持带超时的阻塞等待
 * - 支持优雅关闭，关闭后等待中的消费者会立即返回
 * - 固定容量，防止无限制的内存增长
 *
 * @note 此队列为有界队列，当队列满时 try_push() 会返回 false 而不会阻塞。
 *       消费者可以通过 wait_pop_for() 实现阻塞等待，或通过 try_pop() 实现非阻塞获取。
 */

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>

namespace mir2 {

/**
 * @brief 有界多生产者单消费者队列模板类
 *
 * @tparam T 队列中存储的元素类型
 *
 * @details 本类实现了一个线程安全的有界队列，遵循多生产者单消费者（MPSC）模式。
 *          内部使用 std::deque 作为底层容器，通过互斥锁保护所有写操作，
 *          并通过条件变量实现高效的等待通知机制。
 *
 * 使用场景：
 * - 模块间消息传递
 * - 任务分发队列
 * - 事件缓冲
 *
 * 线程安全保证：
 * - try_push()：多生产者安全，可并发调用
 * - try_pop()：仅消费者调用，但仍线程安全
 * - wait_pop_for()：仅消费者调用，支持超时等待
 * - close()：可被任意线程调用，通知所有等待的消费者
 */
template <typename T>
class BoundedMpscQueue {
 public:
  /**
   * @brief 构造函数
   * @param capacity 队列的最大容量
   *
   * @details 创建一个具有指定容量的有界队列。
   *          当队列中元素数量达到 capacity 时，push 操作将失败。
   */
  explicit BoundedMpscQueue(std::size_t capacity) : capacity_(capacity) {}

  /**
   * @brief 尝试向队列中推送一个元素（非阻塞）
   * @param value 要推送的元素（通过右值引用转移所有权）
   * @return true 推送成功；false 队列已满，推送失败
   *
   * @details 如果队列未满，将元素添加到队尾并通知等待的消费者。
   *          如果队列已满，立即返回 false 而不阻塞调用线程。
   *          此操作是线程安全的，可被多个生产者并发调用。
   */
  bool try_push(T value) {
    {
      std::scoped_lock lock(mutex_);
      if (queue_.size() >= capacity_) {
        return false;
      }
      queue_.push_back(std::move(value));
    }
    // 通知等待中的消费者有新元素可用
    signal_.notify_one();
    return true;
  }

  /**
   * @brief 尝试从队列中取出一个元素，支持阻塞等待和超时
   * @param timeout 最大等待时间
   * @return 如果成功取出元素，返回包含该元素的 std::optional；否则返回 std::nullopt
   *
   * @details 如果队列为空，调用线程将阻塞最多 timeout 时长等待新元素到来。
   *          当队列被关闭时，即使没有元素也会返回 std::nullopt。
   *          此函数通常由消费者线程调用。
   *
   * @note 超时返回并不一定意味着队列为空或已关闭，仅表示在指定时间内没有新元素到达。
   */
  std::optional<T> wait_pop_for(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    // 等待直到队列非空或已关闭，或超时
    if (!signal_.wait_for(lock, timeout, [this] { return closed_ || !queue_.empty(); })) {
      return std::nullopt;
    }
    if (queue_.empty()) {
      return std::nullopt;
    }
    T value = std::move(queue_.front());
    queue_.pop_front();
    return value;
  }

  /**
   * @brief 尝试从队列中取出一个元素（非阻塞）
   * @return 如果队列非空，返回包含队首元素的 std::optional；否则返回 std::nullopt
   *
   * @details 如果队列非空，立即返回队首元素；否则立即返回 std::nullopt。
   *          此操作不会阻塞调用线程。
   */
  std::optional<T> try_pop() {
    std::scoped_lock lock(mutex_);
    if (queue_.empty()) {
      return std::nullopt;
    }
    T value = std::move(queue_.front());
    queue_.pop_front();
    return value;
  }

  /**
   * @brief 关闭队列
   *
   * @details 将队列标记为已关闭状态，并唤醒所有正在等待的消费者线程。
   *          关闭后，所有正在 wait_pop_for() 中等待的线程将立即返回 std::nullopt。
   *          此操作可用于优雅地通知消费者线程停止等待并退出。
   *
   * @note 关闭后 push 操作仍可能成功（取决于具体实现），但消费者将不再等待新元素。
   */
  void close() {
    {
      std::scoped_lock lock(mutex_);
      closed_ = true;
    }
    signal_.notify_all();
  }

  /**
   * @brief 获取队列当前元素数量
   * @return 队列中的元素个数
   *
   * @details 返回队列中当前存储的元素数量。
   *          注意：在多线程环境中，返回值在返回时可能已过时。
   */
  [[nodiscard]] std::size_t size() const {
    std::scoped_lock lock(mutex_);
    return queue_.size();
  }

  /**
   * @brief 获取队列的最大容量
   * @return 队列容量
   *
   * @details 返回队列在构造函数中指定的最大容量。
   *          此值在队列生命周期内保持不变，无需加锁。
   */
  [[nodiscard]] std::size_t capacity() const { return capacity_; }

 private:
  std::size_t capacity_{0};              ///< 队列最大容量
  mutable std::mutex mutex_{};           ///< 保护队列访问的互斥锁
  std::condition_variable signal_{};     ///< 用于通知消费者的条件变量
  std::deque<T> queue_{};               ///< 底层双端队列容器
  bool closed_{false};                   ///< 队列是否已关闭的标志
};

}  // namespace mir2
