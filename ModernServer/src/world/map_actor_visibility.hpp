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

std::vector<std::uint64_t> MapActor::ordered_visible_event_ids(
    const Player& player) const {
  std::vector<std::uint64_t> event_ids;
  for (std::int32_t x = player.x() - kLegacyViewRange; x <= player.x() + kLegacyViewRange; ++x) {
    for (std::int32_t y = player.y() - kLegacyViewRange; y <= player.y() + kLegacyViewRange; ++y) {
      const auto* cell = environment_.cell(x, y);
      if (cell == nullptr) {
        continue;
      }
      for (const auto& entry : cell->obj_list) {
        if (entry.shape != LegacyMapObjectShape::event_object ||
            std::find(event_ids.begin(), event_ids.end(), entry.object_id) != event_ids.end()) {
          continue;
        }
        if (const auto position_it = event_objects_.find(entry.object_id);
            position_it != event_objects_.end() &&
            in_legacy_view_range(player.x(), player.y(), position_it->second.first,
                                 position_it->second.second)) {
          event_ids.push_back(entry.object_id);
        }
      }
    }
  }
  return event_ids;
}

namespace {

void erase_ordered_id(std::vector<std::uint64_t>& ordered_ids, std::uint64_t id) {
  ordered_ids.erase(std::remove(ordered_ids.begin(), ordered_ids.end(), id), ordered_ids.end());
}

void queue_legacy_actor_enter_packet(RuntimeDispatch& dispatch, const Player& watcher,
                                     const GameObject& target) {
  if (const auto* player = as_player(&target); player != nullptr) {
    if (player->is_dead()) {
      queue_packet(dispatch, watcher.session_id(),
                   make_death_packet(watcher.session_id(), target, false));
      return;
    }
  } else if (const auto* monster = as_monster(&target); monster != nullptr) {
    if (monster->is_dead()) {
      queue_packet(dispatch, watcher.session_id(),
                   actor_uses_skeleton_packet(target)
                       ? make_skeleton_packet(watcher.session_id(), target)
                       : make_death_packet(watcher.session_id(), target, false));
      return;
    }
  }
  queue_packet(dispatch, watcher.session_id(),
               make_turn_like_packet(watcher.session_id(), kSmTurn, target, true));
}

}  // namespace

std::vector<std::uint64_t> MapActor::legacy_ref_target_player_ids(const GameObject& origin,
                                                                  std::uint64_t now_ms) {
  auto& cache = legacy_ref_target_cache_[origin.id()];
  if (!cache.watcher_ids.empty() && now_ms <= cache.collected_at_ms + 500) {
    std::vector<std::uint64_t> reused_ids;
    reused_ids.reserve(cache.watcher_ids.size());
    for (const auto watcher_id : cache.watcher_ids) {
      const auto* watcher = find_player(watcher_id);
      if (watcher == nullptr || watcher->map_id() != origin.map_id()) {
        continue;
      }
      if (std::abs(watcher->x() - origin.x()) <= 11 && std::abs(watcher->y() - origin.y()) <= 11) {
        reused_ids.push_back(watcher_id);
      }
    }
    cache.collected_at_ms = now_ms;
    cache.watcher_ids = std::move(reused_ids);
    return cache.watcher_ids;
  }

  std::vector<std::uint64_t> watcher_ids;
  for (std::int32_t x = origin.x() - 12; x <= origin.x() + 12; ++x) {
    for (std::int32_t y = origin.y() - 12; y <= origin.y() + 12; ++y) {
      const auto* cell = environment_.cell(x, y);
      if (cell == nullptr) {
        continue;
      }
      for (const auto& entry : cell->obj_list) {
        if (entry.shape != LegacyMapObjectShape::moving_object ||
            std::find(watcher_ids.begin(), watcher_ids.end(), entry.object_id) != watcher_ids.end()) {
          continue;
        }
        const auto object_it = objects_.find(entry.object_id);
        if (object_it == objects_.end()) {
          continue;
        }
        if (auto* watcher = as_player(object_it->second.get()); watcher != nullptr &&
            !watcher->legacy_ghost()) {
          watcher_ids.push_back(watcher->id());
        }
      }
    }
  }
  cache.collected_at_ms = now_ms;
  cache.watcher_ids = watcher_ids;
  return watcher_ids;
}

void MapActor::sync_player_visibility(Player& player, RuntimeDispatch& dispatch, bool force,
                                      std::uint64_t now_ms) {
  auto& visibility = visibility_[player.id()];
  const auto current_actor_order = ordered_visible_actor_ids(player);
  std::unordered_set<std::uint64_t> current_actors{current_actor_order.begin(),
                                                   current_actor_order.end()};
  for (const auto actor_id : visibility.actor_order) {
    if (current_actors.contains(actor_id)) {
      continue;
    }
    queue_packet(dispatch, player.session_id(),
                 make_disappear_packet(player.session_id(), actor_id));
  }
  for (const auto actor_id : current_actor_order) {
    if (!force && visibility.actors.contains(actor_id)) {
      continue;
    }
    const auto object_it = objects_.find(actor_id);
    if (object_it == objects_.end()) {
      continue;
    }
    queue_legacy_actor_enter_packet(dispatch, player, *object_it->second);
  }
  visibility.actors = std::move(current_actors);
  visibility.actor_order = current_actor_order;

  auto current_item_order = ordered_visible_item_ids(player);
  std::unordered_set<std::uint64_t> current_items{current_item_order.begin(), current_item_order.end()};
  for (const auto item_id : visibility.item_order) {
    if (current_items.contains(item_id)) {
      continue;
    }
    if (const auto delayed_it = visibility.delayed_item_hides_until_ms.find(item_id);
        delayed_it != visibility.delayed_item_hides_until_ms.end() &&
        now_ms <= delayed_it->second) {
      current_items.insert(item_id);
      if (std::find(current_item_order.begin(), current_item_order.end(), item_id) ==
          current_item_order.end()) {
        current_item_order.push_back(item_id);
      }
      continue;
    }
    if (const auto item_it = ground_items_.find(item_id); item_it != ground_items_.end()) {
      queue_packet(dispatch, player.session_id(),
                   make_item_hide_packet(player.session_id(), item_it->second));
    } else {
      MapActor::GroundItem placeholder;
      placeholder.id = item_id;
      queue_packet(dispatch, player.session_id(),
                   make_item_hide_packet(player.session_id(), placeholder));
    }
    visibility.delayed_item_hides_until_ms.erase(item_id);
  }
  for (const auto item_id : current_item_order) {
    if (!force && visibility.items.contains(item_id)) {
      continue;
    }
    const auto item_it = ground_items_.find(item_id);
    if (item_it == ground_items_.end()) {
      continue;
    }
    queue_packet(dispatch, player.session_id(),
                 make_item_show_packet(player.session_id(), item_it->second));
  }
  visibility.items = std::move(current_items);
  visibility.item_order = current_item_order;

  const auto current_event_order = ordered_visible_event_ids(player);
  std::unordered_set<std::uint64_t> current_events{current_event_order.begin(),
                                                   current_event_order.end()};
  for (const auto event_id : visibility.event_order) {
    if (current_events.contains(event_id)) {
      continue;
    }
    std::int32_t event_x = 0;
    std::int32_t event_y = 0;
    if (const auto position_it = event_objects_.find(event_id);
        position_it != event_objects_.end()) {
      event_x = position_it->second.first;
      event_y = position_it->second.second;
    }
    queue_packet(dispatch, player.session_id(),
                 make_hide_event_packet(player.session_id(), event_id, event_x, event_y));
  }
  for (const auto event_id : current_event_order) {
    if (!force && visibility.events.contains(event_id)) {
      continue;
    }
    const auto position_it = event_objects_.find(event_id);
    if (position_it == event_objects_.end()) {
      continue;
    }
    auto type = LegacyEventType::pile_stones;
    if (const auto type_it = event_object_types_.find(event_id); type_it != event_object_types_.end()) {
      type = type_it->second;
    }
    queue_packet(dispatch, player.session_id(),
                 make_show_event_packet(player.session_id(), event_id,
                                        position_it->second.first, position_it->second.second, type));
  }
  visibility.events = std::move(current_events);
  visibility.event_order = current_event_order;
}

void MapActor::sync_all_player_visibility(RuntimeDispatch& dispatch, std::uint64_t now_ms) {
  for (const auto actor_id : ordered_player_ids()) {
    const auto object_it = objects_.find(actor_id);
    if (object_it != objects_.end()) {
      auto* player = as_player(object_it->second.get());
      if (player == nullptr) {
        continue;
      }
      sync_player_visibility(*player, dispatch, false, now_ms);
    }
  }
}

void MapActor::sync_visibility_after_actor_move(const GameObject& actor, std::int32_t old_x,
                                                std::int32_t old_y, std::int32_t new_x,
                                                std::int32_t new_y,
                                                RuntimeDispatch& dispatch,
                                                std::uint64_t now_ms) {
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
      sync_player_visibility(*watcher, dispatch, false, now_ms);
    }
  }
}

void MapActor::sync_visibility_after_item_change(std::int32_t item_x, std::int32_t item_y,
                                                 RuntimeDispatch& dispatch,
                                                 std::uint64_t now_ms,
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

    sync_player_visibility(*watcher, dispatch, false, now_ms);

    if (refresh_item != nullptr && had_refresh_item &&
        visibility_[watcher->id()].items.contains(refresh_item->id)) {
      queue_packet(dispatch, watcher->session_id(),
                   make_item_show_packet(watcher->session_id(), *refresh_item));
    }
  }
}

void MapActor::sync_visibility_after_event_change(std::int32_t event_x, std::int32_t event_y,
                                                  RuntimeDispatch& dispatch,
                                                  std::uint64_t now_ms) {
  for (const auto actor_id : ordered_player_ids()) {
    const auto object_it = objects_.find(actor_id);
    if (object_it == objects_.end()) {
      continue;
    }
    auto* watcher = as_player(object_it->second.get());
    if (watcher != nullptr &&
        in_legacy_view_range(watcher->x(), watcher->y(), event_x, event_y)) {
      sync_player_visibility(*watcher, dispatch, false, now_ms);
    }
  }
}

void MapActor::force_refresh_after_same_map_transfer(Player& player, std::int32_t old_x,
                                                     std::int32_t old_y,
                                                     RuntimeDispatch& dispatch,
                                                     std::uint64_t now_ms,
                                                     std::uint16_t space_move_hide_ident,
                                                     std::uint16_t space_move_show_ident) {
  for_each_player(objects_, [&](std::uint64_t watcher_id, const Player& watcher) {
    if (watcher.id() != player.id() &&
        !in_legacy_view_range(watcher.x(), watcher.y(), old_x, old_y)) {
      return;
    }
    if (space_move_hide_ident == kSmSpaceMoveHide2) {
      queue_packet(dispatch, watcher.session_id(),
                   make_space_move_hide2_packet(watcher.session_id(), player));
    } else {
      queue_packet(dispatch, watcher.session_id(),
                   make_space_move_hide_packet(watcher.session_id(), player));
    }
    if (watcher_id != player.id()) {
      visibility_[watcher_id].actors.erase(player.id());
      erase_ordered_id(visibility_[watcher_id].actor_order, player.id());
    }
  });
  queue_packet(dispatch, player.session_id(), make_clear_objects_packet(player.session_id()));
  queue_packet(dispatch, player.session_id(),
               make_change_map_packet(player.session_id(), config_.id, player.x(), player.y(),
                                      legacy_map_darkness(config_)));
  for_each_player(objects_, [&](std::uint64_t watcher_id, const Player& watcher) {
    if (watcher.id() != player.id() && !is_legacy_visible_to(watcher, player)) {
      return;
    }
    if (space_move_show_ident == kSmSpaceMoveShow2) {
      queue_packet(dispatch, watcher.session_id(),
                   make_space_move_show2_packet(watcher.session_id(), player));
    } else {
      queue_packet(dispatch, watcher.session_id(),
                   make_space_move_show_packet(watcher.session_id(), player));
    }
    if (watcher_id != player.id()) {
      visibility_[watcher_id].actors.insert(player.id());
      if (std::find(visibility_[watcher_id].actor_order.begin(),
                    visibility_[watcher_id].actor_order.end(),
                    player.id()) == visibility_[watcher_id].actor_order.end()) {
        visibility_[watcher_id].actor_order.push_back(player.id());
      }
    }
  });
  sync_player_visibility(player, dispatch, true, now_ms);
  sync_visibility_after_actor_move(player, old_x, old_y, player.x(), player.y(), dispatch, now_ms);
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
    erase_ordered_id(visibility.actor_order, actor_id);
    if (auto* watcher = find_player(watcher_id); watcher != nullptr) {
      queue_packet(dispatch, watcher->session_id(),
                   make_disappear_packet(watcher->session_id(), actor_id));
    }
  }
  visibility_.erase(actor_id);
  legacy_ref_target_cache_.erase(actor_id);
}

void MapActor::remove_item_from_visibility(std::uint64_t item_id, RuntimeDispatch& dispatch,
                                          std::uint64_t now_ms,
                                          ItemVisibilityRemovalMode mode,
                                          std::uint64_t immediate_session_id) {
  for (const auto watcher_id : ordered_player_ids()) {
    auto visibility_it = visibility_.find(watcher_id);
    if (visibility_it == visibility_.end()) {
      continue;
    }
    auto& visibility = visibility_it->second;
    if (!visibility.items.contains(item_id)) {
      continue;
    }
    auto* watcher = find_player(watcher_id);
    const auto item_it = ground_items_.find(item_id);
    switch (mode) {
      case ItemVisibilityRemovalMode::immediate_all:
        visibility.items.erase(item_id);
        erase_ordered_id(visibility.item_order, item_id);
        if (watcher != nullptr && item_it != ground_items_.end()) {
          queue_packet(dispatch, watcher->session_id(),
                       make_item_hide_packet(watcher->session_id(), item_it->second));
        }
        break;
      case ItemVisibilityRemovalMode::delayed_all:
        visibility.delayed_item_hides_until_ms[item_id] = now_ms;
        break;
      case ItemVisibilityRemovalMode::immediate_single_session:
        if (watcher != nullptr && watcher->session_id() == immediate_session_id) {
          visibility.items.erase(item_id);
          erase_ordered_id(visibility.item_order, item_id);
          visibility.delayed_item_hides_until_ms.erase(item_id);
          if (item_it != ground_items_.end()) {
            queue_packet(dispatch, watcher->session_id(),
                         make_item_hide_packet(watcher->session_id(), item_it->second));
          }
        } else {
          visibility.delayed_item_hides_until_ms[item_id] = now_ms;
        }
        break;
    }
  }
}

