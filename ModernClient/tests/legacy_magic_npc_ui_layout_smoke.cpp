#include "scene/legacy_magic_npc_ui.hpp"

#include <cassert>

namespace {

using mir2::client::RectI;
namespace mn = mir2::client::legacy_magic_npc_ui;

bool same(const RectI lhs, const RectI rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.w == rhs.w && lhs.h == rhs.h;
}

}  // namespace

int main() {
  const auto magic = mn::legacy_magic_page_layout();
  assert(magic.resource_index == 383);
  assert(same(magic.background, RectI{38, 52, 177, 197}));
  assert(same(magic.row_hit_area, RectI{33, 55, 166, 185}));
  assert(same(magic.row_hit_rect(0), RectI{33, 55, 166, 37}));
  assert(same(magic.row_hit_rect(4), RectI{33, 203, 166, 37}));
  assert(same(magic.icon_rect(0), RectI{46, 60, 31, 33}));
  assert(same(magic.icon_rect(4), RectI{46, 208, 31, 33}));
  assert(magic.key_icon_resource_index(1) == 248);
  assert(magic.key_icon_resource_index(8) == 255);
  assert(magic.key_icon_resource_index(0) == -1);

  const auto key = mn::legacy_magic_key_layout();
  assert(key.resource_index == 229);
  assert(same(key.window, RectI{289, 234, 222, 132}));
  assert(same(key.none_button, RectI{15, 42, 32, 22}));
  assert(same(key.key_button(1), RectI{58, 42, 32, 22}));
  assert(same(key.key_button(4), RectI{172, 42, 32, 22}));
  assert(same(key.key_button(5), RectI{58, 70, 32, 22}));
  assert(same(key.key_button(8), RectI{172, 70, 32, 22}));
  assert(key.key_resource_index(1) == 232);
  assert(key.key_resource_index(8) == 246);
  assert(same(key.ok_button, RectI{78, 103, 70, 24}));

  const auto npc = mn::legacy_npc_dialog_layout();
  assert(npc.resource_index == 384);
  assert(same(npc.window, RectI{0, 0, 420, 180}));
  assert(same(npc.close_button, RectI{399, 1, 16, 16}));
  assert(npc.text_x == 30);
  assert(npc.text_y == 20);
  assert(npc.line_height == 16);
  assert(npc.link_hit_height == 14);

  const auto menu = mn::legacy_merchant_menu_layout();
  assert(menu.resource_index == 385);
  assert(same(menu.normal_window, RectI{138, 163, 320, 210}));
  assert(same(menu.shop_window, RectI{0, 176, 320, 210}));
  assert(same(menu.row_hit_area, RectI{14, 32, 265, 140}));
  assert(same(menu.row_button(0), RectI{27, 28, 244, 25}));
  assert(same(menu.row_button(4), RectI{27, 140, 244, 25}));
  assert(same(menu.prev_button, RectI{43, 175, 40, 24}));
  assert(same(menu.next_button, RectI{90, 175, 40, 24}));
  assert(same(menu.buy_button, RectI{215, 171, 50, 28}));
  assert(same(menu.storage_take_button, RectI{174, 171, 50, 28}));
  assert(same(menu.storage_deposit_button, RectI{224, 171, 50, 28}));
  assert(same(menu.close_button, RectI{291, 0, 16, 16}));

  const auto sell = mn::legacy_sell_dialog_layout();
  assert(sell.resource_index == 392);
  assert(same(sell.normal_window, RectI{328, 163, 145, 174}));
  assert(same(sell.shop_sell_window, RectI{260, 176, 145, 174}));
  assert(same(sell.spot, RectI{27, 67, 61, 52}));
  assert(same(sell.ok_button, RectI{85, 150, 60, 24}));
  assert(same(sell.close_button, RectI{115, 0, 16, 16}));
  return 0;
}
