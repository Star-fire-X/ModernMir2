#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "render/software_renderer.hpp"

namespace mir2::client::legacy_magic_npc_ui {

enum class LegacyMagicNpcUiTraceLabel {
  magic_page_controls_created,
  magic_key_dialog_created,
  magic_key_buttons_created,
  shortcut_open_magic_page,
  show_state_window_magic_page,
  magic_page_controls_visible,
  draw_magic_page_rows,
  click_magic_row,
  show_magic_key_modal,
  select_magic_key,
  send_clear_conflicting_magic_key,
  send_assign_magic_key,
  hide_magic_key_modal,
  recv_magic_list_fifo,
  refresh_magic_page,
  click_npc_from_scene_when_ui_not_consumed,
  send_npc_click_request,
  recv_npc_dialog_fifo,
  show_merchant_dialog,
  parse_npc_links,
  click_npc_link,
  send_npc_dialog_select,
  wait_server_refresh,
  close_npc_dialog_local,
  hide_merchant_dialog,
  restore_item_bag_origin,
  recv_npc_close_fifo,
  recv_merchant_goods_fifo,
  show_shop_menu,
  move_item_bag_for_shop,
  select_shop_row,
  send_merchant_buy,
  recv_inventory_or_gold_fifo,
  refresh_shop_or_bag,
  open_sell_or_repair_selecting,
  select_bag_item_for_price,
  send_price_request,
  recv_price_result_fifo,
  show_sell_dialog,
  confirm_sell_or_repair,
  send_sell_or_repair,
  refresh_bag_or_gold_fifo,
  recv_storage_list_fifo,
  show_storage_menu,
  select_storage_row,
  send_storage_withdraw,
  open_storage_deposit_selecting,
  select_bag_item_for_storage,
  send_storage_deposit,
  refresh_storage_or_bag
};

[[nodiscard]] std::string_view legacy_magic_npc_ui_trace_label(
    LegacyMagicNpcUiTraceLabel label);

struct LegacyMagicPageLayout {
  int resource_index{383};
  RectI background{38, 52, 177, 197};
  RectI row_hit_area{33, 55, 166, 185};
  int row_count{5};
  int row_height{37};
  int icon_x{46};
  int first_row_y{60};
  int icon_w{31};
  int icon_h{33};
  int name_x{84};
  int level_icon_resource_index{112};
  int level_icon_x{84};
  int exp_icon_resource_index{111};
  int exp_icon_x{110};
  int key_icon_x{183};
  int key_icon_first_resource_index{248};

  [[nodiscard]] RectI row_hit_rect(int row) const;
  [[nodiscard]] RectI icon_rect(int row) const;
  [[nodiscard]] int key_icon_resource_index(std::uint8_t key) const;
};

struct LegacyMagicKeyLayout {
  int resource_index{229};
  RectI window{289, 234, 222, 132};
  int none_resource_index{230};
  RectI none_button{15, 42, 32, 22};
  int key_first_resource_index{230};
  RectI ok_button{78, 103, 70, 24};
  int ok_resource_index{62};

  [[nodiscard]] RectI key_button(int key) const;
  [[nodiscard]] int key_resource_index(int key) const;
};

struct LegacyNpcDialogLayout {
  int resource_index{384};
  RectI window{0, 0, 420, 180};
  int close_resource_index{64};
  RectI close_button{399, 1, 16, 16};
  int text_x{30};
  int text_y{20};
  int line_height{16};
  int link_hit_height{14};
};

struct LegacyMerchantMenuLayout {
  int resource_index{385};
  RectI normal_window{138, 163, 320, 210};
  RectI shop_window{0, 176, 320, 210};
  RectI row_hit_area{14, 32, 265, 5 * 28};
  int row_count{5};
  int row_height{28};
  int row_draw_x{27};
  int row_draw_y{28};
  int prev_resource_index{387};
  RectI prev_button{43, 175, 40, 24};
  int next_resource_index{388};
  RectI next_button{90, 175, 40, 24};
  int buy_resource_index{386};
  RectI buy_button{215, 171, 50, 28};
  RectI storage_take_button{174, 171, 50, 28};
  RectI storage_deposit_button{224, 171, 50, 28};
  int close_resource_index{64};
  RectI close_button{291, 0, 16, 16};

  [[nodiscard]] RectI row_button(int row) const;
};

struct LegacySellDialogLayout {
  int resource_index{392};
  RectI normal_window{328, 163, 145, 174};
  RectI shop_sell_window{260, 176, 145, 174};
  RectI spot{27, 67, 61, 52};
  int ok_resource_index{393};
  RectI ok_button{85, 150, 60, 24};
  int close_resource_index{64};
  RectI close_button{115, 0, 16, 16};
};

[[nodiscard]] LegacyMagicPageLayout legacy_magic_page_layout();
[[nodiscard]] LegacyMagicKeyLayout legacy_magic_key_layout();
[[nodiscard]] LegacyNpcDialogLayout legacy_npc_dialog_layout();
[[nodiscard]] LegacyMerchantMenuLayout legacy_merchant_menu_layout();
[[nodiscard]] LegacySellDialogLayout legacy_sell_dialog_layout();

}  // namespace mir2::client::legacy_magic_npc_ui
