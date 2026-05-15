#include "scene/legacy_inventory_ui.hpp"

#include <cassert>

namespace {

using mir2::client::RectI;
namespace inv = mir2::client::legacy_inventory_ui;

bool same(const RectI lhs, const RectI rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.w == rhs.w && lhs.h == rhs.h;
}

mir2::client_v1::ItemState make_item() {
  mir2::client_v1::ItemState item;
  item.name = "Potion";
  item.make_index = 1001;
  item.looks = 1;
  item.std_mode = 0;
  item.dura = 2000;
  item.dura_max = 5000;
  return item;
}

}  // namespace

int main() {
  const auto bag = inv::legacy_bag_layout();
  assert(bag.resource_index == 3);
  assert(same(bag.window, RectI{0, 0, 329, 227}));
  assert(same(bag.grid, RectI{20, 13, 286, 162}));
  assert(bag.columns == 8);
  assert(bag.rows == 5);
  assert(bag.cell_width == 36);
  assert(bag.cell_height == 32);
  assert(bag.first_visible_slot == 6);
  assert(bag.last_visible_slot == 45);
  assert(bag.slot_for_cell(0, 0) == 6);
  assert(bag.slot_for_cell(7, 4) == 45);
  assert(same(bag.close_button, RectI{309, 203, 14, 20}));
  assert(same(bag.repair_button, RectI{254, 183, 48, 22}));
  assert(same(bag.gold_button, RectI{10, 190, 30, 20}));

  const auto equip = inv::legacy_equipment_layout(252);
  assert(equip.resource_index == 370);
  assert(same(equip.window, RectI{548, 0, 252, 308}));
  assert(same(equip.close_button, RectI{8, 39, 14, 20}));
  assert(same(equip.prev_button, RectI{7, 128, 22, 24}));
  assert(same(equip.next_button, RectI{7, 187, 22, 24}));
  assert(same(equip.magic_page_up_button, RectI{213, 113, 22, 24}));
  assert(same(equip.magic_page_down_button, RectI{213, 143, 22, 24}));
  assert(same(equip.visible_slots[0], RectI{47, 80, 47, 87}));
  assert(same(equip.visible_slots[8], RectI{168, 215, 34, 31}));

  const auto hint = inv::legacy_item_hint_layout();
  assert(hint.mouse_offset_x == 12);
  assert(hint.mouse_offset_y == 16);
  assert(hint.logical_separator == L'\\');
  assert(hint.render_separator == L'\n');

  assert(same(inv::legacy_moving_item_overlay_rect(100, 120, 32, 28),
              RectI{84, 106, 32, 28}));
  const auto item = make_item();
  assert(inv::legacy_item_hint_text(item) == L"Potion\\药品\\持久 2/5");
  assert(inv::legacy_hint_to_render_text(L"A\\B") == L"A\nB");
  assert(inv::legacy_item_hint_color(0) == 0xFFF5F7FAU);
  assert(inv::legacy_item_hint_color(19) == 0xFF60A5FAU);
  return 0;
}
