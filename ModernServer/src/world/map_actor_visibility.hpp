/**
 * @file map_actor_visibility.hpp
 * @brief 地图角色可见性同步的实现文件
 * @details 该文件是 map_actor.cpp 的实现细节，实现了基于玩家视野范围的
 *          可见性管理系统。核心功能包括：
 *          - 获取有序的玩家/可见角色/可见物品/可见事件 ID 列表
 *          - 同步单个玩家的可见性（角色、物品、事件）
 *          - 同步所有玩家的可见性
 *          - 角色/物品/事件变化后的可见性刷新
 *          - 同地图传送后的可见性强制刷新
 *          - 从可见性系统中移除角色/物品
 *
 *          视野范围基于 kLegacyViewRange 常量，采用方形视野区域。
 */

#pragma once

// Implementation detail for map_actor.cpp: visibility synchronization members.

/**
 * @brief 获取地图中所有玩家的有序 ID 列表
 * @return 排序后的玩家 Actor ID 向量
 * @details 排序规则：按坐标 (x, y) 排序，同坐标按地图单元格中的对象列表顺序排序，
 *          最后按 actor_id 排序。这种排序保证了玩家列表的确定性顺序，
 *          对客户端渲染一致性很重要。
 */
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

/**
 * @brief 获取指定玩家视野内可见角色的有序 ID 列表
 * @param player 观察者玩家
 * @return 视野范围内可见的角色 Actor ID 向量
 * @details 遍历以玩家为中心的方形视野区域（kLegacyViewRange），
 *          收集所有 moving_object 类型的角色，并过滤掉不可见的角色。
 */
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

/**
 * @brief 获取指定玩家视野内可见物品的有序 ID 列表
 * @param player 观察者玩家
 * @return 视野范围内可见物品的 ID 向量
 * @details 遍历视野区域收集所有 item_object 类型的物品，
 *          使用 in_legacy_view_range 检查物品是否在视野区域内。
 */
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

/**
 * @brief 获取指定玩家视野内可见事件的有序 ID 列表
 * @param player 观察者玩家
 * @return 视野范围内可见事件对象的 ID 向量
 * @details 遍历视野区域收集所有 event_object 类型的事件对象，
 *          使用 event_objects_ 映射获取事件位置进行距离判断。
 */
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
            in_legacy_view_range(player.x(), player.y(), position_it->second.x,
                                 position_it->second.y)) {
          event_ids.push_back(entry.object_id);
        }
      }
    }
  }
  return event_ids;
}

namespace {

/**
 * @brief 从有序 ID 列表中移除指定的 ID
 * @param ordered_ids 有序 ID 列表（引用修改）
 * @param id 要移除的 ID
 * @details 使用 erase-remove 惯用法从向量中删除指定 ID。
 */
void erase_ordered_id(std::vector<std::uint64_t>& ordered_ids, std::uint64_t id) {
  ordered_ids.erase(std::remove(ordered_ids.begin(), ordered_ids.end(), id), ordered_ids.end());
}

/**
 * @brief 为观察者排队发送角色进入视野的包
 * @param dispatch 运行时调度输出
 * @param watcher 观察者玩家
 * @param target 进入视野的目标角色
 * @details 根据目标类型（玩家/怪物）和状态（存活/死亡），
 *          发送相应的进入包（turn/death/skeleton）。
 */
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

/**
 * @brief 获取指定角色的可见玩家（观察者）列表，使用缓存优化
 * @param origin 源角色
 * @param now_ms 当前系统时间（毫秒）
 * @return 观察者玩家 ID 向量
 * @details 缓存有效期为 500 毫秒，过期后重新从地图环境收集。
 *          只收集 non-ghost 的玩家作为观察者。
 *          视野范围为 12 格（比标准视野略大以覆盖边界情况）。
 */
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

bool MapActor::legacy_ref_target_cache_contains(std::uint64_t actor_id) const {
  return legacy_ref_target_cache_.contains(actor_id);
}

std::vector<std::uint64_t> MapActor::legacy_ref_target_player_ids_at(
    const std::int32_t x, const std::int32_t y) const {
  std::vector<std::uint64_t> watcher_ids;
  for (std::int32_t scan_x = x - 12; scan_x <= x + 12; ++scan_x) {
    for (std::int32_t scan_y = y - 12; scan_y <= y + 12; ++scan_y) {
      const auto* cell = environment_.cell(scan_x, scan_y);
      if (cell == nullptr) {
        continue;
      }
      for (const auto& entry : cell->obj_list) {
        if (entry.shape != LegacyMapObjectShape::moving_object ||
            std::find(watcher_ids.begin(), watcher_ids.end(), entry.object_id) !=
                watcher_ids.end()) {
          continue;
        }
        const auto object_it = objects_.find(entry.object_id);
        if (object_it == objects_.end()) {
          continue;
        }
        if (const auto* watcher = as_player(object_it->second.get());
            watcher != nullptr && !watcher->legacy_ghost() &&
            in_legacy_view_range(watcher->x(), watcher->y(), x, y)) {
          watcher_ids.push_back(watcher->id());
        }
      }
    }
  }
  return watcher_ids;
}

/**
 * @brief 同步指定玩家的视野可见性（角色、物品、事件）
 * @param player 目标玩家
 * @param dispatch 运行时调度输出
 * @param force 是否强制刷新所有可见对象
 * @param now_ms 当前系统时间（毫秒）
 * @details 核心可见性同步逻辑：
 *          1. 角色可见性：对比当前视野和新视野，发送消失/出现包
 *          2. 物品可见性：支持延迟消失（用于拾取后的短暂保留）
 *          3. 事件可见性：对比当前视野和新视野，发送隐藏/显示包
 */
void MapActor::sync_player_visibility(Player& player, RuntimeDispatch& dispatch, bool force,
                                      std::uint64_t now_ms) {
  constexpr std::uint64_t kLegacyMapObjectStaleMs = 10ULL * 60ULL * 1000ULL;
  static_cast<void>(environment_.prune_stale_moving_objects(
      player.x() - kLegacyViewRange, player.x() + kLegacyViewRange,
      player.y() - kLegacyViewRange, player.y() + kLegacyViewRange, now_ms,
      kLegacyMapObjectStaleMs));
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
      event_x = position_it->second.x;
      event_y = position_it->second.y;
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
    queue_packet(dispatch, player.session_id(),
                 make_show_event_packet(player.session_id(), event_id,
                                        position_it->second.x, position_it->second.y,
                                        position_it->second.type, position_it->second.event_param));
  }
  visibility.events = std::move(current_events);
  visibility.event_order = current_event_order;
}

/**
 * @brief 同步地图中所有玩家的可见性
 * @param dispatch 运行时调度输出
 * @param now_ms 当前系统时间（毫秒）
 * @details 遍历所有在线玩家，对每个玩家调用 sync_player_visibility。
 *          通常在玩家进入/离开地图或全局可见性变化时调用。
 */
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

/**
 * @brief 角色移动后同步所有可能受影响的玩家可见性
 * @param actor 移动的角色
 * @param old_x 移动前的 X 坐标
 * @param old_y 移动前的 Y 坐标
 * @param new_x 移动后的 X 坐标
 * @param new_y 移动后的 Y 坐标
 * @param dispatch 运行时调度输出
 * @param now_ms 当前系统时间（毫秒）
 * @details 当角色移动时，为所有可能看到旧位置或新位置的玩家刷新可见性。
 *          这包括角色自身（视野中心变化）和附近的观察者（可能有角色进入/离开视野）。
 */
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

/**
 * @brief 物品变化后同步所有可能受影响的玩家可见性
 * @param item_x 物品 X 坐标
 * @param item_y 物品 Y 坐标
 * @param dispatch 运行时调度输出
 * @param now_ms 当前系统时间（毫秒）
 * @param refresh_item_id 需要强制刷新的物品 ID（可选）
 * @details 当物品被添加、移除或修改时，为所有能看见该位置的玩家刷新可见性。
 *          如果提供了 refresh_item_id，会在刷新后重新发送该物品的显示包。
 */
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

/**
 * @brief 事件对象变化后同步所有可能受影响的玩家可见性
 * @param event_x 事件 X 坐标
 * @param event_y 事件 Y 坐标
 * @param dispatch 运行时调度输出
 * @param now_ms 当前系统时间（毫秒）
 */
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

/**
 * @brief 同地图传送后强制刷新可见性
 * @param player 传送的玩家
 * @param old_x 传送前 X 坐标
 * @param old_y 传送前 Y 坐标
 * @param dispatch 运行时调度输出
 * @param now_ms 当前系统时间（毫秒）
 * @param space_move_hide_ident 传送潜行消失包标识
 * @param space_move_show_ident 传送潜行显示包标识
 * @details 处理同地图传送后的可见性刷新：
 *          1. 向旧位置附近的观察者发送消失包
 *          2. 清空传送玩家自身的对象列表
 *          3. 发送地图切换包
 *          4. 向新观察者发送显示包
 *          5. 刷新传送玩家的完整可见性
 */
void MapActor::force_refresh_after_same_map_transfer(Player& player, std::int32_t old_x,
                                                     std::int32_t old_y,
                                                     RuntimeDispatch& dispatch,
                                                     std::uint64_t now_ms,
                                                     std::uint16_t space_move_hide_ident,
                                                     std::uint16_t space_move_show_ident) {
  if (space_move_hide_ident == 0 && space_move_show_ident == 0) {
    remove_actor_from_visibility(player.id(), dispatch);
    queue_packet(dispatch, player.session_id(), make_clear_objects_packet(player.session_id()));
    queue_packet(dispatch, player.session_id(),
                 make_change_map_packet(player.session_id(), config_.id, player.x(), player.y(),
                                        legacy_map_darkness(config_)));
    sync_player_visibility(player, dispatch, true, now_ms);
    sync_all_player_visibility(dispatch, now_ms);
    queue_save_character(dispatch, player);
    static_cast<void>(old_x);
    static_cast<void>(old_y);
    return;
  }

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

/**
 * @brief 从所有玩家的可见性中移除指定角色
 * @param actor_id 要移除的角色 ID
 * @param dispatch 运行时调度输出
 * @details 向所有在线玩家发送消失包，并从可见性映射中删除该角色。
 *          同时清除该角色的参考目标缓存。
 */
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

/**
 * @brief 从所有玩家的可见性中移除指定物品
 * @param item_id 要移除的物品 ID
 * @param dispatch 运行时调度输出
 * @param now_ms 当前系统时间（毫秒）
 * @param mode 物品可见性移除模式
 * @param immediate_session_id 立即移除模式的会话 ID
 * @details 支持三种移除模式：
 *          - immediate_all: 对所有观察者立即隐藏
 *          - delayed_all: 对所有观察者延迟隐藏
 *          - immediate_single_session: 对指定会话立即隐藏，其他观察者延迟隐藏
 */
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
