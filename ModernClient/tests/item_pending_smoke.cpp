#include <cassert>
#include <cstdint>

#include "game/game_state.hpp"

namespace {

using namespace mir2::client;
using namespace mir2::client_v1;

ItemState make_item(const char* name, const std::int32_t make_index,
                    const std::uint8_t std_mode) {
  ItemState item;
  item.name = name;
  item.make_index = make_index;
  item.std_mode = std_mode;
  item.looks = 1;
  item.dura = 100;
  item.dura_max = 1000;
  return item;
}

void test_equip_pending_clears_only_on_matching_bag_or_equipment_update() {
  GameStateStore state;
  const auto sword = make_item("Wooden Sword", 1001, 5);
  const auto potion = make_item("Potion", 2001, 0);
  state.world.bag_items[6] = sword;
  state.begin_pending_item_action(PendingItemActionKind::equip, MovingItemSource::bag, 6, 1,
                                  sword, 100);
  state.world.bag_items[6] = ItemState{};

  state.apply(InventoryUpdate{ItemSlotState{7, potion}});
  assert(state.world.pending_item_action.active);

  state.apply(InventoryRemove{6});
  assert(!state.world.pending_item_action.active);

  state.begin_pending_item_action(PendingItemActionKind::equip, MovingItemSource::bag, 6, 1,
                                  sword, 200);
  state.world.bag_items[6] = ItemState{};
  EquipmentSnapshot equipment;
  equipment.items.push_back(ItemSlotState{1, sword});
  state.apply(equipment);
  assert(!state.world.pending_item_action.active);
}

void test_unequip_pending_clears_on_matching_bag_or_equipment_update() {
  GameStateStore state;
  const auto sword = make_item("Wooden Sword", 1001, 5);
  const auto potion = make_item("Potion", 2001, 0);
  state.world.equipment[1] = sword;
  state.begin_pending_item_action(PendingItemActionKind::unequip, MovingItemSource::equipment, 1,
                                  8, sword, 100);
  state.world.equipment[1] = ItemState{};

  state.apply(InventoryUpdate{ItemSlotState{7, potion}});
  assert(state.world.pending_item_action.active);

  state.apply(InventoryAdd{ItemSlotState{8, sword}});
  assert(!state.world.pending_item_action.active);

  state.world.equipment[1] = sword;
  state.begin_pending_item_action(PendingItemActionKind::unequip, MovingItemSource::equipment, 1,
                                  8, sword, 200);
  state.world.equipment[1] = ItemState{};
  state.apply(EquipmentSnapshot{});
  assert(!state.world.pending_item_action.active);
}

void test_use_failure_and_timeout_restore_source_slot() {
  GameStateStore state;
  const auto potion = make_item("Potion", 2001, 0);
  state.world.bag_items[0] = potion;
  state.begin_pending_item_action(PendingItemActionKind::use, MovingItemSource::bag, 0, 0,
                                  potion, 100);
  state.world.bag_items[0] = ItemState{};
  state.apply(UseItemResult{false});
  assert(!state.world.pending_item_action.active);
  assert(state.world.bag_items[0].make_index == potion.make_index);

  state.begin_pending_item_action(PendingItemActionKind::drop, MovingItemSource::bag, 6, -1,
                                  potion, 200);
  state.world.bag_items[6] = ItemState{};
  assert(state.pending_item_action_expired(3200));
  state.restore_pending_item_action();
  assert(!state.world.pending_item_action.active);
  assert(state.world.bag_items[6].make_index == potion.make_index);
}

void test_drop_pending_ignores_unrelated_updates() {
  GameStateStore state;
  const auto sword = make_item("Wooden Sword", 1001, 5);
  const auto potion = make_item("Potion", 2001, 0);
  state.world.bag_items[6] = sword;
  state.begin_pending_item_action(PendingItemActionKind::drop, MovingItemSource::bag, 6, -1,
                                  sword, 100);
  state.world.bag_items[6] = ItemState{};

  state.apply(InventoryAdd{ItemSlotState{7, potion}});
  assert(state.world.pending_item_action.active);

  state.apply(InventoryRemove{6});
  assert(!state.world.pending_item_action.active);
}

void test_magic_key_rebinds_clear_previous_owner() {
  GameStateStore state;
  state.world.magics.push_back(MagicShortcutState{1, 1, 0, 0, 0, "Fire", 0, 0});
  state.world.magics.push_back(MagicShortcutState{2, 0, 0, 0, 0, "Thunder", 0, 0});

  state.bind_magic_key(2, 1);
  assert(state.world.magics[0].key == 0);
  assert(state.world.magics[1].key == 1);

  state.bind_magic_key(2, 0);
  assert(state.world.magics[1].key == 0);
}

void test_durability_change_updates_matching_bag_and_equipment_items() {
  GameStateStore state;
  auto bag_sword = make_item("Wooden Sword", 1001, 5);
  auto equipped_sword = bag_sword;
  const auto potion = make_item("Potion", 2001, 0);
  state.world.bag_items[6] = bag_sword;
  state.world.equipment[1] = equipped_sword;
  state.world.bag_items[7] = potion;

  state.apply(DurabilityChange{1001, 550, 900});

  assert(state.world.bag_items[6].dura == 550);
  assert(state.world.bag_items[6].dura_max == 900);
  assert(state.world.equipment[1].dura == 550);
  assert(state.world.equipment[1].dura_max == 900);
  assert(state.world.bag_items[7].dura == 100);
  assert(state.world.bag_items[7].dura_max == 1000);
}

}  // namespace

int main() {
  test_equip_pending_clears_only_on_matching_bag_or_equipment_update();
  test_unequip_pending_clears_on_matching_bag_or_equipment_update();
  test_use_failure_and_timeout_restore_source_slot();
  test_drop_pending_ignores_unrelated_updates();
  test_magic_key_rebinds_clear_previous_owner();
  test_durability_change_updates_matching_bag_and_equipment_items();
  return 0;
}
