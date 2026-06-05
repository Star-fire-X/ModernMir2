#pragma once

// Implementation detail for map_actor.cpp: door, gate, and map movement members.
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

void MapActor::set_castle_door_wall_state(std::int32_t x, std::int32_t y, bool open) {
  constexpr std::array<std::pair<std::int32_t, std::int32_t>, 9> kDoorWallTiles{{
      {0, 0},
      {0, -1},
      {0, -2},
      {1, -1},
      {1, -2},
      {-1, 0},
      {-2, 0},
      {-1, -1},
      {-1, 1},
  }};
  constexpr std::array<std::pair<std::int32_t, std::int32_t>, 3> kOpenCarveBlockedTiles{{
      {0, -2},
      {1, -1},
      {1, -2},
  }};

  for (const auto& [dx, dy] : kDoorWallTiles) {
    const CellKey key{x + dx, y + dy};
    if (!open) {
      ++castle_door_wall_block_counts_[key];
    }
    environment_.set_runtime_can_move(key.first, key.second, open);
  }
  if (!open) {
    return;
  }
  for (const auto& [dx, dy] : kOpenCarveBlockedTiles) {
    environment_.set_runtime_can_move(x + dx, y + dy, false);
  }
}

void MapActor::clear_castle_door_wall_state(std::int32_t x, std::int32_t y) {
  constexpr std::array<std::pair<std::int32_t, std::int32_t>, 9> kDoorWallTiles{{
      {0, 0},
      {0, -1},
      {0, -2},
      {1, -1},
      {1, -2},
      {-1, 0},
      {-2, 0},
      {-1, -1},
      {-1, 1},
  }};
  for (const auto& [dx, dy] : kDoorWallTiles) {
    const CellKey key{x + dx, y + dy};
    auto count_it = castle_door_wall_block_counts_.find(key);
    if (count_it != castle_door_wall_block_counts_.end()) {
      --count_it->second;
      if (count_it->second > 0) {
        continue;
      }
      castle_door_wall_block_counts_.erase(count_it);
    }
    environment_.clear_runtime_can_move(key.first, key.second);
  }
}

bool MapActor::has_event_at(std::int32_t x, std::int32_t y, LegacyEventType type) const {
  for (const auto& [_, event] : event_objects_) {
    if (event.x != x || event.y != y) {
      continue;
    }
    if (event.type == type) {
      return true;
    }
  }
  return false;
}

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
    return false;
  }
  if (rule.width > 0 && rule.height > 0) {
    return gate.target_x >= 0 && gate.target_y >= 0 &&
           gate.target_x < rule.width && gate.target_y < rule.height;
  }
  return true;
}

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

bool MapActor::try_gate_transfer(Player& player, RuntimeDispatch& dispatch,
                                 std::uint64_t current_tick, std::uint64_t now_ms) {
  const auto* gate = environment_.gate_at(player.x(), player.y());
  if (gate == nullptr || gate->gate.target_map_id.empty()) {
    return false;
  }

  if (gate->gate.require_doors_open &&
      !environment_.around_door_opened(player.x(), player.y())) {
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

std::optional<std::pair<std::int32_t, std::int32_t>>
MapActor::random_item_scroll_target(RuntimeDispatch& dispatch, const Player& player,
                                    std::uint64_t current_tick, std::uint64_t now_ms) {
  const auto width = movement_width();
  const auto height = movement_height();
  if (width <= 0 || height <= 0) {
    return std::nullopt;
  }
  const auto edge_y = height < 150 ? (height < 30 ? 2 : 20) : 50;
  auto x = edge_y + legacy_random_value(dispatch, "LegacyItem", "random_scroll_x",
                                        std::max(1, width - edge_y - 1), player.id(), 0,
                                        "eat_item", now_ms, current_tick);
  auto y = edge_y + legacy_random_value(dispatch, "LegacyItem", "random_scroll_y",
                                        std::max(1, height - edge_y - 1), player.id(), 0,
                                        "eat_item", now_ms, current_tick);
  const auto step = width < 80 ? 3 : 10;
  const auto edge = height < 150 ? (height < 50 ? 2 : 15) : 50;
  for (std::int32_t attempt = 0; attempt <= 200; ++attempt) {
    if (environment_.can_walk(x, y, true)) {
      return std::pair{x, y};
    }
    if (x < width - edge - 1) {
      x += step;
    } else {
      x = legacy_random_value(dispatch, "LegacyItem", "random_scroll_retry_x", width,
                              player.id(), 0, "eat_item", now_ms, current_tick);
      if (y < height - edge - 1) {
        y += step;
      } else {
        y = legacy_random_value(dispatch, "LegacyItem", "random_scroll_retry_y", height,
                                player.id(), 0, "eat_item", now_ms, current_tick);
      }
    }
  }
  return std::nullopt;
}

bool MapActor::try_item_map_move(Player& player, std::string target_map_id,
                                 std::int32_t target_x, std::int32_t target_y,
                                 RuntimeDispatch& dispatch, std::uint64_t current_tick,
                                 std::uint64_t now_ms, bool send_space_move_packets) {
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
    force_refresh_after_same_map_transfer(
        player, old_x, old_y, dispatch, now_ms,
        send_space_move_packets ? kSmSpaceMoveHide : 0,
        send_space_move_packets ? kSmSpaceMoveShow : 0);
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

