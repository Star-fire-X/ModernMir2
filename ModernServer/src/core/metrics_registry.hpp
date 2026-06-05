/**
 * @file metrics_registry.hpp
 * @brief 度量/遥测注册表——运行时性能监控与指标收集
 *
 * @details 该文件定义了 MetricsRegistry 类，提供一个轻量级的线程安全度量收集器。
 *          支持两种基本的度量类型：
 *          1. Counter（计数器）：单调递增的值，适用于请求计数、错误计数等
 *          2. Gauge（仪表盘）：可任意设置的值，适用于连接数、队列深度等
 *
 * 设计特点：
 * - 线程安全：所有操作均通过互斥锁保护
 * - 轻量级：仅记录数值，不做聚合统计
 * - 快照导出：支持获取所有度量的当前值快照
 *
 * 使用场景：
 * - 记录模块处理的请求数量（Counter）
 * - 记录当前活跃连接数（Gauge）
 * - 记录处理延迟或错误率（结合外部计算）
 * - 状态监控面板的数据源
 *
 * @note 当前实现为简单的内存计数器，适合单进程场景。
 *       如需分布式监控，可在快照导出时对接外部监控系统（如 Prometheus）。
 */

#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace mir2 {

/**
 * @brief 度量注册表——线程安全的指标收集器
 *
 * @details MetricsRegistry 提供以下功能：
 *
 * 1. 计数器（Counter）：
 *    - 通过 increment_counter() 累加
 *    - 适用于累计值指标，如"总请求数"、"总错误数"
 *    - 初始值为 0，只增不减（除非不指定 delta）
 *
 * 2. 仪表盘（Gauge）：
 *    - 通过 set_gauge() 设置绝对值
 *    - 适用于瞬时值指标，如"当前在线人数"、"队列深度"
 *    - 可升可降，反映当前状态
 *
 * 3. 快照：
 *    - snapshot() 返回所有计数器和仪表盘的合并快照
 *    - 计数器值优先（同名时覆盖仪表盘）
 *
 * 线程安全模型：
 * - 所有公共操作均通过 mutex_ 保护
 * - 读操作（snapshot）和写操作（increment_counter/set_gauge）互斥
 *
 * @note 度量名称建议采用层级命名方式，如 "auth.login.success"、"world.player.count"，
 *       便于在监控系统中进行层次化查询和聚合。
 */
class MetricsRegistry {
 public:
  /**
   * @brief 递增指定计数器的值
   * @param name 计数器名称
   * @param delta 增量值（默认为 1）
   *
   * @details 对指定名称的计数器执行原子性累加操作。
   *          如果该计数器不存在，则自动创建并初始化为 delta。
   *          计数器适合记录事件发生的次数。
   */
  void increment_counter(const std::string& name, std::int64_t delta = 1);

  /**
   * @brief 设置指定仪表盘的值
   * @param name 仪表盘名称
   * @param value 要设置的当前值
   *
   * @details 将指定名称的仪表盘设置为给定值。
   *          如果该仪表盘不存在，则自动创建。
   *          仪表盘适合记录可上下波动的瞬时值。
   */
  void set_gauge(const std::string& name, std::int64_t value);

  /**
   * @brief 获取所有度量的当前值快照
   * @return 度量名称到数值的映射表
   *
   * @details 合并所有计数器和仪表盘的当前值返回。
   *          如果计数器和仪表盘存在同名项，计数器的值将覆盖仪表盘的值。
   *          返回的映射表是调用时刻的一个快照。
   *
   * @note 调用者不应假设返回结果中包含特定度量，
   *       只有在调用 snapshot() 之前至少设置过一次的度量才会出现。
   */
  [[nodiscard]] std::unordered_map<std::string, std::int64_t> snapshot() const;

 private:
  mutable std::mutex mutex_{};                    ///< 保护度量数据的互斥锁
  std::unordered_map<std::string, std::int64_t> counters_{};  ///< 计数器存储（名称 -> 当前值）
  std::unordered_map<std::string, std::int64_t> gauges_{};    ///< 仪表盘存储（名称 -> 当前值）
};

/**
 * @brief 递增计数器实现
 * @param name 计数器名称
 * @param delta 增量值
 *
 * @details 在互斥锁保护下对计数器执行累加。
 *          使用 operator+= 会自动处理键不存在的情况（初始化为 0 后再加 delta）。
 */
inline void MetricsRegistry::increment_counter(const std::string& name, std::int64_t delta) {
  std::scoped_lock lock(mutex_);
  counters_[name] += delta;
}

/**
 * @brief 设置仪表盘值实现
 * @param name 仪表盘名称
 * @param value 新值
 *
 * @details 在互斥锁保护下直接赋值。
 *          使用 operator= 覆盖旧值，简单直接。
 */
inline void MetricsRegistry::set_gauge(const std::string& name, std::int64_t value) {
  std::scoped_lock lock(mutex_);
  gauges_[name] = value;
}

/**
 * @brief 获取快照实现
 * @return 度量名称到数值的映射
 *
 * @details 实现步骤：
 * 1. 加锁保护
 * 2. 复制所有计数器到结果映射
 * 3. 遍历仪表盘，插入结果映射（计数器优先，同名不会覆盖）
 * 4. 返回合并后的快照
 *
 * @note 此操作涉及两个 unordered_map 的完整遍历，数据量较大时可能影响性能。
 *       当前设计假设度量数量在合理范围内（通常数十到数百个）。
 */
inline std::unordered_map<std::string, std::int64_t> MetricsRegistry::snapshot() const {
  std::scoped_lock lock(mutex_);
  std::unordered_map<std::string, std::int64_t> combined = counters_;
  for (const auto& [key, value] : gauges_) {
    combined[key] = value;
  }
  return combined;
}

}  // namespace mir2
