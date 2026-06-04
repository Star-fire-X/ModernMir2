/**
 * @file wheel_timer.hpp
 * @brief 哈希时间轮（Hashed Wheel Timer）实现
 *
 * @details 该文件实现了 WheelTimer 模板类，基于"哈希时间轮"算法（Hashed Timing Wheel）
 *          提供高效的定时器管理。时间轮是一种经典的定时器数据结构，将时间划分为
 *          固定数量的槽位（slot），每个槽位对应一个时间刻度，通过取模运算实现
 *          O(1) 的定时器调度和到期检查。
 *
 * 算法原理：
 * 1. 时间轮由 N 个槽位组成（默认 512 个），每个槽位存储一个定时器节点链表
 * 2. 调度定时器时，计算 (current_tick + delay_ticks) % N 得到目标槽位索引
 * 3. 每经过一个 tick，检查当前 tick 对应的槽位，取出所有到期的定时器
 * 4. 通过"当前 tick 取模 N"定位到要检查的槽位
 *
 * 时间复杂度：
 * - schedule()：O(1)，直接计算槽位索引并插入链表
 * - pop_ready()：O(K)，K 为当前槽位中到期的定时器数量
 *
 * 使用场景：
 * - 游戏中的周期性事件（如怪物刷新、Buff 效果触发）
 * - 延迟任务调度
 * - 超时检测
 *
 * @note 时间轮适用于 tick 间隔相对固定的场景。
 *       定时器精度取决于 tick 的粒度。此实现未处理"槽位溢出"问题——
 *       如果延迟时间大于 N 个 tick，定时器会绕回并可能被提前取出。
 *
 * @tparam T 定时器携带的值类型（如回调函数、事件 ID 等）
 */

#pragma once

#include <cstdint>
#include <list>
#include <utility>
#include <vector>

namespace mir2 {

/**
 * @brief 哈希时间轮模板类
 *
 * @tparam T 定时器到期时返回的值的类型
 *
 * @details WheelTimer 实现了经典的时间轮算法，适用于高频率、低延迟的定时任务。
 *
 * 内部结构：
 * - buckets_：槽位数组，每个槽位是一个 std::list<Node>
 * - Node：包含到期 tick 和用户值
 * - slots_：槽位总数，决定时间轮的精度和容量
 *
 * 工作原理：
 * 1. 时间轮用 tick 作为时间单位，而非绝对时间
 * 2. schedule() 根据当前 tick + 延迟 tick 计算到期时间
 * 3. 到期时间对 slots_ 取模得到槽位索引
 * 4. pop_ready() 检查当前 tick 对应的槽位，取出所有到期节点
 *
 * 局限性：
 * - 定时器最大延迟不能超过 std::uint64_t 的最大值（实际受 slots_ 限制）
 * - 不能保证同一槽位内定时器的严格顺序（但通过链表实现了 FIFO 近似）
 * - 需要外部驱动（持续调用 pop_ready() 并推进 current_tick）
 *
 * @note 如果延迟时间大于 slots_，定时器会绕回到前面的槽位。
 *       但到时检查中仍会正确比较 due_tick <= current_tick，
 *       因此大延迟定时器也能正常工作，只是可能影响槽位的负载均衡。
 */
template <typename T>
class WheelTimer {
 public:
  /**
   * @brief 构造函数
   * @param slots 时间轮槽位数量（默认 512）
   *
   * @details 创建一个具有指定槽位数量的时间轮。
   *          槽位数量影响：
   *          - 内存占用：每个槽位是一个空链表，slots 越大占用越多
   *          - 定时器分布：slots 越大，定时器分布越均匀，冲突越少
   *          - 取模计算开销：无影响（编译期优化）
   *
   * @note 建议 slots 选择 2 的幂，这样取模运算可优化为位与操作。
   *       512 是一个平衡内存和性能的合理值。
   */
  explicit WheelTimer(std::size_t slots = 512) : slots_(slots), buckets_(slots) {}

  /**
   * @brief 调度一个定时器
   * @param current_tick 当前 tick 值
   * @param delay_ticks 从当前 tick 开始的延迟 tick 数
   * @param value 定时器到期时要返回的值
   *
   * @details 将值调度到未来的某个 tick 触发。
   *          计算方式：due_tick = current_tick + delay_ticks
   *          槽位索引：due_tick % slots_
   *
   * @note 如果 delay_ticks 为 0，定时器将在下次调用 pop_ready() 时立即到期。
   *       调用者应确保 value 类型支持移动语义，因为内部使用 std::move 转移所有权。
   */
  void schedule(std::uint64_t current_tick, std::uint64_t delay_ticks, T value) {
    const auto due_tick = current_tick + delay_ticks;
    buckets_[due_tick % slots_].push_back(Node{due_tick, std::move(value)});
  }

  /**
   * @brief 取出所有到期的定时器值
   * @param current_tick 当前 tick 值
   * @return 所有到期定时器携带的值组成的向量
   *
   * @details 检查当前 tick 对应的槽位，遍历槽位中的链表，
   *          取出所有 due_tick <= current_tick 的节点。
   *
   * 实现要点：
   * - 由于定时器可能由于绕回而被放入"过去"的槽位，
   *   所以不能仅靠槽位索引判断到期，还需要比较 due_tick
   * - 使用迭代器遍历，取出后及时从链表中删除（erase）
   * - 返回的 vector 中元素的顺序与同一槽位内的插入顺序一致
   *
   * @note 调用者需要外部驱动 tick 推进（如定时器线程定期递增 current_tick 并调用此方法）。
   *       不同槽位的定时器不会在同一批中取出，这是时间轮的固有特性。
   */
  [[nodiscard]] std::vector<T> pop_ready(std::uint64_t current_tick) {
    std::vector<T> ready;
    auto& bucket = buckets_[current_tick % slots_];
    for (auto it = bucket.begin(); it != bucket.end();) {
      if (it->due_tick <= current_tick) {
        ready.push_back(std::move(it->value));
        it = bucket.erase(it);
      } else {
        ++it;
      }
    }
    return ready;
  }

 private:
  /**
   * @brief 内部节点结构体
   *
   * @details 存储定时器的到期 tick 和携带的值。
   *          在链表中的位置决定了在槽位中的顺序。
   */
  struct Node {
    std::uint64_t due_tick{0};  ///< 到期 tick（绝对 tick 值）
    T value{};                  ///< 定时器携带的用户值
  };

  std::size_t slots_{0};                    ///< 时间轮槽位总数
  std::vector<std::list<Node>> buckets_{};  ///< 槽位数组，每个槽位是一个节点链表
};

}  // namespace mir2
