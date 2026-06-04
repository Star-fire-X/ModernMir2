#include <cassert>
#include <cstdint>
#include <memory>

#include "shared/legacy/map_document.hpp"
#include "world/legacy_map_environment.hpp"

namespace {

std::shared_ptr<mir2::legacy::MapDocument> make_map() {
  auto map = std::make_shared<mir2::legacy::MapDocument>();
  map->width = 3;
  map->height = 3;
  map->cells.resize(9);
  map->cells[1 * 3 + 1].fr_img = 0x8000U;
  return map;
}

std::shared_ptr<mir2::legacy::MapDocument> make_door_map() {
  auto map = std::make_shared<mir2::legacy::MapDocument>();
  map->width = 3;
  map->height = 3;
  map->cells.resize(9);
  map->cells[1 * 3 + 1].door_index = 0x80U | 1U;
  return map;
}

const mir2::LegacyMapObject* find_object(const mir2::LegacyMapEnvironment::Cell& cell,
                                         mir2::LegacyMapObjectShape shape,
                                         std::uint64_t object_id) {
  for (const auto& object : cell.obj_list) {
    if (object.shape == shape && object.object_id == object_id) {
      return &object;
    }
  }
  return nullptr;
}

}  // namespace

int main() {
  mir2::LegacyMapEnvironment env(3, 3, make_map());

  assert(env.can_walk(0, 0, false));
  assert(!env.can_walk(3, 0, false));
  assert(!env.can_walk(1, 1, false));

  assert(env.add_moving_object(0, 0, 10, 100));
  assert(!env.can_walk(0, 0, false));
  assert(env.can_walk(0, 0, true));

  assert(env.add_moving_object(1, 0, 20, 110));
  assert(env.move_to_moving_object(0, 0, 10, 1, 1, false, 120) == -1);
  assert(env.move_to_moving_object(0, 0, 10, 1, 0, false, 130) == 0);
  assert(env.move_to_moving_object(0, 0, 10, 0, 1, false, 140) == 1);
  assert(env.cell(0, 0) == nullptr);
  const auto* moved_cell = env.cell(0, 1);
  assert(moved_cell != nullptr);
  assert(find_object(*moved_cell, mir2::LegacyMapObjectShape::moving_object, 10) != nullptr);

  assert(env.verify_map_time(0, 1, 10, 777));
  moved_cell = env.cell(0, 1);
  const auto* moved_object =
      find_object(*moved_cell, mir2::LegacyMapObjectShape::moving_object, 10);
  assert(moved_object != nullptr && moved_object->a_time_ms == 777);

  assert(!env.add_item_object(1, 1, 100, {}, 200).ok);
  assert(!env.add_placeholder_object(
      1, 1, mir2::LegacyMapObjectShape::event_object, 900, 200,
      mir2::LegacyMapPlacementPolicy::passable_only));
  assert(!env.add_placeholder_object(
      0, 1, mir2::LegacyMapObjectShape::event_object, 901, 200,
      mir2::LegacyMapPlacementPolicy::blocked_only));
  assert(env.add_placeholder_object(
      1, 1, mir2::LegacyMapObjectShape::event_object, 902, 200,
      mir2::LegacyMapPlacementPolicy::blocked_only));
  assert(env.add_placeholder_object(
      0, 1, mir2::LegacyMapObjectShape::event_object, 903, 200,
      mir2::LegacyMapPlacementPolicy::passable_only));
  assert(env.add_placeholder_object(
      2, 0, mir2::LegacyMapObjectShape::event_object, 904, 200,
      mir2::LegacyMapPlacementPolicy::passable_only, true));
  assert(env.can_walk(2, 0, false));
  assert(env.can_safe_walk(2, 0));
  assert(env.add_placeholder_object(
      2, 1, mir2::LegacyMapObjectShape::event_object, 905, 200,
      mir2::LegacyMapPlacementPolicy::passable_only, false, 12));
  assert(env.can_walk(2, 1, false));
  assert(!env.can_safe_walk(2, 1));
  assert(env.add_item_object(0, 1, 100, {}, 201).ok);
  assert(env.add_item_object(0, 1, 101, {}, 202).ok);
  assert(env.add_item_object(0, 1, 102, {}, 203).ok);
  assert(env.add_item_object(0, 1, 103, {}, 204).ok);
  assert(!env.add_item_object(0, 1, 104, {}, 205).ok);
  assert(env.first_item_object_id(0, 1) == 103);
  assert(env.delete_from_map(0, 1, mir2::LegacyMapObjectShape::item_object, 103) == 1);
  assert(env.first_item_object_id(0, 1) == 102);
  assert(env.delete_from_map(9, 9, mir2::LegacyMapObjectShape::item_object, 101) == 0);
  assert(env.delete_from_map(2, 2, mir2::LegacyMapObjectShape::item_object, 101) == -2);

  mir2::LegacyMapEnvironment gold_env(3, 3);
  auto first_gold =
      gold_env.add_item_object(2, 2, 1, mir2::LegacyMapItemState{true, 200}, 300);
  assert(first_gold.ok && !first_gold.merged && first_gold.object_id == 1);
  auto merged_gold =
      gold_env.add_item_object(2, 2, 2, mir2::LegacyMapItemState{true, 300}, 301);
  assert(merged_gold.ok && merged_gold.merged && merged_gold.object_id == 1 &&
         merged_gold.merged_gold_amount == 500);
  const auto* gold_cell = gold_env.cell(2, 2);
  assert(gold_cell != nullptr && gold_cell->obj_list.size() == 1);
  assert(gold_cell->obj_list.front().item.gold_amount == 500);

  auto split_gold = gold_env.add_item_object(
      2, 2, 2, mir2::LegacyMapItemState{true, mir2::kLegacyBagGold}, 302);
  assert(split_gold.ok && !split_gold.merged && split_gold.object_id == 2);
  gold_cell = gold_env.cell(2, 2);
  assert(gold_cell != nullptr && gold_cell->obj_list.size() == 2);

  mir2::LegacyMapEnvironment door_env(3, 3, make_door_map());
  assert(door_env.door_core_count() == 1);
  assert(door_env.static_can_move(1, 1));
  assert(door_env.static_can_fly(1, 1));
  assert(door_env.can_walk(1, 1, false));
  assert(door_env.can_fly_line(0, 1, 2, 1));
  assert(door_env.can_fire_fly_line(0, 1, 2, 1));
  const auto opened = door_env.open_doors_around(1, 1, 400);
  assert(opened.size() == 1);
  assert(door_env.door_is_open(1, 1));
  assert(door_env.static_can_move(1, 1));
  assert(door_env.static_can_fly(1, 1));
  assert(door_env.can_walk(1, 1, false));
  assert(door_env.can_fly_line(0, 1, 2, 1));
  assert(door_env.can_fire_fly_line(0, 1, 2, 1));
  const auto closed = door_env.close_expired_doors(5401, 5000);
  assert(closed.size() == 1);
  assert(door_env.static_can_move(1, 1));
  assert(door_env.static_can_fly(1, 1));
  assert(door_env.can_fire_fly_line(0, 1, 2, 1));

  return 0;
}
