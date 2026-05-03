#pragma once

// Implementation detail for map_actor.cpp: visibility synchronization members.
void MapActor::sync_player_visibility(Player& player, RuntimeDispatch& dispatch, bool force) {
  auto& visibility = visibility_[player.id()];
  std::unordered_set<std::uint64_t> current_actors;
  for (const auto& [object_id, object] : objects_) {
    if (!is_legacy_visible_to(player, *object)) {
      continue;
    }
    current_actors.insert(object_id);
    if (force || !visibility.actors.contains(object_id)) {
      queue_packet(dispatch, player.session_id(),
                   make_turn_like_packet(player.session_id(), kSmTurn, *object, true));
    }
  }

  for (const auto actor_id : visibility.actors) {
    if (!current_actors.contains(actor_id)) {
      queue_packet(dispatch, player.session_id(),
                   make_disappear_packet(player.session_id(), actor_id));
    }
  }
  visibility.actors = std::move(current_actors);

  std::unordered_set<std::uint64_t> current_items;
  for (const auto& [item_id, item] : ground_items_) {
    if (!in_legacy_view_range(player, item)) {
      continue;
    }
    current_items.insert(item_id);
    if (force || !visibility.items.contains(item_id)) {
      queue_packet(dispatch, player.session_id(), make_item_show_packet(player.session_id(), item));
    }
  }
  for (const auto item_id : visibility.items) {
    if (!current_items.contains(item_id)) {
      if (const auto item_it = ground_items_.find(item_id); item_it != ground_items_.end()) {
        queue_packet(dispatch, player.session_id(),
                     make_item_hide_packet(player.session_id(), item_it->second));
      } else {
        MapActor::GroundItem placeholder;
        placeholder.id = item_id;
        queue_packet(dispatch, player.session_id(),
                     make_item_hide_packet(player.session_id(), placeholder));
      }
    }
  }
  visibility.items = std::move(current_items);

  std::unordered_set<std::uint64_t> current_events;
  for (const auto& [event_id, position] : event_objects_) {
    if (!in_legacy_view_range(player.x(), player.y(), position.first, position.second)) {
      continue;
    }
    current_events.insert(event_id);
  }
  // EventObject wire show/hide is intentionally not emitted until the Delphi
  // packet shape is confirmed; this tracks server-side visibility only.
  visibility.events = std::move(current_events);
}

void MapActor::sync_all_player_visibility(RuntimeDispatch& dispatch) {
  for (auto& [_, object] : objects_) {
    if (auto* player = as_player(object.get()); player != nullptr) {
      sync_player_visibility(*player, dispatch, false);
    }
  }
}

void MapActor::sync_visibility_after_actor_move(const GameObject& actor, std::int32_t old_x,
                                                std::int32_t old_y, std::int32_t new_x,
                                                std::int32_t new_y,
                                                RuntimeDispatch& dispatch) {
  for (auto& [_, object] : objects_) {
    auto* watcher = as_player(object.get());
    if (watcher == nullptr) {
      continue;
    }
    if (watcher->id() == actor.id() ||
        in_legacy_view_range(watcher->x(), watcher->y(), old_x, old_y) ||
        in_legacy_view_range(watcher->x(), watcher->y(), new_x, new_y)) {
      sync_player_visibility(*watcher, dispatch, false);
    }
  }
}

void MapActor::sync_visibility_after_item_change(std::int32_t item_x, std::int32_t item_y,
                                                 RuntimeDispatch& dispatch,
                                                 std::optional<std::uint64_t> refresh_item_id) {
  for (auto& [_, object] : objects_) {
    auto* watcher = as_player(object.get());
    if (watcher == nullptr || !in_legacy_view_range(watcher->x(), watcher->y(), item_x, item_y)) {
      continue;
    }

    const auto* refresh_item =
        refresh_item_id.has_value()
            ? [&]() -> const GroundItem* {
                const auto item_it = ground_items_.find(*refresh_item_id);
                return item_it != ground_items_.end() ? &item_it->second : nullptr;
              }()
            : nullptr;
    const auto had_refresh_item = refresh_item_id.has_value() &&
                                  visibility_[watcher->id()].items.contains(*refresh_item_id);

    sync_player_visibility(*watcher, dispatch, false);

    if (refresh_item != nullptr && had_refresh_item &&
        visibility_[watcher->id()].items.contains(refresh_item->id)) {
      queue_packet(dispatch, watcher->session_id(),
                   make_item_show_packet(watcher->session_id(), *refresh_item));
    }
  }
}

void MapActor::sync_visibility_after_event_change(std::int32_t event_x, std::int32_t event_y,
                                                  RuntimeDispatch& dispatch) {
  for (auto& [_, object] : objects_) {
    auto* watcher = as_player(object.get());
    if (watcher != nullptr &&
        in_legacy_view_range(watcher->x(), watcher->y(), event_x, event_y)) {
      sync_player_visibility(*watcher, dispatch, false);
    }
  }
}

void MapActor::force_refresh_after_same_map_transfer(Player& player, std::int32_t old_x,
                                                     std::int32_t old_y,
                                                     RuntimeDispatch& dispatch,
                                                     std::uint64_t now_ms) {
  queue_packet(dispatch, player.session_id(), make_clear_objects_packet(player.session_id()));
  queue_packet(dispatch, player.session_id(), make_change_map_packet(player.session_id(), config_.id));
  dispatch_login_sequence(dispatch, player, config_, item_configs_, magic_configs_,
                          area_state_mask(config_, player.x(), player.y()));
  sync_player_visibility(player, dispatch, true);
  sync_visibility_after_actor_move(player, old_x, old_y, player.x(), player.y(), dispatch);
  queue_save_character(dispatch, player);
  static_cast<void>(now_ms);
}

void MapActor::remove_actor_from_visibility(std::uint64_t actor_id, RuntimeDispatch& dispatch) {
  for (auto& [watcher_id, visibility] : visibility_) {
    if (watcher_id == actor_id) {
      continue;
    }
    if (visibility.actors.erase(actor_id) == 0) {
      continue;
    }
    if (auto* watcher = find_player(watcher_id); watcher != nullptr) {
      queue_packet(dispatch, watcher->session_id(),
                   make_disappear_packet(watcher->session_id(), actor_id));
    }
  }
  visibility_.erase(actor_id);
}

void MapActor::remove_item_from_visibility(std::uint64_t item_id, RuntimeDispatch& dispatch) {
  for (auto& [watcher_id, visibility] : visibility_) {
    if (visibility.items.erase(item_id) == 0) {
      continue;
    }
    auto* watcher = find_player(watcher_id);
    const auto item_it = ground_items_.find(item_id);
    if (watcher != nullptr && item_it != ground_items_.end()) {
      queue_packet(dispatch, watcher->session_id(),
                   make_item_hide_packet(watcher->session_id(), item_it->second));
    }
  }
}

