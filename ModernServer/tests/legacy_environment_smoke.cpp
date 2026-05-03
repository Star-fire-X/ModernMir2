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
  assert(env.add_item_object(0, 1, 100, {}, 201).ok);
  assert(env.add_item_object(0, 1, 101, {}, 202).ok);
  assert(env.add_item_object(0, 1, 102, {}, 203).ok);
  assert(env.add_item_object(0, 1, 103, {}, 204).ok);
  assert(!env.add_item_object(0, 1, 104, {}, 205).ok);
  assert(env.first_item_object_id(0, 1) == 100);
  assert(env.delete_from_map(0, 1, mir2::LegacyMapObjectShape::item_object, 100) == 1);
  assert(env.first_item_object_id(0, 1) == 101);
  assert(env.delete_from_map(9, 9, mir2::LegacyMapObjectShape::item_object, 101) == 0);
  assert(env.delete_from_map(2, 2, mir2::LegacyMapObjectShape::item_object, 101) == -2);

  mir2::LegacyMapEnvironment gold_env(3, 3);
  auto first_gold =
      gold_env.add_item_object(2, 2, 1, mir2::LegacyMapItemState{true, 200}, 300);
  assert(first_gold.ok && !first_gold.merged && first_gold.object_id == 1);
  auto merged_gold =
      gold_env.add_item_object(2, 2, 2, mir2::LegacyMapItemState{true, 300}, 301);
  assert(merged_gold.ok && merged_gold.merged && merged_gold.object_id == 1);
  const auto* gold_cell = gold_env.cell(2, 2);
  assert(gold_cell != nullptr && gold_cell->obj_list.size() == 1);
  assert(gold_cell->obj_list.front().item.gold_amount == 500);

  auto split_gold = gold_env.add_item_object(
      2, 2, 2, mir2::LegacyMapItemState{true, mir2::kLegacyBagGold}, 302);
  assert(split_gold.ok && !split_gold.merged && split_gold.object_id == 2);
  gold_cell = gold_env.cell(2, 2);
  assert(gold_cell != nullptr && gold_cell->obj_list.size() == 2);

  return 0;
}
