/**
 * @file map_actor_movement.hpp
 * @brief 门、传送门和地图移动成员的 MapActor 实现细节
 * @details 该文件是 map_actor.cpp 的实现细节部分，包含以下核心功能：
 *          - broadcast_open_doors/broadcast_close_doors: 门开关广播
 *          - has_event_at: 检查指定位置是否存在特定类型事件
 *          - target_map_can_enter: 检查目标地图是否可进入
 *          - target_entry_allowed: 检查玩家是否满足进入条件（等级、任务、标志位）
 *          - try_gate_transfer: 尝试传送门转移（同地图和跨地图）
 *          - random_item_scroll_target: 随机卷轴传送目标位置
 *          - try_item_map_move: 尝试物品触发的跨地图移动
 *
 *          这些函数实现了玩家在地图中的移动逻辑，包括门交互、传送门转移、
 *          以及物品触发的空间移动。
 */

#pragma once

// Implementation detail for map_actor.cpp: door, gate, and map movement members.

/**
 * @brief 广播开门事件给所有可见玩家
 * @param tiles 需要打开的门坐标列表
 * @param dispatch 运行时调度输出
 * @details 遍历所有门坐标，对每个门坐标向视野范围内的所有玩家
 *          广播开门包。用于传送门自动开门和物品触发开门场景。
 */
void MapActor::broadcast_open_doors(
    const std::vector<std::pair<std::int32_t, std::int32_t>>& tiles,
    RuntimeDispatch& dispatch) {
  for (const auto& [x, y] : tiles) {
    for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
      if (!in_legacy_view_range(watcher.x(), watcher.y(), x, y)) {
        return;
      }
      queue_packet(dispatch, watcher.session_id(),
                   make_open_door_packet(watcher.session_id(), x, y));
    });
  }
}

/**
 * @brief 广播关门事件给所有可见玩家
 * @param tiles 需要关闭的门坐标列表
 * @param dispatch 运行时调度输出
 * @details 遍历所有门坐标，对每个门坐标向视野范围内的所有玩家
 *          广播关门包。用于门超时自动关闭场景。
 */
void MapActor::broadcast_close_doors(
    const std::vector<std::pair<std::int32_t, std::int32_t>>& tiles,
    RuntimeDispatch& dispatch) {
  for (const auto& [x, y] : tiles) {
    for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
      if (!in_legacy_view_range(watcher.x(), watcher.y(), x, y)) {
        return;
      }
      queue_packet(dispatch, watcher.session_id(),
                   make_close_door_packet(watcher.session_id(), x, y));
    });
  }
}

/**
 * @brief 检查指定位置是否存在特定类型的事件对象
 * @param x 目标 X 坐标
 * @param y 目标 Y 坐标
 * @param type 事件类型（如挖僵尸、圣言等）
 * @return true 如果该位置存在指定类型的事件对象
 * @details 遍历 event_objects_ 映射表，检查是否有事件位于指定坐标
 *          且类型与传入类型匹配。用于传送门进入条件检查（need_hole）。
 */
bool MapActor::has_event_at(std::int32_t x, std::int32_t y, LegacyEventType type) const {
  for (const auto& [event_id, xy] : event_objects_) {
    if (xy.first != x || xy.second != y) {
      continue;
    }
    const auto type_it = event_object_types_.find(event_id);
    if (type_it != event_object_types_.end() && type_it->second == type) {
      return true;
    }
  }
  return false;
}

/**
 * @brief 检查目标地图是否允许进入（基于地图配置和传送门状态）
 * @param rule 地图入口规则配置
 * @param gate 传送门状态信息（目标坐标）
 * @return true 如果目标位置可行走且在地图范围内
 * @details 根据规则判断目标位置的可进入性：
 *          - 同地图：检查目标坐标是否在地图边界内且可行走
 *          - 跨地图：解码目标地图文件后检查可行性
 *          - 无地图文件时：仅检查边界范围
 */
bool MapActor::target_map_can_enter(const MapEntryRuleConfig& rule,
                                    const LegacyMapGateState& gate) const {
  if (rule.map_id == config_.id) {
    return environment_.in_bounds(gate.target_x, gate.target_y) &&
           environment_.can_walk(gate.target_x, gate.target_y, true);
  }
  if (!rule.source_map.empty()) {
    if (const auto target_map = legacy::decode_map_file(rule.source_map);
        target_map != nullptr) {
      LegacyMapEnvironment target_environment(rule.width, rule.height, target_map);
      return target_environment.can_walk(gate.target_x, gate.target_y, true);
    }
  }
  if (rule.width > 0 && rule.height > 0) {
    return gate.target_x >= 0 && gate.target_y >= 0 &&
           gate.target_x < rule.width && gate.target_y < rule.height;
  }
  return true;
}

/**
 * @brief 检查玩家是否满足传送门的进入条件
 * @param player 目标玩家
 * @param gate 传送门状态信息
 * @param dispatch 运行时调度输出
 * @param current_tick 当前逻辑 tick
 * @param now_ms 当前系统时间（毫秒）
 * @return true 如果玩家满足所有进入条件
 * @details 依次检查以下条件：
 *          1. 是否需要洞口事件（digout_zombi）
 *          2. 是否满足等级要求
 *          3. 是否需要执行任务脚本（MapEntryQuest）
 *          4. 是否满足任务标志位要求
 *          5. 最后调用 target_map_can_enter 检查目标位置可行性
 * @note 任务脚本执行后可能改变玩家状态，因此执行后重新获取玩家指针
 */
bool MapActor::target_entry_allowed(Player& player, const LegacyMapGateState& gate,
                                    RuntimeDispatch& dispatch,
                                    std::uint64_t current_tick,
                                    std::uint64_t now_ms) {
  const auto player_id = player.id();
  auto* current_player = &player;
  const auto rule_it = map_entry_rules_.find(gate.target_map_id);
  if (rule_it == map_entry_rules_.end()) {
    return map_entry_rules_.empty();
  }
  const auto& rule = rule_it->second;
  if (rule.need_hole &&
      !has_event_at(current_player->x(), current_player->y(), LegacyEventType::digout_zombi)) {
    return false;
  }
  if (rule.need_level > 0 &&
      static_cast<std::int32_t>(current_player->character().ability.level) < rule.need_level) {
    return false;
  }
  if (rule.check_quest.has_value()) {
    const auto& quest = *rule.check_quest;
    if (quest.dialog_sections.empty()) {
      ActorMail trace_mail;
      trace_mail.kind = ActorMailKind::merchant_select;
      trace_mail.map_id = config_.id;
      trace_mail.actor_id = player.id();
      trace_mail.session_id = player.session_id();
      trace_mail.payload = "MapEntryQuest:" + quest.qfile;
      add_legacy_trace(dispatch, "LegacyScript", "map_entry_missing_script", trace_mail,
                       current_tick, now_ms, false, 0, 0, gate.target_map_id);
    } else {
      const auto npc_id = kMapQuestNpcObjectBase + 0x100000ULL +
                          (std::hash<std::string>{}(gate.target_map_id) & 0xffffULL);
      Npc quest_npc(npc_id,
                    quest.qfile.empty() ? std::string("MapEntryQuest") : quest.qfile,
                    config_.id, player.x(), player.y(), "none", {}, quest.dialog_sections);
      ActorMail trace_mail;
      trace_mail.kind = ActorMailKind::merchant_select;
      trace_mail.map_id = config_.id;
      trace_mail.actor_id = player.id();
      trace_mail.session_id = player.session_id();
      trace_mail.target_actor_id = quest_npc.id();
      trace_mail.payload = "MapEntryQuest:" + quest.qfile;
      add_legacy_trace(dispatch, "LegacyScript", "map_entry_checkquest", trace_mail,
                       current_tick, now_ms, true, 0, 0, gate.target_map_id);
      static_cast<void>(legacy_execute_npc_script(player, quest_npc, "@main", dispatch,
                                                 current_tick, now_ms));
      current_player = find_player(player_id);
      if (current_player == nullptr) {
        return false;
      }
    }
  }
  if (rule.need_set_number >= 0 &&
      current_player->quest_mark(rule.need_set_number) != rule.need_set_value) {
    return false;
  }
  return target_map_can_enter(rule, gate);
}

/**
 * @brief 尝试执行传送门转移
 * @param player 目标玩家
 * @param dispatch 运行时调度输出
 * @param current_tick 当前逻辑 tick
 * @param now_ms 当前系统时间（毫秒）
 * @return true 如果传送成功
 * @details 传送流程：
 *          1. 检查玩家当前位置是否有传送门
 *          2. 如果传送门需要开门，尝试自动开门
 *          3. 检查进入条件（target_entry_allowed）
 *          4. 同地图转移：直接移动对象，刷新可见性
 *          5. 跨地图转移：清除当前地图对象，发送跨地图邮件
 * @note 跨地图转移时会断开宠物奴隶关系，释放 Buff
 */
bool MapActor::try_gate_transfer(Player& player, RuntimeDispatch& dispatch,
                                 std::uint64_t current_tick, std::uint64_t now_ms) {
  const auto* gate = environment_.gate_at(player.x(), player.y());
  if (gate == nullptr || gate->gate.target_map_id.empty()) {
    return false;
  }

  if (gate->gate.require_doors_open &&
      !environment_.around_door_opened(player.x(), player.y())) {
    const auto opened = environment_.open_doors_around(player.x(), player.y(), now_ms);
    broadcast_open_doors(opened, dispatch);
    return false;
  }

  if (!target_entry_allowed(player, gate->gate, dispatch, current_tick, now_ms)) {
    return false;
  }

  if (gate->gate.target_map_id != config_.id) {
    const auto leave_clear = player.clear_legacy_buffs_on_leave_map(current_tick);
    dispatch_player_status_tick_result(player, leave_clear, dispatch, false);
  }

  auto snapshot = player.persistent_snapshot();
  snapshot.map_id = gate->gate.target_map_id;
  snapshot.x = gate->gate.target_x;
  snapshot.y = gate->gate.target_y;
  snapshot.dir = player.character().dir;
  snapshot.slaves = snapshot_owned_slaves(player, now_ms);

  if (snapshot.map_id == config_.id) {
    const auto old_x = player.x();
    const auto old_y = player.y();
    if (!environment_.in_bounds(snapshot.x, snapshot.y) ||
        !environment_.can_walk(snapshot.x, snapshot.y, true)) {
      return false;
    }
    if (environment_.move_to_moving_object(old_x, old_y, player.id(), snapshot.x, snapshot.y,
                                           true, now_ms, moving_state_for(player)) != 1) {
      return false;
    }
    cancel_trade_for(player.id(), dispatch, true);
    ActorMail move_mail;
    move_mail.kind = ActorMailKind::move;
    move_mail.map_id = config_.id;
    move_mail.actor_id = player.id();
    move_mail.session_id = player.session_id();
    move_mail.x = snapshot.x;
    move_mail.y = snapshot.y;
    move_mail.dir = snapshot.dir;
    MapContext context;
    context.tick = current_tick;
    context.map_id = config_.id;
    context.dispatch = &dispatch;
    context.items = &item_configs_;
    context.magics = &magic_configs_;
    player.on_mail(move_mail, context);
    force_refresh_after_same_map_transfer(player, old_x, old_y, dispatch, now_ms);
    recall_owned_slaves_to_master(player, dispatch, current_tick, now_ms);
    dispatch.audit_events.push_back(
        AuditEvent{"world.transfer", snapshot.account_id + ":" + snapshot.character_name,
                   snapshot.map_id});
    return true;
  }

  cancel_trade_for(player.id(), dispatch, true);
  snapshot = player.persistent_snapshot();
  snapshot.map_id = gate->gate.target_map_id;
  snapshot.x = gate->gate.target_x;
  snapshot.y = gate->gate.target_y;
  snapshot.dir = player.character().dir;
  snapshot.slaves = snapshot_owned_slaves(player, now_ms);
  ActorMail transfer;
  transfer.kind = ActorMailKind::spawn_player;
  transfer.map_id = snapshot.map_id;
  transfer.actor_id = player.id();
  transfer.session_id = player.session_id();
  transfer.name = snapshot.character_name;
  transfer.x = snapshot.x;
  transfer.y = snapshot.y;
  transfer.dir = snapshot.dir;
  transfer.character = snapshot;
  transfer.legacy_buffs = player.legacy_buffs_for_transfer(current_tick);
  transfer.legacy_name_color = player.legacy_name_color();
  transfer.legacy_spawn_reason = LegacySpawnReason::map_transfer;

  queue_packet(dispatch, player.session_id(), make_clear_objects_packet(player.session_id()));
  queue_packet(dispatch, player.session_id(),
               make_change_map_packet(player.session_id(), snapshot.map_id, snapshot.x,
                                      snapshot.y, 0));
  queue_save_character(dispatch, snapshot);
  detach_owned_slaves(player, dispatch, now_ms, true);
  remove_actor_from_visibility(player.id(), dispatch);
  static_cast<void>(environment_.delete_from_map(player.x(), player.y(),
                                                 LegacyMapObjectShape::moving_object,
                                                 player.id()));
  visibility_.erase(player.id());
  const auto actor_id = player.id();
  objects_.erase(actor_id);
  dispatch.cross_map_mails.push_back(std::move(transfer));
  dispatch.audit_events.push_back(
      AuditEvent{"world.transfer", snapshot.account_id + ":" + snapshot.character_name,
                 snapshot.map_id});
  static_cast<void>(current_tick);
  return true;
}

/**
 * @brief 获取随机卷轴传送的目标位置
 * @param dispatch 运行时调度输出
 * @param player 使用卷轴的玩家
 * @param current_tick 当前逻辑 tick
 * @param now_ms 当前系统时间（毫秒）
 * @return 目标坐标对，如果找不到可行走位置则返回 std::nullopt
 * @details 在地图范围内随机尝试最多 30 次，寻找可行走的位置。
 *          使用地图的 movement_width() 和 movement_height() 确定搜索范围。
 *          随机数通过 legacy_random_value 生成以支持可重现追踪。
 */
std::optional<std::pair<std::int32_t, std::int32_t>>
MapActor::random_item_scroll_target(RuntimeDispatch& dispatch, const Player& player,
                                    std::uint64_t current_tick, std::uint64_t now_ms) {
  const auto width = movement_width();
  const auto height = movement_height();
  if (width <= 0 || height <= 0) {
    return std::nullopt;
  }
  for (std::int32_t attempt = 0; attempt < 30; ++attempt) {
    const auto x = legacy_random_value(dispatch, "LegacyItem", "random_scroll_x", width,
                                       player.id(), 0, "eat_item", now_ms, current_tick);
    const auto y = legacy_random_value(dispatch, "LegacyItem", "random_scroll_y", height,
                                       player.id(), 0, "eat_item", now_ms, current_tick);
    if (environment_.can_walk(x, y, false)) {
      return std::pair{x, y};
    }
  }
  return std::nullopt;
}

/**
 * @brief 尝试执行物品触发的跨地图移动（如传送卷轴、随机卷轴）
 * @param player 目标玩家
 * @param target_map_id 目标地图 ID（空字符串表示当前地图）
 * @param target_x 目标 X 坐标
 * @param target_y 目标 Y 坐标
 * @param dispatch 运行时调度输出
 * @param current_tick 当前逻辑 tick
 * @param now_ms 当前系统时间（毫秒）
 * @return true 如果移动成功
 * @details 和 try_gate_transfer 类似，由物品使用触发：
 *          - 同地图移动：直接移动对象位置
 *          - 跨地图移动：清除当前对象，发送跨地图 spawn 邮件
 * @see try_gate_transfer
 */
bool MapActor::try_item_map_move(Player& player, std::string target_map_id,
                                 std::int32_t target_x, std::int32_t target_y,
                                 RuntimeDispatch& dispatch, std::uint64_t current_tick,
                                 std::uint64_t now_ms) {
  if (target_map_id.empty()) {
    target_map_id = config_.id;
  }

  if (target_map_id != config_.id) {
    const auto leave_clear = player.clear_legacy_buffs_on_leave_map(current_tick);
    dispatch_player_status_tick_result(player, leave_clear, dispatch, false);
  }

  auto snapshot = player.persistent_snapshot();
  snapshot.map_id = target_map_id;
  snapshot.x = target_x;
  snapshot.y = target_y;
  snapshot.dir = player.character().dir;
  snapshot.slaves = snapshot_owned_slaves(player, now_ms);

  if (target_map_id == config_.id) {
    const auto old_x = player.x();
    const auto old_y = player.y();
    if (!environment_.in_bounds(target_x, target_y) ||
        !environment_.can_walk(target_x, target_y, true)) {
      return false;
    }
    if (environment_.move_to_moving_object(player.x(), player.y(), player.id(), target_x,
                                           target_y, true, now_ms,
                                           moving_state_for(player)) != 1) {
      return false;
    }
    cancel_trade_for(player.id(), dispatch, true);
    ActorMail move_mail;
    move_mail.kind = ActorMailKind::move;
    move_mail.map_id = config_.id;
    move_mail.actor_id = player.id();
    move_mail.session_id = player.session_id();
    move_mail.x = target_x;
    move_mail.y = target_y;
    move_mail.dir = snapshot.dir;
    MapContext context;
    context.tick = current_tick;
    context.map_id = config_.id;
    context.dispatch = &dispatch;
    context.items = &item_configs_;
    context.magics = &magic_configs_;
    player.on_mail(move_mail, context);
    force_refresh_after_same_map_transfer(player, old_x, old_y, dispatch, now_ms);
    recall_owned_slaves_to_master(player, dispatch, current_tick, now_ms);
    return true;
  }

  cancel_trade_for(player.id(), dispatch, true);
  snapshot = player.persistent_snapshot();
  snapshot.map_id = target_map_id;
  snapshot.x = target_x;
  snapshot.y = target_y;
  snapshot.dir = player.character().dir;
  snapshot.slaves = snapshot_owned_slaves(player, now_ms);
  ActorMail transfer;
  transfer.kind = ActorMailKind::spawn_player;
  transfer.map_id = snapshot.map_id;
  transfer.actor_id = player.id();
  transfer.session_id = player.session_id();
  transfer.name = snapshot.character_name;
  transfer.x = snapshot.x;
  transfer.y = snapshot.y;
  transfer.dir = snapshot.dir;
  transfer.character = snapshot;
  transfer.legacy_buffs = player.legacy_buffs_for_transfer(current_tick);
  transfer.legacy_name_color = player.legacy_name_color();
  transfer.legacy_spawn_reason = LegacySpawnReason::map_transfer;

  queue_packet(dispatch, player.session_id(), make_clear_objects_packet(player.session_id()));
  queue_packet(dispatch, player.session_id(),
               make_change_map_packet(player.session_id(), snapshot.map_id, snapshot.x,
                                      snapshot.y, 0));
  queue_save_character(dispatch, snapshot);
  detach_owned_slaves(player, dispatch, now_ms, true);
  remove_actor_from_visibility(player.id(), dispatch);
  static_cast<void>(environment_.delete_from_map(player.x(), player.y(),
                                                 LegacyMapObjectShape::moving_object,
                                                 player.id()));
  visibility_.erase(player.id());
  const auto actor_id = player.id();
  objects_.erase(actor_id);
  dispatch.cross_map_mails.push_back(std::move(transfer));
  dispatch.audit_events.push_back(
      AuditEvent{"world.item_transfer", snapshot.account_id + ":" + snapshot.character_name,
                 snapshot.map_id});
  return true;
}
