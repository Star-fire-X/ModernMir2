#include "game/game_state.hpp"
#include "scene/legacy_inventory_ui.hpp"
#include "ui/ui.hpp"

#include <cassert>

namespace {

using namespace mir2::client;
using namespace mir2::client_v1;
namespace ui = mir2::client::ui;

ItemState make_item(const char* name, const std::int32_t make_index,
                    const std::uint8_t std_mode) {
  ItemState item;
  item.name = name;
  item.make_index = make_index;
  item.looks = 1;
  item.std_mode = std_mode;
  item.dura = 100;
  item.dura_max = 1000;
  return item;
}

void click_at(ui::UiTree& tree, const int x, const int y) {
  InputState press{};
  press.mouse_x = x;
  press.mouse_y = y;
  press.left_pressed = true;
  press.left_down = true;
  tree.update(press);

  InputState release{};
  release.mouse_x = x;
  release.mouse_y = y;
  release.left_released = true;
  tree.update(release);
}

void right_press_at(ui::UiTree& tree, const int x, const int y) {
  InputState press{};
  press.mouse_x = x;
  press.mouse_y = y;
  press.right_pressed = true;
  press.right_down = true;
  tree.update(press);
}

class InventorySlotNode final : public ui::UiNode {
 public:
  InventorySlotNode(const RectI bounds, ui::Window* menu_window)
      : ui::UiNode(bounds), menu(menu_window) {
    focusable = true;
  }

  bool on_mouse_down(ui::UiTree& tree, const InputState& /*input*/,
                     const ui::UiMouseButton button) override {
    if (button != ui::UiMouseButton::right || menu == nullptr) {
      return false;
    }
    menu->show(tree);
    return true;
  }

  ui::Window* menu{nullptr};
};

void test_bag_window_grid_double_click_use_rolls_back() {
  GameStateStore state;
  const auto potion = make_item("Potion", 2001, 0);
  state.world.bag_items[0] = potion;

  ui::UiTree tree;
  auto* root = tree.set_root<ui::UiNode>(RectI{0, 0, 800, 600});
  auto* bag = root->emplace_child<ui::Window>(RectI{100, 100, 160, 120});
  bag->visible = false;
  bag->show(tree);
  assert(bag->visible);

  auto* grid = bag->emplace_child<ui::Grid>(RectI{10, 10, 80, 40});
  grid->col_count = 2;
  grid->row_count = 1;
  grid->col_width = 40;
  grid->row_height = 40;
  int selected_slot = -1;
  grid->on_cell_select = [&](ui::Grid&, const int col, const int row) {
    selected_slot = row * 2 + col;
    state.world.selected_bag_slot = selected_slot;
  };
  grid->on_cell_double_click = [&](ui::Grid&, const int col, const int row) {
    const auto slot = row * 2 + col;
    const auto item = state.world.bag_items[static_cast<std::size_t>(slot)];
    state.begin_pending_item_action(PendingItemActionKind::use, MovingItemSource::bag,
                                    slot, slot, item, 100);
    state.world.bag_items[static_cast<std::size_t>(slot)] = ItemState{};
  };

  click_at(tree, 115, 115);
  click_at(tree, 115, 115);
  assert(selected_slot == 0);
  assert(state.world.pending_item_action.active);
  assert(state.world.bag_items[0].empty());

  state.apply(UseItemResult{false});
  assert(!state.world.pending_item_action.active);
  assert(state.world.bag_items[0].make_index == 2001);

  bag->hide(tree);
  assert(!bag->visible);
}

void test_right_click_menu_drop_and_pending_clear() {
  GameStateStore state;
  const auto ore = make_item("Ore", 3001, 0);
  state.world.bag_items[6] = ore;

  ui::UiTree tree;
  auto* root = tree.set_root<ui::UiNode>(RectI{0, 0, 800, 600});
  auto* menu = root->emplace_child<ui::Window>(RectI{50, 50, 80, 30});
  menu->visible = false;
  auto* drop_button = menu->emplace_child<ui::Button>(RectI{0, 0, 80, 30});
  drop_button->on_click = [&] {
    state.begin_pending_item_action(PendingItemActionKind::drop, MovingItemSource::bag,
                                    6, -1, ore, 200);
    state.world.bag_items[6] = ItemState{};
  };
  root->emplace_child<InventorySlotNode>(RectI{10, 10, 32, 32}, menu);

  right_press_at(tree, 16, 16);
  assert(menu->visible);
  click_at(tree, 60, 60);
  assert(state.world.pending_item_action.active);
  assert(state.world.bag_items[6].empty());

  state.apply(InventoryRemove{6});
  assert(!state.world.pending_item_action.active);
}

void test_equip_unequip_belt_and_durability_state() {
  GameStateStore state;
  const auto sword = make_item("Wooden Sword", 1001, 5);
  const auto potion = make_item("Potion", 2001, 0);
  state.world.bag_items[0] = potion;
  state.world.bag_items[6] = sword;

  state.begin_pending_item_action(PendingItemActionKind::equip, MovingItemSource::bag,
                                  6, 1, sword, 100);
  state.world.bag_items[6] = ItemState{};
  EquipmentSnapshot equipped;
  equipped.items.push_back(ItemSlotState{1, sword});
  state.apply(equipped);
  assert(!state.world.pending_item_action.active);
  assert(state.world.equipment[1].make_index == 1001);

  state.apply(DurabilityChange{1001, 500, 900});
  assert(state.world.equipment[1].dura == 500);
  assert(state.world.equipment[1].dura_max == 900);

  state.begin_pending_item_action(PendingItemActionKind::use, MovingItemSource::bag,
                                  0, 0, potion, 300);
  state.world.bag_items[0] = ItemState{};
  state.apply(UseItemResult{true});
  assert(!state.world.pending_item_action.active);
  assert(state.world.bag_items[0].empty());

  state.begin_pending_item_action(PendingItemActionKind::unequip, MovingItemSource::equipment,
                                  1, 8, state.world.equipment[1], 400);
  state.world.equipment[1] = ItemState{};
  state.apply(InventoryAdd{ItemSlotState{8, sword}});
  assert(!state.world.pending_item_action.active);
  assert(state.world.bag_items[8].make_index == 1001);
}

void test_invalid_drop_timeout_restores_original_slot() {
  GameStateStore state;
  const auto ore = make_item("Ore", 3001, 0);
  state.world.bag_items[6] = ore;
  state.begin_pending_item_action(PendingItemActionKind::drop, MovingItemSource::bag,
                                  6, -1, ore, 100);
  state.world.bag_items[6] = ItemState{};

  assert(state.pending_item_action_expired(3200));
  state.restore_pending_item_action();
  assert(!state.world.pending_item_action.active);
  assert(state.world.bag_items[6].make_index == 3001);
}

void test_legacy_item_hint_and_moving_overlay_geometry() {
  const auto potion = make_item("Potion", 2001, 0);
  const auto hint = mir2::client::legacy_inventory_ui::legacy_item_hint_text(potion);
  assert(hint == L"Potion\\药品\\持久 0/1");
  assert(mir2::client::legacy_inventory_ui::legacy_hint_to_render_text(hint) ==
         L"Potion\n药品\n持久 0/1");

  const auto rect =
      mir2::client::legacy_inventory_ui::legacy_moving_item_overlay_rect(100, 120, 32, 28);
  assert(rect.x == 84);
  assert(rect.y == 106);
  assert(rect.w == 32);
  assert(rect.h == 28);
}

}  // namespace

int main() {
  test_bag_window_grid_double_click_use_rolls_back();
  test_right_click_menu_drop_and_pending_clear();
  test_equip_unequip_belt_and_durability_state();
  test_invalid_drop_timeout_restores_original_slot();
  test_legacy_item_hint_and_moving_overlay_geometry();
  return 0;
}
