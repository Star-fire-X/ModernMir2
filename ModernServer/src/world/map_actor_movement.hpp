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

  auto snapshot = player.snapshot();
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

  queue_packet(dispatch, player.session_id(), make_clear_objects_packet(player.session_id()));
  queue_packet(dispatch, player.session_id(),
               make_change_map_packet(player.session_id(), snapshot.map_id));
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

bool MapActor::try_item_map_move(Player& player, std::string target_map_id,
                                 std::int32_t target_x, std::int32_t target_y,
                                 RuntimeDispatch& dispatch, std::uint64_t current_tick,
                                 std::uint64_t now_ms) {
  if (target_map_id.empty()) {
    target_map_id = config_.id;
  }

  auto snapshot = player.snapshot();
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

  queue_packet(dispatch, player.session_id(), make_clear_objects_packet(player.session_id()));
  queue_packet(dispatch, player.session_id(),
               make_change_map_packet(player.session_id(), snapshot.map_id));
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

