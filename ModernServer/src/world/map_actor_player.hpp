/**
 * @file map_actor_player.hpp
 * @brief 玩家生命周期和状态管理的 MapActor 成员实现
 * @details 该文件是 map_actor.cpp 的实现细节部分，包含以下核心功能：
 *          - dispatch_legacy_run_notice: 玩家运行通知分发
 *          - dispatch_legacy_initialize: 玩家初始化（出生物品、登录序列）
 *          - dispatch_legacy_close: 玩家关闭清理
 *          - legacy_operate_player_running: 玩家每帧运行处理
 *          - handle_player_status_effects: 玩家状态效果处理
 *          - trace_player_operate: 玩家操作追踪
 *
 *          这些函数实现了从玩家连接到断开连接的完整生命周期管理。
 */

#pragma once

// Implementation detail for map_actor.cpp: player lifecycle and status members.

/**
 * @brief 分发玩家运行通知
 * @param player 目标玩家对象
 * @param dispatch 运行时调度输出
 * @param now_ms 当前系统时间（毫秒）
 * @details 当玩家处于 notice_pending 状态时，发送审计事件并标记通知已完成。
 *          这是玩家连接后的第一个状态转换步骤。
 */
void MapActor::dispatch_legacy_run_notice(Player& player, RuntimeDispatch& dispatch,
                                          std::uint64_t now_ms) {
  if (player.legacy_state() != LegacyPlayerState::notice_pending) {
    return;
  }
  dispatch.audit_events.push_back(
      AuditEvent{"world.run_notice", player.character().account_id + ":" +
                                         player.character().character_name,
                 config_.id});
  player.mark_legacy_notice_done(now_ms);
}

/**
 * @brief 执行玩家初始化流程
 * @param player 目标玩家对象
 * @param dispatch 运行时调度输出
 * @param now_ms 当前系统时间（毫秒）
 * @details 当玩家处于 initialize_pending 状态时执行：
 *          1. 刷新派生状态（装备效果、Buff 等）
 *          2. 设置安全区状态
 *          3. 检查是否为首次登录，如果是则发放出生物品（蜡烛、金创药、木剑、布衣）
 *          4. 发送登录序列包（新地图、登录、用户名、区域状态、地图描述、能力值、装备、魔法）
 *          5. 同步玩家可见性
 *          6. 发送审计事件
 * @note 出生物品仅在角色经验为 0 且背包和装备栏均为空时发放
 */
void MapActor::dispatch_legacy_initialize(Player& player, RuntimeDispatch& dispatch,
                                          std::uint64_t now_ms) {
  if (player.legacy_state() != LegacyPlayerState::initialize_pending) {
    return;
  }

  player.refresh_derived_state(item_configs_);
  player.set_in_safe_zone(is_safe_zone(config_, player.x(), player.y()));

  if (!player.character().birth_items_granted) {
    bool starter_items_changed = false;
    bool bag_empty = true;
    for (const auto& item : player.character().bag_items) {
      if (item.index != 0) {
        bag_empty = false;
        break;
      }
    }
    bool equipped_empty = true;
    for (const auto& item : player.character().equipped_items) {
      if (item.index != 0) {
        equipped_empty = false;
        break;
      }
    }
    if (player.character().ability.exp == 0 && bag_empty && equipped_empty) {
      const auto grant_item = [&](std::string_view item_name) {
        const auto* item_config = find_item_config_by_name_or_id(item_configs_, item_name);
        if (item_config == nullptr) {
          return;
        }
        LegacyUserItem item;
        item.make_index = allocate_make_index();
        item.index = static_cast<std::uint16_t>(std::clamp(item_config->id, 0, 65535));
        item.dura_max = static_cast<std::uint16_t>(
            std::clamp(item_config->dura_max > 0 ? item_config->dura_max : 1000, 0, 65535));
        item.dura = item.dura_max;
        if (player.can_add_bag_item(item, item_configs_) && player.has_free_bag_slot()) {
          static_cast<void>(player.add_bag_item(item));
          starter_items_changed = true;
          queue_packet(dispatch, player.session_id(),
                       make_add_item_packet(player.session_id(), player.id(), item, item_configs_));
        }
      };
      grant_item("蜡烛");
      grant_item("金创药(小量)");
      grant_item("木剑");
      if (player.character().sex == 0) {
        grant_item("布衣(男)");
      } else {
        grant_item("布衣(女)");
      }
    }
    if (starter_items_changed) {
      player.refresh_derived_state(item_configs_);
    }
    player.mark_birth_items_granted();
    queue_save_character(dispatch, player);
  }

  dispatch_login_sequence(dispatch, player, config_, item_configs_, magic_configs_,
                          area_state_mask(config_, player.x(), player.y()));

  sync_player_visibility(player, dispatch, true, now_ms);
  sync_all_player_visibility(dispatch, now_ms);

  dispatch.audit_events.push_back(
      AuditEvent{"world.initialize", player.character().account_id + ":" +
                                         player.character().character_name,
                 config_.id});
  player.mark_legacy_initialize_done(now_ms);
}

/**
 * @brief 执行玩家关闭清理
 * @param player 目标玩家对象
 * @param dispatch 运行时调度输出
 * @details 当玩家进入 ghost 状态时执行：
 *          1. 取消交易
 *          2. 保存角色数据
 *          3. 释放宠物奴隶
 *          4. 发送强制断开连接
 *          5. 从地图环境中删除
 *          6. 标记关闭状态并从对象映射中移除
 */
void MapActor::dispatch_legacy_close(Player& player, RuntimeDispatch& dispatch) {
  const auto actor_id = player.id();
  const auto now_ms = player.legacy_ghost_time_ms() != 0 ? player.legacy_ghost_time_ms()
                                                         : static_cast<std::uint64_t>(tick_count_ms());
  cancel_trade_for(actor_id, dispatch, true);
  queue_save_player_character(dispatch, player, now_ms);
  detach_owned_slaves(player, dispatch, now_ms, true);
  queue_force_disconnect(dispatch, player.session_id(), "legacy_player_closed");
  static_cast<void>(environment_.delete_from_map(player.x(), player.y(),
                                                 LegacyMapObjectShape::moving_object,
                                                 player.id()));
  player.mark_legacy_closed();
  objects_.erase(actor_id);
}

/**
 * @brief 记录玩家操作追踪信息
 * @param dispatch 运行时调度输出
 * @param player 目标玩家
 * @param action 操作名称
 * @param current_tick 当前逻辑 tick
 * @param now_ms 当前系统时间（毫秒）
 * @param success 操作是否成功
 * @param value 关联的数值
 * @param label 文本标签描述
 */
void MapActor::trace_player_operate(RuntimeDispatch& dispatch, const Player& player,
                                    std::string action, std::uint64_t current_tick,
                                    std::uint64_t now_ms, bool success,
                                    std::int32_t value, std::string label) const {
  LegacyRuntimeTrace trace;
  trace.stage = "PlayerOperate";
  trace.action = std::move(action);
  trace.map_id = config_.id;
  trace.object_name = player.name();
  trace.actor_id = player.id();
  trace.now_ms = now_ms;
  trace.current_tick = current_tick;
  trace.label = std::move(label);
  trace.value = value;
  trace.success = success;
  dispatch.legacy_traces.push_back(std::move(trace));
}

/**
 * @brief 处理玩家的 HP/MP 自动恢复 tick
 * @param player 目标玩家
 * @param dispatch 运行时调度输出
 * @param current_tick 当前逻辑 tick
 * @details 调用玩家的 tick_legacy_health_spell 进行 HP/MP 恢复，
 *          如果有变化则发送健康值变更包。
 */
void MapActor::handle_player_health_spell_tick(Player& player, RuntimeDispatch& dispatch,
                                               std::uint64_t current_tick) {
  const auto tick_result = player.tick_legacy_health_spell(current_tick);
  if (!tick_result.changed) {
    return;
  }
  queue_packet(dispatch, player.session_id(),
               make_health_spell_changed_packet(player.session_id(), player));
}

/**
 * @brief 处理玩家的状态效果（Buff/Debuff）tick
 * @param player 目标玩家
 * @param dispatch 运行时调度输出
 * @param current_tick 当前逻辑 tick
 * @details 处理状态效果的主逻辑：
 *          1. 调用 tick_status_effects 获取状态变更
 *          2. 如果有伤害，尝试复活（使用复活戒指）
 *          3. 如果死亡，执行死亡结算
 *          4. 广播伤害/受击包
 *          5. 处理护盾破碎/消失通知
 *          6. 广播死亡包
 * @note 尝试复活使用 try_legacy_revival 检查复活戒指
 */
void MapActor::handle_player_status_effects(Player& player, RuntimeDispatch& dispatch,
                                            std::uint64_t current_tick) {
  const auto tick_result = player.tick_status_effects(current_tick);
  if (tick_result.damage <= 0 && tick_result.heal <= 0 && tick_result.absorbed_damage <= 0 &&
      !tick_result.shield_expired && !tick_result.ability_changed &&
      !tick_result.legacy_status_changed) {
    return;
  }

  auto died = false;
  if (tick_result.damage > 0) {
    died = player.is_dead();
    if (died) {
      died = !try_legacy_revival(
          player, dispatch, current_tick,
          current_tick * static_cast<std::uint64_t>(std::max<std::uint32_t>(budgets_.tick_ms, 1)));
    }
    if (died) {
      const auto now_ms = current_tick *
                          static_cast<std::uint64_t>(
                              std::max<std::uint32_t>(budgets_.tick_ms, 1));
      const auto death_clear = player.mark_dead(now_ms);
      dispatch_player_status_tick_result(player, death_clear, dispatch, false);
      static_cast<void>(settle_player_death(player, dispatch, current_tick, now_ms));
    }
    if (!died) {
      for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
        if (watcher.id() != player.id() && !is_legacy_visible_to(watcher, player)) {
          return;
        }
        queue_packet(dispatch, watcher.session_id(),
                     make_struck_packet(watcher.session_id(), player,
                                        tick_result.source_actor_id, tick_result.damage, true));
      });
    }
  }

  dispatch_player_status_tick_result(player, tick_result, dispatch, true);
  if (tick_result.shield_broken) {
    notify_player_and_watchers(dispatch, player, make_shield_break_self_notice(tick_result.shield_name),
                               make_shield_break_watcher_notice(player, tick_result.shield_name));
  }
  if (tick_result.shield_expired) {
    notify_player_and_watchers(dispatch, player, make_shield_fade_self_notice(tick_result.shield_name),
                               make_shield_fade_watcher_notice(player, tick_result.shield_name));
  }
  if (died) {
    for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
      if (watcher.id() != player.id() && !is_legacy_visible_to(watcher, player)) {
        return;
      }
      queue_packet(dispatch, watcher.session_id(),
                   make_death_packet(watcher.session_id(), player, watcher.id() == player.id()));
    });
  }
}

/**
 * @brief 执行玩家每帧运行处理
 * @param actor_id 玩家角色 ID
 * @param player 玩家对象引用
 * @param dispatch 运行时调度输出
 * @param current_tick 当前逻辑 tick
 * @param now_ms 当前系统时间（毫秒）
 * @param persistence_overloaded 持久化层是否过载
 * @param player_input_budget_per_tick 每帧玩家输入处理预算
 * @details 每帧运行处理包含以下步骤：
 *          1. 检查玩家是否到期（legacy_due）
 *          2. 同步安全区状态
 *          3. 同步玩家可见性
 *          4. 处理 HP/MP 恢复
 *          5. 处理状态效果
 *          6. 消费玩家输入队列中的消息（受预算限制）
 *          7. 检查尸体过期
 *          8. 定期自动保存（每 15 分钟）
 */
void MapActor::legacy_operate_player_running(std::uint64_t actor_id, Player& player,
                                             RuntimeDispatch& dispatch,
                                             std::uint64_t current_tick,
                                             std::uint64_t now_ms,
                                             bool persistence_overloaded,
                                             std::size_t player_input_budget_per_tick) {
  if (!player.legacy_due(now_ms)) {
    return;
  }

  trace_player_operate(dispatch, player, "pre_periodic", current_tick, now_ms);
  sync_area_state(dispatch, config_, player);
  sync_player_visibility(player, dispatch, false, now_ms);

  trace_player_operate(dispatch, player, "operate_timers", current_tick, now_ms);

  trace_player_operate(dispatch, player, "health_spell", current_tick, now_ms);
  handle_player_health_spell_tick(player, dispatch, current_tick);

  trace_player_operate(dispatch, player, "status", current_tick, now_ms);
  handle_player_status_effects(player, dispatch, current_tick);

  trace_player_operate(dispatch, player, "messages", current_tick, now_ms,
                       player.legacy_has_commands(),
                       static_cast<std::int32_t>(player.legacy_inbox_size()));
  std::size_t processed_messages = 0;
  while (player_input_budget_per_tick == 0 ||
         processed_messages < player_input_budget_per_tick) {
    auto command = player.pop_legacy_command();
    if (!command.has_value()) {
      break;
    }
    ++processed_messages;
    handle_mail(command->mail, dispatch, current_tick, now_ms, true);
    auto* current_player = find_player(actor_id);
    if (current_player == nullptr) {
      return;
    }
  }

  auto* current_player = find_player(actor_id);
  if (current_player == nullptr) {
    return;
  }
  if (current_player->legacy_has_commands()) {
    trace_player_operate(dispatch, *current_player, "messages_budget_exhausted", current_tick,
                         now_ms, false,
                         static_cast<std::int32_t>(current_player->legacy_inbox_size()));
  }
  trace_player_operate(dispatch, *current_player, "post_operate", current_tick, now_ms);
  sync_player_visibility(*current_player, dispatch, false, now_ms);

  if (current_player->is_dead() && current_player->death_time_ms() != 0 &&
      now_ms > current_player->death_time_ms() + kPlayerCorpseMs) {
    static_cast<void>(environment_.delete_from_map(current_player->x(), current_player->y(),
                                                   LegacyMapObjectShape::moving_object,
                                                   current_player->id()));
    current_player->mark_legacy_ghost(now_ms);
    queue_save_player_character(dispatch, *current_player, now_ms);
    return;
  }

  constexpr std::uint64_t kLegacyAutoSaveMs = 15ULL * 60ULL * 1000ULL;
  if (!persistence_overloaded &&
      now_ms > current_player->legacy_last_save_time_ms() + kLegacyAutoSaveMs) {
    queue_save_player_character(dispatch, *current_player, now_ms);
    current_player->mark_legacy_autosaved(now_ms);
  }
  current_player->mark_legacy_running_time(now_ms);
}
