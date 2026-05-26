#pragma once

// Implementation detail for map_actor.cpp: visibility synchronization members.
std::vector<std::uint64_t> MapActor::ordered_player_ids() const {
  std::vector<std::uint64_t> actor_ids;
  for (const auto& [object_id, object] : objects_) {
    if (as_player(object.get()) != nullptr) {
      actor_ids.push_back(object_id);
    }
  }
  std::sort(actor_ids.begin(), actor_ids.end(), [&](std::uint64_t lhs, std::uint64_t rhs) {
    const auto* left = objects_.at(lhs).get();
    const auto* right = objects_.at(rhs).get();
    if (left->x() != right->x()) {
      return left->x() < right->x();
    }
    if (left->y() != right->y()) {
      return left->y() < right->y();
    }
    const auto* cell = environment_.cell(left->x(), left->y());
    auto rank = [&](std::uint64_t actor_id) {
      if (cell == nullptr) {
        return std::numeric_limits<std::size_t>::max();
      }
      for (std::size_t index = 0; index < cell->obj_list.size(); ++index) {
        if (cell->obj_list[index].shape == LegacyMapObjectShape::moving_object &&
            cell->obj_list[index].object_id == actor_id) {
          return index;
        }
      }
      return std::numeric_limits<std::size_t>::max();
    };
    const auto left_rank = rank(lhs);
    const auto right_rank = rank(rhs);
    return left_rank != right_rank ? left_rank < right_rank : lhs < rhs;
  });
  return actor_ids;
}

std::vector<std::uint64_t> MapActor::ordered_visible_actor_ids(
    const Player& player) const {
  std::vector<std::uint64_t> actor_ids;
  for (std::int32_t x = player.x() - kLegacyViewRange; x <= player.x() + kLegacyViewRange; ++x) {
    for (std::int32_t y = player.y() - kLegacyViewRange; y <= player.y() + kLegacyViewRange; ++y) {
      const auto* cell = environment_.cell(x, y);
      if (cell == nullptr) {
        continue;
      }
      for (const auto& entry : cell->obj_list) {
        if (entry.shape != LegacyMapObjectShape::moving_object) {
          continue;
        }
        const auto object_it = objects_.find(entry.object_id);
        if (object_it == objects_.end() ||
            !is_legacy_visible_to(player, *object_it->second) ||
            std::find(actor_ids.begin(), actor_ids.end(), entry.object_id) != actor_ids.end()) {
          continue;
        }
        actor_ids.push_back(entry.object_id);
      }
    }
  }
  return actor_ids;
}

std::vector<std::uint64_t> MapActor::ordered_visible_item_ids(
    const Player& player) const {
  std::vector<std::uint64_t> item_ids;
  for (std::int32_t x = player.x() - kLegacyViewRange; x <= player.x() + kLegacyViewRange; ++x) {
    for (std::int32_t y = player.y() - kLegacyViewRange; y <= player.y() + kLegacyViewRange; ++y) {
      const auto* cell = environment_.cell(x, y);
      if (cell == nullptr) {
        continue;
      }
      for (const auto& entry : cell->obj_list) {
        if (entry.shape != LegacyMapObjectShape::item_object) {
          continue;
        }
        const auto item_it = ground_items_.find(entry.object_id);
        if (item_it == ground_items_.end() || !in_legacy_view_range(player, item_it->second) ||
            std::find(item_ids.begin(), item_ids.end(), entry.object_id) != item_ids.end()) {
          continue;
        }
        item_ids.push_back(entry.object_id);
      }
    }
  }
  return item_ids;
}

void MapActor::sync_player_visibility(Player& player, RuntimeDispatch& dispatch, bool force) {
  auto& visibility = visibility_[player.id()];
  std::unordered_set<std::uint64_t> current_actors;
  for (const auto object_id : ordered_visible_actor_ids(player)) {
    current_actors.insert(object_id);
    if (force || !visibility.actors.contains(object_id)) {
      const auto object_it = objects_.find(object_id);
      if (object_it == objects_.end()) {
        continue;
      }
      queue_packet(dispatch, player.session_id(),
                   make_turn_like_packet(player.session_id(), kSmTurn, *object_it->second, true));
    }
  }

  std::vector<std::uint64_t> stale_actors;
  for (const auto actor_id : visibility.actors) {
    if (!current_actors.contains(actor_id)) {
      stale_actors.push_back(actor_id);
    }
  }
  std::sort(stale_actors.begin(), stale_actors.end());
  for (const auto actor_id : stale_actors) {
    queue_packet(dispatch, player.session_id(),
                 make_disappear_packet(player.session_id(), actor_id));
  }
  visibility.actors = std::move(current_actors);

  std::unordered_set<std::uint64_t> current_items;
  for (const auto item_id : ordered_visible_item_ids(player)) {
    current_items.insert(item_id);
    if (force || !visibility.items.contains(item_id)) {
      const auto item_it = ground_items_.find(item_id);
      if (item_it == ground_items_.end()) {
        continue;
      }
      queue_packet(dispatch, player.session_id(),
                   make_item_show_packet(player.session_id(), item_it->second));
    }
  }
  std::vector<std::uint64_t> stale_items;
  for (const auto item_id : visibility.items) {
    if (!current_items.contains(item_id)) {
      stale_items.push_back(item_id);
    }
  }
  std::sort(stale_items.begin(), stale_items.end());
  for (const auto item_id : stale_items) {
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
  for (const auto actor_id : ordered_player_ids()) {
    const auto object_it = objects_.find(actor_id);
    if (object_it != objects_.end()) {
      auto* player = as_player(object_it->second.get());
      if (player == nullptr) {
        continue;
      }
      sync_player_visibility(*player, dispatch, false);
    }
  }
}

void MapActor::sync_visibility_after_actor_move(const GameObject& actor, std::int32_t old_x,
                                                std::int32_t old_y, std::int32_t new_x,
                                                std::int32_t new_y,
                                                RuntimeDispatch& dispatch) {
  for (const auto actor_id : ordered_player_ids()) {
    const auto object_it = objects_.find(actor_id);
    if (object_it == objects_.end()) {
      continue;
    }
    auto* watcher = as_player(object_it->second.get());
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
  for (const auto actor_id : ordered_player_ids()) {
    const auto object_it = objects_.find(actor_id);
    if (object_it == objects_.end()) {
      continue;
    }
    auto* watcher = as_player(object_it->second.get());
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
  for (const auto actor_id : ordered_player_ids()) {
    const auto object_it = objects_.find(actor_id);
    if (object_it == objects_.end()) {
      continue;
    }
    auto* watcher = as_player(object_it->second.get());
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
  for (const auto watcher_id : ordered_player_ids()) {
    if (watcher_id == actor_id) {
      continue;
    }
    auto visibility_it = visibility_.find(watcher_id);
    if (visibility_it == visibility_.end()) {
      continue;
    }
    auto& visibility = visibility_it->second;
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
  for (const auto watcher_id : ordered_player_ids()) {
    auto visibility_it = visibility_.find(watcher_id);
    if (visibility_it == visibility_.end()) {
      continue;
    }
    auto& visibility = visibility_it->second;
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

