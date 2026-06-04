/**
 * @file legacy_event_manager.hpp
 * @brief 传统定时事件管理器头文件
 * @details 定义LegacyEventManager类，用于管理和调度游戏世界中的各类定时事件。
 *          支持普通事件和神圣帷幕（Holy Curtain）事件组的管理。
 *          事件包括：矿石采集、僵尸挖掘、石堆、神圣帷幕、火焰燃烧、雕像碎片等。
 */

#pragma once

#include <optional>
#include <vector>

#include "world/game_object.hpp"

namespace mir2 {

/**
 * @struct LegacyEventManagerRun
 * @brief 事件管理器心跳运行结果
 * @details 包含每次心跳执行后产生的所有事件变更信息。
 *          用于通知外部系统处理火灾燃烧伤害、关闭事件和清理已关闭事件。
 */
struct LegacyEventManagerRun {
  RuntimeDispatch dispatch{};                    ///< 运行时调度/追踪信息
  std::vector<LegacyEventRecord> fire_burn_events{};   ///< 需要施加火焰燃烧伤害的事件列表
  std::vector<LegacyEventRecord> closed_events{};       ///< 本次关闭的事件列表
  std::vector<LegacyEventRecord> cleaned_events{};      ///< 彻底清理的已关闭事件列表
};

/**
 * @class LegacyEventManager
 * @brief 传统定时事件管理器
 * @details 管理游戏世界中的所有定时事件：
 *
 *          - 事件队列管理：支持添加、查找、心跳更新、关闭和清理
 *          - 神圣帷幕（Holy Curtain）组管理：特殊的事件组，一组事件共享生命周期
 *          - 火焰燃烧伤害：定时触发火墙伤害计算
 *          - 事件ID自动分配：支持自定义ID和自动递增ID
 *
 *          事件生命周期：active（活跃中）-> closed（已关闭，保留TTL）-> cleaned（清理）
 *          已关闭事件保留5分钟后被彻底清理，用于追踪和延迟处理。
 */
class LegacyEventManager {
 public:
  /**
   * @brief 添加事件到队列
   * @details 如果事件设置了 skip_if_occupied 且目标位置已有活跃事件，则跳过。
   *          自动分配事件ID（如果未指定），设置运行间隔（默认500ms）。
   * @param record 事件记录
   * @param now_ms 当前时间戳
   * @return 分配的事件ID，0表示跳过添加
   */
  [[nodiscard]] std::uint64_t enqueue(LegacyEventRecord record, std::uint64_t now_ms);

  /**
   * @brief 添加神圣帷幕事件组
   * @param group 神圣帷幕组
   * @param now_ms 当前时间戳
   * @return 分配的事件组ID
   */
  [[nodiscard]] std::uint64_t enqueue_holy_curtain_group(
      LegacyHolyCurtainGroup group, std::uint64_t now_ms);

  /**
   * @brief 查找指定位置和类型的活跃事件
   * @param map_id 地图ID
   * @param x X坐标
   * @param y Y坐标
   * @param type 事件类型
   * @return 匹配的事件记录，未找到返回 std::nullopt
   */
  [[nodiscard]] std::optional<LegacyEventRecord> find(const std::string& map_id,
                                                      std::int32_t x,
                                                      std::int32_t y,
                                                      LegacyEventType type) const;

  /**
   * @brief 检查指定位置是否有活跃事件
   * @param map_id 地图ID
   * @param x X坐标
   * @param y Y坐标
   * @return true 有活跃事件，false 无
   */
  [[nodiscard]] bool has_active_event(const std::string& map_id,
                                      std::int32_t x,
                                      std::int32_t y) const;

  /**
   * @brief 获取所有活跃的神圣帷幕组
   * @return 活跃的神圣帷幕组列表
   */
  [[nodiscard]] std::vector<LegacyHolyCurtainGroup> active_holy_groups() const;

  /**
   * @brief 更新神圣帷幕组的占领状态
   * @details 当神圣帷幕核心区域的占领者变化时调用。
   *          如果占领者列表为空，则关闭该组下的所有事件并移除该组。
   * @param group_id 帷幕组ID
   * @param seized_actor_ids 当前占领者的角色ID列表
   * @param now_ms 当前时间戳
   * @param current_tick 当前计时器滴答数
   * @return 事件运行结果（包含关闭事件等）
   */
  [[nodiscard]] LegacyEventManagerRun update_holy_group_seized(
      std::uint64_t group_id, std::vector<std::uint64_t> seized_actor_ids,
      std::uint64_t now_ms, std::uint64_t current_tick);

  /**
   * @brief 事件管理器心跳函数
   * @details 执行以下操作：
   *          1. 检查神圣帷幕组是否到期并关闭
   *          2. 遍历所有活跃事件，到期的执行run回调
   *          3. fire_burn类型事件每3秒产生一次伤害
   *          4. 超时事件自动关闭
   *          5. 清理已关闭超过5分钟的事件
   * @param now_ms 当前时间戳
   * @param current_tick 当前计时器滴答数
   * @return 事件运行结果（包含fire_burn、关闭、清理事件）
   */
  [[nodiscard]] LegacyEventManagerRun run(std::uint64_t now_ms, std::uint64_t current_tick);

  /** @brief 获取活跃事件数量 */
  [[nodiscard]] std::size_t active_count() const { return active_events_.size(); }
  /** @brief 获取已关闭但尚未清理的事件数量 */
  [[nodiscard]] std::size_t closed_count() const { return closed_events_.size(); }

 private:
  /**
   * @brief 获取事件类型的字符串名称
   * @param type 事件类型
   * @return 事件类型名称字符串
   */
  [[nodiscard]] std::string type_name(LegacyEventType type) const;

  /**
   * @brief 添加事件追踪记录
   * @param dispatch 运行时调度信息
   * @param event 事件记录
   * @param action 操作名称（如"run"、"close"）
   * @param now_ms 当前时间戳
   * @param current_tick 当前计时器滴答数
   * @param success 操作是否成功
   */
  void add_trace(RuntimeDispatch& dispatch, const LegacyEventRecord& event,
                 std::string action, std::uint64_t now_ms, std::uint64_t current_tick,
                 bool success = true) const;

  /**
   * @brief 添加神圣帷幕组追踪记录
   * @param dispatch 运行时调度信息
   * @param group 神圣帷幕组
   * @param action 操作名称
   * @param now_ms 当前时间戳
   * @param current_tick 当前计时器滴答数
   * @param success 操作是否成功
   */
  void add_group_trace(RuntimeDispatch& dispatch, const LegacyHolyCurtainGroup& group,
                       std::string action, std::uint64_t now_ms,
                       std::uint64_t current_tick, bool success = true) const;

  /**
   * @brief 关闭指定索引的活跃事件
   * @details 将事件从active移动到closed状态，记录关闭时间。
   * @param index 事件在active_events_中的索引
   * @param result 输出参数，记录关闭事件
   * @param now_ms 当前时间戳
   * @param current_tick 当前计时器滴答数
   */
  void close_event_at(std::size_t index, LegacyEventManagerRun& result,
                      std::uint64_t now_ms, std::uint64_t current_tick);

  /**
   * @brief 关闭神圣帷幕组下的所有事件
   * @details 遍历组中所有事件ID，依次关闭。
   * @param group 神圣帷幕组
   * @param result 输出参数，记录关闭事件
   * @param now_ms 当前时间戳
   * @param current_tick 当前计时器滴答数
   */
  void close_holy_group_events(LegacyHolyCurtainGroup& group, LegacyEventManagerRun& result,
                               std::uint64_t now_ms, std::uint64_t current_tick);

  std::vector<LegacyEventRecord> active_events_{};   ///< 活跃事件列表
  std::vector<LegacyEventRecord> closed_events_{};    ///< 已关闭但未清理的事件列表
  std::uint64_t next_event_id_{1};                    ///< 下一个可用的事件ID
  std::vector<LegacyHolyCurtainGroup> holy_groups_{}; ///< 神圣帷幕组列表
  std::uint64_t next_holy_group_id_{1};               ///< 下一个可用的神圣帷幕组ID
};

}  // namespace mir2
