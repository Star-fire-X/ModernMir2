#include "scene/legacy_magic_npc_ui.hpp"

namespace mir2::client::legacy_magic_npc_ui {

std::string_view legacy_magic_npc_ui_trace_label(
    const LegacyMagicNpcUiTraceLabel label) {
  switch (label) {
    case LegacyMagicNpcUiTraceLabel::magic_page_controls_created:
      return "magic_page_controls_created";
    case LegacyMagicNpcUiTraceLabel::magic_key_dialog_created:
      return "magic_key_dialog_created";
    case LegacyMagicNpcUiTraceLabel::magic_key_buttons_created:
      return "magic_key_buttons_created";
    case LegacyMagicNpcUiTraceLabel::shortcut_open_magic_page:
      return "shortcut_open_magic_page";
    case LegacyMagicNpcUiTraceLabel::show_state_window_magic_page:
      return "show_state_window_magic_page";
    case LegacyMagicNpcUiTraceLabel::magic_page_controls_visible:
      return "magic_page_controls_visible";
    case LegacyMagicNpcUiTraceLabel::draw_magic_page_rows:
      return "draw_magic_page_rows";
    case LegacyMagicNpcUiTraceLabel::click_magic_row:
      return "click_magic_row";
    case LegacyMagicNpcUiTraceLabel::show_magic_key_modal:
      return "show_magic_key_modal";
    case LegacyMagicNpcUiTraceLabel::select_magic_key:
      return "select_magic_key";
    case LegacyMagicNpcUiTraceLabel::send_clear_conflicting_magic_key:
      return "send_clear_conflicting_magic_key";
    case LegacyMagicNpcUiTraceLabel::send_assign_magic_key:
      return "send_assign_magic_key";
    case LegacyMagicNpcUiTraceLabel::hide_magic_key_modal:
      return "hide_magic_key_modal";
    case LegacyMagicNpcUiTraceLabel::recv_magic_list_fifo:
      return "recv_magic_list_fifo";
    case LegacyMagicNpcUiTraceLabel::refresh_magic_page:
      return "refresh_magic_page";
    case LegacyMagicNpcUiTraceLabel::click_npc_from_scene_when_ui_not_consumed:
      return "click_npc_from_scene_when_ui_not_consumed";
    case LegacyMagicNpcUiTraceLabel::send_npc_click_request:
      return "send_npc_click_request";
    case LegacyMagicNpcUiTraceLabel::recv_npc_dialog_fifo:
      return "recv_npc_dialog_fifo";
    case LegacyMagicNpcUiTraceLabel::show_merchant_dialog:
      return "show_merchant_dialog";
    case LegacyMagicNpcUiTraceLabel::parse_npc_links:
      return "parse_npc_links";
    case LegacyMagicNpcUiTraceLabel::click_npc_link:
      return "click_npc_link";
    case LegacyMagicNpcUiTraceLabel::send_npc_dialog_select:
      return "send_npc_dialog_select";
    case LegacyMagicNpcUiTraceLabel::wait_server_refresh:
      return "wait_server_refresh";
    case LegacyMagicNpcUiTraceLabel::close_npc_dialog_local:
      return "close_npc_dialog_local";
    case LegacyMagicNpcUiTraceLabel::hide_merchant_dialog:
      return "hide_merchant_dialog";
    case LegacyMagicNpcUiTraceLabel::restore_item_bag_origin:
      return "restore_item_bag_origin";
    case LegacyMagicNpcUiTraceLabel::recv_npc_close_fifo:
      return "recv_npc_close_fifo";
    case LegacyMagicNpcUiTraceLabel::recv_merchant_goods_fifo:
      return "recv_merchant_goods_fifo";
    case LegacyMagicNpcUiTraceLabel::show_shop_menu:
      return "show_shop_menu";
    case LegacyMagicNpcUiTraceLabel::move_item_bag_for_shop:
      return "move_item_bag_for_shop";
    case LegacyMagicNpcUiTraceLabel::select_shop_row:
      return "select_shop_row";
    case LegacyMagicNpcUiTraceLabel::send_merchant_buy:
      return "send_merchant_buy";
    case LegacyMagicNpcUiTraceLabel::recv_inventory_or_gold_fifo:
      return "recv_inventory_or_gold_fifo";
    case LegacyMagicNpcUiTraceLabel::refresh_shop_or_bag:
      return "refresh_shop_or_bag";
    case LegacyMagicNpcUiTraceLabel::open_sell_or_repair_selecting:
      return "open_sell_or_repair_selecting";
    case LegacyMagicNpcUiTraceLabel::select_bag_item_for_price:
      return "select_bag_item_for_price";
    case LegacyMagicNpcUiTraceLabel::send_price_request:
      return "send_price_request";
    case LegacyMagicNpcUiTraceLabel::recv_price_result_fifo:
      return "recv_price_result_fifo";
    case LegacyMagicNpcUiTraceLabel::show_sell_dialog:
      return "show_sell_dialog";
    case LegacyMagicNpcUiTraceLabel::confirm_sell_or_repair:
      return "confirm_sell_or_repair";
    case LegacyMagicNpcUiTraceLabel::send_sell_or_repair:
      return "send_sell_or_repair";
    case LegacyMagicNpcUiTraceLabel::refresh_bag_or_gold_fifo:
      return "refresh_bag_or_gold_fifo";
    case LegacyMagicNpcUiTraceLabel::recv_storage_list_fifo:
      return "recv_storage_list_fifo";
    case LegacyMagicNpcUiTraceLabel::show_storage_menu:
      return "show_storage_menu";
    case LegacyMagicNpcUiTraceLabel::select_storage_row:
      return "select_storage_row";
    case LegacyMagicNpcUiTraceLabel::send_storage_withdraw:
      return "send_storage_withdraw";
    case LegacyMagicNpcUiTraceLabel::open_storage_deposit_selecting:
      return "open_storage_deposit_selecting";
    case LegacyMagicNpcUiTraceLabel::select_bag_item_for_storage:
      return "select_bag_item_for_storage";
    case LegacyMagicNpcUiTraceLabel::send_storage_deposit:
      return "send_storage_deposit";
    case LegacyMagicNpcUiTraceLabel::refresh_storage_or_bag:
      return "refresh_storage_or_bag";
  }
  return "";
}

RectI LegacyMagicPageLayout::row_hit_rect(const int row) const {
  return RectI{row_hit_area.x, row_hit_area.y + row * row_height,
               row_hit_area.w, row_height};
}

RectI LegacyMagicPageLayout::icon_rect(const int row) const {
  return RectI{icon_x, first_row_y + row * row_height, icon_w, icon_h};
}

int LegacyMagicPageLayout::key_icon_resource_index(const std::uint8_t key) const {
  if (key < 1 || key > 8) {
    return -1;
  }
  return key_icon_first_resource_index + static_cast<int>(key) - 1;
}

RectI LegacyMagicKeyLayout::key_button(const int key) const {
  const auto clamped = key < 1 ? 1 : key;
  const auto index = clamped - 1;
  const auto row = index / 4;
  const auto col = index % 4;
  return RectI{58 + col * 38, 42 + row * 28, 32, 22};
}

int LegacyMagicKeyLayout::key_resource_index(const int key) const {
  if (key < 1 || key > 8) {
    return -1;
  }
  return key_first_resource_index + key * 2;
}

RectI LegacyMerchantMenuLayout::row_button(const int row) const {
  return RectI{row_draw_x, row_draw_y + row * row_height, 244, 25};
}

LegacyMagicPageLayout legacy_magic_page_layout() { return LegacyMagicPageLayout{}; }
LegacyMagicKeyLayout legacy_magic_key_layout() { return LegacyMagicKeyLayout{}; }
LegacyNpcDialogLayout legacy_npc_dialog_layout() { return LegacyNpcDialogLayout{}; }
LegacyMerchantMenuLayout legacy_merchant_menu_layout() { return LegacyMerchantMenuLayout{}; }
LegacySellDialogLayout legacy_sell_dialog_layout() { return LegacySellDialogLayout{}; }

}  // namespace mir2::client::legacy_magic_npc_ui
