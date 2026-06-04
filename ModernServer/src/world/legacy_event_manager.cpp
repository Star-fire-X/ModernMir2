/**
 * @file legacy_event_manager.cpp
 * @brief 传统定时事件管理器实现
 * @details 实现了LegacyEventManager的全部功能，包括事件队列管理、
 *          神圣帷幕组管理、火焰燃烧伤害定时触发、事件到期关闭、
 *          已关闭事件清理等核心逻辑。
 */

#include "world/legacy_event_manager.hpp"

#include <algorithm>
#include <utility>

namespace mir2 {

/**
 * @brief 匿名命名空间，定义内部辅助函数
 */
namespace {

/**
 * @brief 判断时间是否已超过指定间隔
 * @details 计算 now_ms - start_ms > interval_ms，用于检查定时器是否到期。
 *          使用减法而非加法，避免溢出问题。
 * @param now_ms 当前时间戳
 * @param start_ms 开始时间戳
 * @param interval_ms 间隔时间
 * @return true 已超过间隔，false 未超过
 */
bool elapsed_gt(std::uint64_t now_ms, std::uint64_t start_ms, std::uint64_t interval_ms) {
  return now_ms - start_ms > interval_ms;
}

}  // namespace

/**
 * @brief 添加事件到队列
 * @details 事件入队逻辑：
 *          1. 如果设置了 skip_if_occupied，检查目标位置是否已有活跃事件
 *          2. 自动分配或更新事件ID（使用 next_event_id_ 递增）
 *          3. 设置时间戳：open_start_ms（创建时间），run_start_ms（上次运行时间）
 *          4. 设置默认运行间隔为500ms
 *          5. 标记为活跃（active=true, closed=false）
 * @param record 事件记录。如果 record.id 为0则自动分配新ID；
 *                如果 record.id 非0则更新 next_event_id_ 确保不冲突
 * @param now_ms 当前时间戳
 * @return 分配的事件ID，0表示跳过
 */
std::uint64_t LegacyEventManager::enqueue(LegacyEventRecord record, std::uint64_t now_ms) {
  // 如果设置了跳过已占用位置，检查该位置是否已有活跃事件
  if (record.skip_if_occupied && has_active_event(record.map_id, record.x, record.y)) {
    return 0;
  }
  // ID分配或同步
  if (record.id == 0) {
    record.id = next_event_id_++;
  } else {
    next_event_id_ = std::max(next_event_id_, record.id + 1);
  }
  // 初始化时间戳
  if (record.open_start_ms == 0) {
    record.open_start_ms = now_ms;
  }
  if (record.run_start_ms == 0) {
    record.run_start_ms = now_ms;
  }
  if (record.run_tick_ms == 0) {
    record.run_tick_ms = 500;
  }
  record.active = true;
  record.closed = false;
  active_events_.push_back(std::move(record));
  return active_events_.back().id;
}

/**
 * @brief 添加神圣帷幕事件组
 * @param group 神圣帷幕组，如果 group.id 为0则自动分配
 * @param now_ms 当前时间戳
 * @return 分配的事件组ID
 */
std::uint64_t LegacyEventManager::enqueue_holy_curtain_group(
    LegacyHolyCurtainGroup group, std::uint64_t now_ms) {
  if (group.id == 0) {
    group.id = next_holy_group_id_++;
  } else {
    next_holy_group_id_ = std::max(next_holy_group_id_, group.id + 1);
  }
  if (group.open_start_ms == 0) {
    group.open_start_ms = now_ms;
  }
  holy_groups_.push_back(std::move(group));
  return holy_groups_.back().id;
}

/**
 * @brief 查找指定位置和类型的活跃事件
 * @param map_id 地图ID
 * @param x X坐标
 * @param y Y坐标
 * @param type 事件类型
 * @return 匹配的事件记录（副本），未找到返回 std::nullopt
 */
std::optional<LegacyEventRecord> LegacyEventManager::find(const std::string& map_id,
                                                          std::int32_t x,
                                                          std::int32_t y,
                                                          LegacyEventType type) const {
  for (const auto& event : active_events_) {
    if (event.active && !event.closed && event.map_id == map_id && event.x == x &&
        event.y == y && event.type == type) {
      return event;
    }
  }
  return std::nullopt;
}

/**
 * @brief 检查指定位置是否有任何活跃事件
 * @param map_id 地图ID
 * @param x X坐标
 * @param y Y坐标
 * @return true 有活跃事件，false 无
 */
bool LegacyEventManager::has_active_event(const std::string& map_id,
                                          std::int32_t x,
                                          std::int32_t y) const {
  for (const auto& event : active_events_) {
    if (event.active && !event.closed && event.map_id == map_id && event.x == x &&
        event.y == y) {
      return true;
    }
  }
  return false;
}

/**
 * @brief 获取所有活跃神圣帷幕组的副本
 * @return 活跃神圣帷幕组列表
 */
std::vector<LegacyHolyCurtainGroup> LegacyEventManager::active_holy_groups() const {
  return holy_groups_;
}

/**
 * @brief 更新神圣帷幕组的占领者状态
 * @details 找到指定ID的帷幕组，更新其占领者角色ID列表。
 *          如果占领者列表为空（没有人在核心区域内），
 *          则关闭该组下的所有事件并从管理中移除该组。
 * @param group_id 帷幕组ID
 * @param seized_actor_ids 当前占领者的角色ID列表
 * @param now_ms 当前时间戳
 * @param current_tick 当前计时器滴答数
 * @return 事件运行结果（包含被关闭的事件）
 */
LegacyEventManagerRun LegacyEventManager::update_holy_group_seized(
    std::uint64_t group_id, std::vector<std::uint64_t> seized_actor_ids,
    std::uint64_t now_ms, std::uint64_t current_tick) {
  LegacyEventManagerRun result;
  for (auto group_it = holy_groups_.begin(); group_it != holy_groups_.end(); ++group_it) {
    if (group_it->id != group_id) {
      continue;
    }
    group_it->seized_actor_ids = std::move(seized_actor_ids);
    // 无人占领时关闭帷幕组
    if (group_it->seized_actor_ids.empty()) {
      add_group_trace(result.dispatch, *group_it, "holy_group_empty", now_ms, current_tick);
      close_holy_group_events(*group_it, result, now_ms, current_tick);
      holy_groups_.erase(group_it);
    }
    break;
  }
  return result;
}

/**
 * @brief 事件管理器心跳函数
 * @details 执行周期性的维护操作：
 *
 *          1. 神圣帷幕组到期检查：
 *             - 超过seize_ms或3分钟后自动关闭
 *
 *          2. 活跃事件处理：
 *             - 到运行间隔的事件触发run追踪
 *             - fire_burn类型事件每3秒产生一次伤害记录
 *             - 超过continue_ms的事件自动关闭
 *
 *          3. 已关闭事件清理：
 *             - 超过5分钟（kClosedTtlMs）的已关闭事件被彻底清理
 *
 * @param now_ms 当前时间戳
 * @param current_tick 当前计时器滴答数
 * @return 事件运行结果
 */
LegacyEventManagerRun LegacyEventManager::run(std::uint64_t now_ms,
                                              std::uint64_t current_tick) {
  LegacyEventManagerRun result;

  // 检查并关闭到期的神圣帷幕组
  for (auto group_it = holy_groups_.begin(); group_it != holy_groups_.end();) {
    const auto expired = (group_it->seize_ms > 0 &&
                          elapsed_gt(now_ms, group_it->open_start_ms, group_it->seize_ms)) ||
                         elapsed_gt(now_ms, group_it->open_start_ms, 3ULL * 60ULL * 1000ULL);
    if (expired) {
      add_group_trace(result.dispatch, *group_it, "holy_group_close", now_ms, current_tick);
      close_holy_group_events(*group_it, result, now_ms, current_tick);
      group_it = holy_groups_.erase(group_it);
      continue;
    }
    ++group_it;
  }

  // 遍历活跃事件，检查是否需要执行或关闭
  std::size_t index = 0;
  while (index < active_events_.size()) {
    auto& event = active_events_[index];
    if (event.active && !event.closed && elapsed_gt(now_ms, event.run_start_ms,
                                                    event.run_tick_ms)) {
      event.run_start_ms = now_ms;
      add_trace(result.dispatch, event, "run", now_ms, current_tick);
      // fire_burn类型事件：每3秒产生一次伤害
      if (event.type == LegacyEventType::fire_burn &&
          (event.last_damage_ms == 0 || elapsed_gt(now_ms, event.last_damage_ms, 3000ULL))) {
        event.last_damage_ms = now_ms;
        add_trace(result.dispatch, event, "fire_tick", now_ms, current_tick);
        result.fire_burn_events.push_back(event);
      }
      // 检查是否已超时
      if (elapsed_gt(now_ms, event.open_start_ms, event.continue_ms)) {
        close_event_at(index, result, now_ms, current_tick);
        continue;
      }
    }
    ++index;
  }

  // 清理已关闭超过5分钟的事件
  constexpr std::uint64_t kClosedTtlMs = 5ULL * 60ULL * 1000ULL;
  for (auto it = closed_events_.begin(); it != closed_events_.end(); ++it) {
    if (elapsed_gt(now_ms, it->close_time_ms, kClosedTtlMs)) {
      add_trace(result.dispatch, *it, "cleanup_closed", now_ms, current_tick);
      result.cleaned_events.push_back(*it);
      closed_events_.erase(it);
      break;
    }
  }
  return result;
}

/**
 * @brief 获取事件类型的字符串名称
 * @param type 事件类型枚举值
 * @return 对应的事件类型名称字符串
 */
std::string LegacyEventManager::type_name(LegacyEventType type) const {
  switch (type) {
    case LegacyEventType::stone_mine:
      return "stone_mine";
    case LegacyEventType::digout_zombi:
      return "digout_zombi";
    case LegacyEventType::pile_stones:
      return "pile_stones";
    case LegacyEventType::holy_curtain:
      return "holy_curtain";
    case LegacyEventType::fire_burn:
      return "fire_burn";
    case LegacyEventType::sculp_piece:
      return "sculp_piece";
  }
  return "unknown";
}

/**
 * @brief 添加事件追踪记录
 * @details 向 RuntimeDispatch 中添加一条追踪信息，记录事件生命周期中的操作。
 * @param dispatch 运行时分发信息
 * @param event 事件记录
 * @param action 操作描述
 * @param now_ms 当前时间戳
 * @param current_tick 当前计时器滴答数
 * @param success 操作是否成功
 */
void LegacyEventManager::add_trace(RuntimeDispatch& dispatch, const LegacyEventRecord& event,
                                   std::string action, std::uint64_t now_ms,
                                   std::uint64_t current_tick, bool success) const {
  dispatch.legacy_traces.push_back(LegacyRuntimeTrace{
      "LegacyEventManager",
      std::move(action),
      event.map_id,
      type_name(event.type),
      event.id,
      now_ms,
      current_tick,
      0,
      0,
      0,
      0,
      {},
      std::to_string(event.x) + "," + std::to_string(event.y),
      0,
      0,
      event.event_param,
      0,
      success});
}

/**
 * @brief 添加神圣帷幕组追踪记录
 * @param dispatch 运行时分发信息
 * @param group 神圣帷幕组
 * @param action 操作描述
 * @param now_ms 当前时间戳
 * @param current_tick 当前计时器滴答数
 * @param success 操作是否成功
 */
void LegacyEventManager::add_group_trace(RuntimeDispatch& dispatch,
                                         const LegacyHolyCurtainGroup& group,
                                         std::string action, std::uint64_t now_ms,
                                         std::uint64_t current_tick, bool success) const {
  dispatch.legacy_traces.push_back(LegacyRuntimeTrace{
      "LegacyEventManager",
      std::move(action),
      group.map_id,
      "holy_curtain_group",
      group.id,
      now_ms,
      current_tick,
      0,
      0,
      0,
      0,
      {},
      std::to_string(group.seized_actor_ids.size()),
      0,
      0,
      static_cast<std::int32_t>(group.event_ids.size()),
      0,
      success});
}

/**
 * @brief 关闭指定索引的活跃事件
 * @details 将事件从 active_events_ 移动到 closed_events_ 列表：
 *          1. 标记为非活跃并记录关闭时间
 *          2. 添加到 result.closed_events 和 closed_events_
 *          3. 从 active_events_ 中移除
 * @param index 事件在 active_events_ 中的索引
 * @param result 事件运行结果（用于输出关闭事件）
 * @param now_ms 当前时间戳
 * @param current_tick 当前计时器滴答数
 */
void LegacyEventManager::close_event_at(std::size_t index, LegacyEventManagerRun& result,
                                        std::uint64_t now_ms,
                                        std::uint64_t current_tick) {
  auto event = active_events_[index];
  event.active = false;
  event.closed = true;
  event.close_time_ms = now_ms;
  add_trace(result.dispatch, event, "close", now_ms, current_tick);
  result.closed_events.push_back(event);
  closed_events_.push_back(event);
  active_events_.erase(active_events_.begin() + static_cast<std::ptrdiff_t>(index));
}

/**
 * @brief 关闭神圣帷幕组下的所有事件
 * @details 遍历组中记录的所有事件ID，在 active_events_ 中查找并依次关闭。
 * @param group 神圣帷幕组
 * @param result 事件运行结果（用于输出关闭事件）
 * @param now_ms 当前时间戳
 * @param current_tick 当前计时器滴答数
 */
void LegacyEventManager::close_holy_group_events(LegacyHolyCurtainGroup& group,
                                                 LegacyEventManagerRun& result,
                                                 std::uint64_t now_ms,
                                                 std::uint64_t current_tick) {
  for (const auto event_id : group.event_ids) {
    for (std::size_t index = 0; index < active_events_.size(); ++index) {
      if (active_events_[index].id == event_id) {
        close_event_at(index, result, now_ms, current_tick);
        break;
      }
    }
  }
}

}  // namespace mir2
