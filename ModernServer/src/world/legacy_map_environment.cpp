#include "world/legacy_map_environment.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace mir2 {

LegacyMapEnvironment::LegacyMapEnvironment(
    std::int32_t width, std::int32_t height,
    std::shared_ptr<const legacy::MapDocument> movement_map) {
  reset(width, height, std::move(movement_map));
}

void LegacyMapEnvironment::reset(std::int32_t width, std::int32_t height,
                                 std::shared_ptr<const legacy::MapDocument> movement_map) {
  width_ = width;
  height_ = height;
  movement_map_ = std::move(movement_map);
  cells_.clear();
  door_cores_.clear();
  door_tiles_.clear();
  load_doors_from_map();
}

bool LegacyMapEnvironment::in_bounds(std::int32_t x, std::int32_t y) const {
  if (movement_map_ != nullptr) {
    return movement_map_->cell(x, y) != nullptr;
  }
  if (width_ <= 0 || height_ <= 0) {
    return true;
  }
  return x >= 0 && y >= 0 && x < width_ && y < height_;
}

bool LegacyMapEnvironment::static_can_move(std::int32_t x, std::int32_t y) const {
  if (!in_bounds(x, y)) {
    return false;
  }
  if (movement_map_ != nullptr) {
    const auto* target = movement_map_->cell(x, y);
    return target != nullptr && !legacy::MapDocument::terrain_blocks_move(*target);
  }
  return true;
}

bool LegacyMapEnvironment::static_can_fly(std::int32_t x, std::int32_t y) const {
  if (!in_bounds(x, y)) {
    return false;
  }
  if (movement_map_ != nullptr) {
    const auto* target = movement_map_->cell(x, y);
    return target != nullptr && (target->fr_img & 0x8000U) == 0U;
  }
  return true;
}

bool LegacyMapEnvironment::can_walk(std::int32_t x, std::int32_t y, bool allow_dup) const {
  if (!static_can_move(x, y)) {
    return false;
  }
  if (allow_dup) {
    return true;
  }

  const auto* target = cell(x, y);
  if (target == nullptr) {
    return true;
  }
  for (const auto& object : target->obj_list) {
    if (object.shape == LegacyMapObjectShape::event_object && object.blocks_walk) {
      return false;
    }
    if (object.shape == LegacyMapObjectShape::moving_object) {
      if (!object.moving.ghost && object.moving.hold_place && !object.moving.death &&
          !object.moving.hide_mode && !object.moving.supervisor_mode) {
        return false;
      }
    }
  }
  return true;
}

bool LegacyMapEnvironment::can_fly_line(std::int32_t from_x, std::int32_t from_y,
                                        std::int32_t to_x, std::int32_t to_y) const {
  if (!in_bounds(from_x, from_y) || !in_bounds(to_x, to_y)) {
    return false;
  }
  auto x = from_x;
  auto y = from_y;
  for (std::int32_t step = 0; step < 32; ++step) {
    if (x == to_x && y == to_y) {
      return true;
    }
    const auto dx = to_x == x ? 0 : (to_x > x ? 1 : -1);
    const auto dy = to_y == y ? 0 : (to_y > y ? 1 : -1);
    x += dx;
    y += dy;
    if (!static_can_move(x, y)) {
      return false;
    }
    if (x == to_x && y == to_y) {
      return true;
    }
  }
  return false;
}

bool LegacyMapEnvironment::can_fire_fly_line(std::int32_t from_x, std::int32_t from_y,
                                             std::int32_t to_x, std::int32_t to_y) const {
  if (!in_bounds(from_x, from_y) || !in_bounds(to_x, to_y)) {
    return false;
  }
  auto x = from_x;
  auto y = from_y;
  for (std::int32_t step = 0; step < 32; ++step) {
    if (x == to_x && y == to_y) {
      return true;
    }
    const auto dx = to_x == x ? 0 : (to_x > x ? 1 : -1);
    const auto dy = to_y == y ? 0 : (to_y > y ? 1 : -1);
    x += dx;
    y += dy;
    if (!static_can_fly(x, y)) {
      return false;
    }
    if (x == to_x && y == to_y) {
      return true;
    }
  }
  return false;
}

bool LegacyMapEnvironment::add_moving_object(std::int32_t x, std::int32_t y,
                                             std::uint64_t object_id,
                                             std::uint64_t now_ms,
                                             LegacyMovingObjectState state) {
  if (!static_can_move(x, y)) {
    return false;
  }
  auto* target = mutable_cell(x, y);
  if (target == nullptr) {
    return false;
  }

  LegacyMapObject object;
  object.shape = LegacyMapObjectShape::moving_object;
  object.object_id = object_id;
  object.a_time_ms = now_ms;
  object.moving = state;
  target->obj_list.push_back(object);
  return true;
}

int LegacyMapEnvironment::move_to_moving_object(std::int32_t old_x, std::int32_t old_y,
                                                std::uint64_t object_id,
                                                std::int32_t new_x, std::int32_t new_y,
                                                bool allow_dup, std::uint64_t now_ms,
                                                LegacyMovingObjectState state) {
  if (!allow_dup) {
    if (!in_bounds(new_x, new_y) || !static_can_move(new_x, new_y)) {
      return -1;
    }
    if (!can_walk(new_x, new_y, false)) {
      return 0;
    }
  }

  if (!in_bounds(new_x, new_y) || !static_can_move(new_x, new_y)) {
    return -1;
  }

  const CellKey old_key{old_x, old_y};
  if (auto old_it = cells_.find(old_key); old_it != cells_.end()) {
    auto& list = old_it->second.obj_list;
    for (auto it = list.begin(); it != list.end();) {
      if (it->shape == LegacyMapObjectShape::moving_object && it->object_id == object_id) {
        it = list.erase(it);
      } else {
        ++it;
      }
    }
    erase_cell_if_empty(old_key);
  }

  return add_moving_object(new_x, new_y, object_id, now_ms, state) ? 1 : -1;
}

LegacyMapAddResult LegacyMapEnvironment::add_item_object(std::int32_t x, std::int32_t y,
                                                         std::uint64_t object_id,
                                                         LegacyMapItemState item,
                                                         std::uint64_t now_ms) {
  LegacyMapAddResult result;
  if (!static_can_move(x, y)) {
    return result;
  }

  auto* target = mutable_cell(x, y);
  if (target == nullptr) {
    return result;
  }

  if (item.is_gold) {
    for (auto& object : target->obj_list) {
      if (object.shape != LegacyMapObjectShape::item_object || !object.item.is_gold) {
        continue;
      }
      const auto merged_amount = object.item.gold_amount + item.gold_amount;
      if (merged_amount <= kLegacyBagGold) {
        object.item.gold_amount = merged_amount;
        object.a_time_ms = now_ms;
        result.ok = true;
        result.merged = true;
        result.object_id = object.object_id;
        return result;
      }
    }
  }

  if (target->obj_list.size() >= 5) {
    return result;
  }

  LegacyMapObject object;
  object.shape = LegacyMapObjectShape::item_object;
  object.object_id = object_id;
  object.a_time_ms = now_ms;
  object.item = item;
  target->obj_list.push_back(object);

  result.ok = true;
  result.object_id = object_id;
  return result;
}

bool LegacyMapEnvironment::add_placeholder_object(std::int32_t x, std::int32_t y,
                                                  LegacyMapObjectShape shape,
                                                  std::uint64_t object_id,
                                                  std::uint64_t now_ms,
                                                  bool blocks_walk) {
  if (!static_can_move(x, y)) {
    return false;
  }
  auto* target = mutable_cell(x, y);
  if (target == nullptr) {
    return false;
  }
  LegacyMapObject object;
  object.shape = shape;
  object.object_id = object_id;
  object.a_time_ms = now_ms;
  object.blocks_walk = blocks_walk;
  target->obj_list.push_back(object);
  return true;
}

bool LegacyMapEnvironment::add_gate_object(std::int32_t x, std::int32_t y,
                                           std::uint64_t object_id,
                                           LegacyMapGateState gate,
                                           std::uint64_t now_ms) {
  if (!in_bounds(x, y)) {
    return false;
  }
  auto* target = mutable_cell(x, y);
  if (target == nullptr) {
    return false;
  }
  LegacyMapObject object;
  object.shape = LegacyMapObjectShape::gate_object;
  object.object_id = object_id;
  object.a_time_ms = now_ms;
  object.gate = std::move(gate);
  target->obj_list.push_back(std::move(object));
  return true;
}

int LegacyMapEnvironment::delete_from_map(std::int32_t x, std::int32_t y,
                                          LegacyMapObjectShape shape,
                                          std::uint64_t object_id) {
  if (!in_bounds(x, y)) {
    return 0;
  }

  const CellKey key{x, y};
  auto cell_it = cells_.find(key);
  if (cell_it == cells_.end() || cell_it->second.obj_list.empty()) {
    return -2;
  }

  int result = -1;
  auto& list = cell_it->second.obj_list;
  for (auto it = list.begin(); it != list.end();) {
    if (it->shape == shape && it->object_id == object_id) {
      it = list.erase(it);
      result = 1;
    } else {
      ++it;
    }
  }
  erase_cell_if_empty(key);
  return result;
}

bool LegacyMapEnvironment::verify_map_time(std::int32_t x, std::int32_t y,
                                           std::uint64_t object_id,
                                           std::uint64_t now_ms) {
  auto cell_it = cells_.find(CellKey{x, y});
  if (cell_it == cells_.end()) {
    return false;
  }
  for (auto& object : cell_it->second.obj_list) {
    if (object.shape == LegacyMapObjectShape::moving_object &&
        object.object_id == object_id) {
      object.a_time_ms = now_ms;
      return true;
    }
  }
  return false;
}

const LegacyMapObject* LegacyMapEnvironment::gate_at(std::int32_t x, std::int32_t y) const {
  const auto* target = cell(x, y);
  if (target == nullptr) {
    return nullptr;
  }
  for (const auto& object : target->obj_list) {
    if (object.shape == LegacyMapObjectShape::gate_object) {
      return &object;
    }
  }
  return nullptr;
}

bool LegacyMapEnvironment::around_door_opened(std::int32_t x, std::int32_t y) const {
  auto found = false;
  for (std::int32_t dy = -1; dy <= 1; ++dy) {
    for (std::int32_t dx = -1; dx <= 1; ++dx) {
      const auto* core = door_core_at(x + dx, y + dy);
      if (core == nullptr) {
        continue;
      }
      found = true;
      if (!core->open) {
        return false;
      }
    }
  }
  return true;
}

std::vector<std::pair<std::int32_t, std::int32_t>> LegacyMapEnvironment::open_doors_around(
    std::int32_t x, std::int32_t y, std::uint64_t now_ms) {
  std::vector<CellKey> opened;
  std::vector<std::size_t> opened_cores;
  for (std::int32_t dy = -1; dy <= 1; ++dy) {
    for (std::int32_t dx = -1; dx <= 1; ++dx) {
      const auto it = door_tiles_.find(CellKey{x + dx, y + dy});
      if (it == door_tiles_.end()) {
        continue;
      }
      const auto core_index = it->second.core_index;
      if (std::find(opened_cores.begin(), opened_cores.end(), core_index) != opened_cores.end()) {
        continue;
      }
      if (core_index >= door_cores_.size()) {
        continue;
      }
      auto& core = door_cores_[core_index];
      if (core.locked || core.open) {
        continue;
      }
      core.open = true;
      core.open_time_ms = now_ms;
      opened_cores.push_back(core_index);
      opened.insert(opened.end(), core.tiles.begin(), core.tiles.end());
    }
  }
  return opened;
}

std::vector<std::pair<std::int32_t, std::int32_t>> LegacyMapEnvironment::close_expired_doors(
    std::uint64_t now_ms, std::uint64_t ttl_ms) {
  std::vector<CellKey> closed;
  for (auto& core : door_cores_) {
    if (!core.open || core.open_time_ms == 0 || now_ms <= core.open_time_ms + ttl_ms) {
      continue;
    }
    core.open = false;
    core.open_time_ms = 0;
    closed.insert(closed.end(), core.tiles.begin(), core.tiles.end());
  }
  return closed;
}

bool LegacyMapEnvironment::door_is_open(std::int32_t x, std::int32_t y) const {
  const auto* core = door_core_at(x, y);
  return core != nullptr && core->open;
}

std::optional<std::uint64_t> LegacyMapEnvironment::first_item_object_id(std::int32_t x,
                                                                        std::int32_t y) const {
  const auto* target = cell(x, y);
  if (target == nullptr) {
    return std::nullopt;
  }
  for (const auto& object : target->obj_list) {
    if (object.shape == LegacyMapObjectShape::item_object) {
      return object.object_id;
    }
  }
  return std::nullopt;
}

std::optional<std::size_t> LegacyMapEnvironment::item_object_count(std::int32_t x,
                                                                   std::int32_t y) const {
  if (!static_can_move(x, y)) {
    return std::nullopt;
  }
  const auto* target = cell(x, y);
  if (target == nullptr) {
    return 0;
  }
  return static_cast<std::size_t>(
      std::count_if(target->obj_list.begin(), target->obj_list.end(),
                    [](const LegacyMapObject& object) {
                      return object.shape == LegacyMapObjectShape::item_object;
                    }));
}

std::vector<std::uint64_t> LegacyMapEnvironment::item_object_ids_in_order() const {
  std::vector<std::uint64_t> ids;
  for (const auto& [_, target] : cells_) {
    for (const auto& object : target.obj_list) {
      if (object.shape == LegacyMapObjectShape::item_object) {
        ids.push_back(object.object_id);
      }
    }
  }
  return ids;
}

const LegacyMapEnvironment::Cell* LegacyMapEnvironment::cell(std::int32_t x,
                                                             std::int32_t y) const {
  const auto it = cells_.find(CellKey{x, y});
  return it != cells_.end() ? &it->second : nullptr;
}

LegacyMapEnvironment::Cell* LegacyMapEnvironment::mutable_cell(std::int32_t x,
                                                               std::int32_t y) {
  if (!in_bounds(x, y)) {
    return nullptr;
  }
  return &cells_[CellKey{x, y}];
}

void LegacyMapEnvironment::erase_cell_if_empty(CellKey key) {
  const auto it = cells_.find(key);
  if (it != cells_.end() && it->second.obj_list.empty()) {
    cells_.erase(it);
  }
}

void LegacyMapEnvironment::load_doors_from_map() {
  if (movement_map_ == nullptr) {
    return;
  }

  for (std::int32_t y = 0; y < movement_map_->height; ++y) {
    for (std::int32_t x = 0; x < movement_map_->width; ++x) {
      const auto* cell = movement_map_->cell(x, y);
      if (cell == nullptr || (cell->door_index & 0x80u) == 0) {
        continue;
      }
      const auto door_number = static_cast<std::int32_t>(cell->door_index & 0x7fu);
      if (door_number <= 0) {
        continue;
      }

      std::optional<std::size_t> core_index;
      for (std::size_t index = 0; index < door_cores_.size(); ++index) {
        const auto& core = door_cores_[index];
        if (core.number != door_number) {
          continue;
        }
        const auto near_existing = std::any_of(core.tiles.begin(), core.tiles.end(),
                                               [&](const CellKey& tile) {
                                                 return std::abs(tile.first - x) <= 10 &&
                                                        std::abs(tile.second - y) <= 10;
                                               });
        if (near_existing) {
          core_index = index;
          break;
        }
      }
      if (!core_index.has_value()) {
        DoorCore core;
        core.number = door_number;
        door_cores_.push_back(std::move(core));
        core_index = door_cores_.size() - 1;
      }
      door_cores_[*core_index].tiles.push_back(CellKey{x, y});
      door_tiles_[CellKey{x, y}] = DoorTile{door_number, *core_index};
    }
  }
}

LegacyMapEnvironment::DoorCore* LegacyMapEnvironment::door_core_at(std::int32_t x,
                                                                   std::int32_t y) {
  const auto it = door_tiles_.find(CellKey{x, y});
  if (it == door_tiles_.end() || it->second.core_index >= door_cores_.size()) {
    return nullptr;
  }
  return &door_cores_[it->second.core_index];
}

const LegacyMapEnvironment::DoorCore* LegacyMapEnvironment::door_core_at(
    std::int32_t x, std::int32_t y) const {
  const auto it = door_tiles_.find(CellKey{x, y});
  if (it == door_tiles_.end() || it->second.core_index >= door_cores_.size()) {
    return nullptr;
  }
  return &door_cores_[it->second.core_index];
}

}  // namespace mir2
