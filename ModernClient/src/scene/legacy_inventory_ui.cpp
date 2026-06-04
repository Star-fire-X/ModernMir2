/**
 * @file legacy_inventory_ui.cpp
 * @brief 旧版背包/装备界面布局实现 —— 背包窗口、装备栏、物品提示的坐标计算
 * @details 实现背包网格（8x5）、装备槽位（9 格）和物品提示的布局计算。
 *          对应 Delphi 客户端的 TItemBagForm 和 TEquipForm 窗口布局。
 */

#include "scene/legacy_inventory_ui.hpp"

#include "text/encoding.hpp"

#include <algorithm>

namespace mir2::client::legacy_inventory_ui {
namespace {

const wchar_t* std_mode_name(const std::uint8_t std_mode) {
  switch (std_mode) {
    case 0:  return L"药品";
    case 1:  return L"食物";
    case 2:  return L"食物";
    case 3:  return L"食物";
    case 4:  return L"技能书";
    case 5:
    case 6:  return L"武器";
    case 10:
    case 11: return L"衣服";
    case 15: return L"头盔";
    case 19:
    case 20:
    case 21: return L"项链";
    case 22:
    case 23: return L"戒指";
    case 24:
    case 26: return L"手镯";
    case 25: return L"毒药";
    case 30: return L"蜡烛";
    case 31: return L"特殊";
    case 40: return L"肉类";
    case 42: return L"酒";
    case 43: return L"矿石";
    default: return L"物品";
  }
}

}  // namespace

std::string_view legacy_inventory_ui_trace_label(
    const LegacyInventoryUiTraceLabel label) {
  switch (label) {
    case LegacyInventoryUiTraceLabel::legacy_inventory_windows_created:
      return "legacy_inventory_windows_created";
    case LegacyInventoryUiTraceLabel::item_bag_window_created:
      return "item_bag_window_created";
    case LegacyInventoryUiTraceLabel::item_grid_created:
      return "item_grid_created";
    case LegacyInventoryUiTraceLabel::state_window_created:
      return "state_window_created";
    case LegacyInventoryUiTraceLabel::equipment_slots_created:
      return "equipment_slots_created";
    case LegacyInventoryUiTraceLabel::item_hint_created:
      return "item_hint_created";
    case LegacyInventoryUiTraceLabel::moving_item_overlay_created:
      return "moving_item_overlay_created";
    case LegacyInventoryUiTraceLabel::show_item_bag:
      return "show_item_bag";
    case LegacyInventoryUiTraceLabel::arrange_item_bag_origin:
      return "arrange_item_bag_origin";
    case LegacyInventoryUiTraceLabel::hide_item_bag:
      return "hide_item_bag";
    case LegacyInventoryUiTraceLabel::show_state_window:
      return "show_state_window";
    case LegacyInventoryUiTraceLabel::hide_state_window:
      return "hide_state_window";
    case LegacyInventoryUiTraceLabel::mouse_down_bag_cell:
      return "mouse_down_bag_cell";
    case LegacyInventoryUiTraceLabel::start_item_moving_from_bag:
      return "start_item_moving_from_bag";
    case LegacyInventoryUiTraceLabel::clear_source_bag_slot:
      return "clear_source_bag_slot";
    case LegacyInventoryUiTraceLabel::draw_moving_item_overlay:
      return "draw_moving_item_overlay";
    case LegacyInventoryUiTraceLabel::cancel_moving_item:
      return "cancel_moving_item";
    case LegacyInventoryUiTraceLabel::restore_item_to_source_slot:
      return "restore_item_to_source_slot";
    case LegacyInventoryUiTraceLabel::double_click_bag_cell:
      return "double_click_bag_cell";
    case LegacyInventoryUiTraceLabel::begin_pending_use_item:
      return "begin_pending_use_item";
    case LegacyInventoryUiTraceLabel::send_use_item:
      return "send_use_item";
    case LegacyInventoryUiTraceLabel::recv_use_item_result_fifo:
      return "recv_use_item_result_fifo";
    case LegacyInventoryUiTraceLabel::clear_or_restore_pending_item:
      return "clear_or_restore_pending_item";
    case LegacyInventoryUiTraceLabel::mouse_down_equipment_slot:
      return "mouse_down_equipment_slot";
    case LegacyInventoryUiTraceLabel::start_item_moving_from_equipment:
      return "start_item_moving_from_equipment";
    case LegacyInventoryUiTraceLabel::clear_source_equipment_slot:
      return "clear_source_equipment_slot";
    case LegacyInventoryUiTraceLabel::drop_equipment_item_on_bag_cell:
      return "drop_equipment_item_on_bag_cell";
    case LegacyInventoryUiTraceLabel::send_takeoff_item:
      return "send_takeoff_item";
    case LegacyInventoryUiTraceLabel::server_equipment_message_fifo:
      return "server_equipment_message_fifo";
    case LegacyInventoryUiTraceLabel::refresh_bag_or_equipment:
      return "refresh_bag_or_equipment";
    case LegacyInventoryUiTraceLabel::drop_bag_item_on_equipment_slot:
      return "drop_bag_item_on_equipment_slot";
    case LegacyInventoryUiTraceLabel::validate_equipment_slot:
      return "validate_equipment_slot";
    case LegacyInventoryUiTraceLabel::begin_pending_equip_item:
      return "begin_pending_equip_item";
    case LegacyInventoryUiTraceLabel::send_takeon_item:
      return "send_takeon_item";
    case LegacyInventoryUiTraceLabel::hover_bag_or_equipment_item:
      return "hover_bag_or_equipment_item";
    case LegacyInventoryUiTraceLabel::format_item_hint_backslash_lines:
      return "format_item_hint_backslash_lines";
    case LegacyInventoryUiTraceLabel::show_hint_layer:
      return "show_hint_layer";
    case LegacyInventoryUiTraceLabel::hide_hint_when_item_missing_or_moving:
      return "hide_hint_when_item_missing_or_moving";
    case LegacyInventoryUiTraceLabel::recv_inventory_message_fifo:
      return "recv_inventory_message_fifo";
    case LegacyInventoryUiTraceLabel::refresh_gold_or_attributes:
      return "refresh_gold_or_attributes";
    case LegacyInventoryUiTraceLabel::append_system_message:
      return "append_system_message";
  }
  return "";
}

LegacyBagLayout legacy_bag_layout() { return LegacyBagLayout{}; }

LegacyEquipmentLayout legacy_equipment_layout(const int frame_width) {
  LegacyEquipmentLayout layout;
  const auto width = frame_width > 0 ? frame_width : 252;
  layout.window = RectI{800 - width, 0, width, 308};
  layout.visible_slots = {RectI{47, 80, 47, 87}, RectI{96, 122, 53, 112},
                          RectI{115, 93, 18, 18}, RectI{168, 87, 34, 31},
                          RectI{168, 125, 34, 31}, RectI{42, 176, 34, 31},
                          RectI{168, 176, 34, 31}, RectI{42, 215, 34, 31},
                          RectI{168, 215, 34, 31}};
  return layout;
}

LegacyItemHintLayout legacy_item_hint_layout() { return LegacyItemHintLayout{}; }

RectI legacy_moving_item_overlay_rect(const int mouse_x, const int mouse_y,
                                      const int width, const int height) {
  return RectI{mouse_x - width / 2, mouse_y - height / 2, width, height};
}

std::wstring legacy_item_hint_text(const client_v1::ItemState& item) {
  auto text = text::utf8_to_wide(item.name);
  text.push_back(L'\\');
  text.append(std_mode_name(item.std_mode));
  if (item.dura_max != 0) {
    text.append(L"\\持久 ");
    text.append(std::to_wstring(item.dura / 1000));
    text.push_back(L'/');
    text.append(std::to_wstring(item.dura_max / 1000));
  }
  return text;
}

std::wstring legacy_hint_to_render_text(std::wstring text) {
  std::replace(text.begin(), text.end(), L'\\', L'\n');
  return text;
}

std::uint32_t legacy_item_hint_color(const std::uint8_t std_mode) {
  if (std_mode >= 25) {
    return 0xFFFACC15U;
  }
  if (std_mode >= 19) {
    return 0xFF60A5FAU;
  }
  if (std_mode >= 10) {
    return 0xFF4ADE80U;
  }
  return 0xFFF5F7FAU;
}

}  // namespace mir2::client::legacy_inventory_ui
