#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "render/software_renderer.hpp"
#include "shared/protocol/client_v1/protocol.hpp"

namespace mir2::client::legacy_inventory_ui {

enum class LegacyInventoryUiTraceLabel {
  legacy_inventory_windows_created,
  item_bag_window_created,
  item_grid_created,
  state_window_created,
  equipment_slots_created,
  item_hint_created,
  moving_item_overlay_created,
  show_item_bag,
  arrange_item_bag_origin,
  hide_item_bag,
  show_state_window,
  hide_state_window,
  mouse_down_bag_cell,
  start_item_moving_from_bag,
  clear_source_bag_slot,
  draw_moving_item_overlay,
  cancel_moving_item,
  restore_item_to_source_slot,
  double_click_bag_cell,
  begin_pending_use_item,
  send_use_item,
  recv_use_item_result_fifo,
  clear_or_restore_pending_item,
  mouse_down_equipment_slot,
  start_item_moving_from_equipment,
  clear_source_equipment_slot,
  drop_equipment_item_on_bag_cell,
  send_takeoff_item,
  server_equipment_message_fifo,
  refresh_bag_or_equipment,
  drop_bag_item_on_equipment_slot,
  validate_equipment_slot,
  begin_pending_equip_item,
  send_takeon_item,
  hover_bag_or_equipment_item,
  format_item_hint_backslash_lines,
  show_hint_layer,
  hide_hint_when_item_missing_or_moving,
  recv_inventory_message_fifo,
  refresh_gold_or_attributes,
  append_system_message
};

[[nodiscard]] std::string_view legacy_inventory_ui_trace_label(
    LegacyInventoryUiTraceLabel label);

struct LegacyBagLayout {
  int resource_index{3};
  RectI window{0, 0, 329, 227};
  RectI grid{20, 13, 286, 162};
  int columns{8};
  int rows{5};
  int cell_width{36};
  int cell_height{32};
  int first_visible_slot{6};
  int last_visible_slot{45};
  int close_resource_index{371};
  RectI close_button{309, 203, 14, 20};
  int repair_resource_index{26};
  RectI repair_button{254, 183, 48, 22};
  int gold_resource_index{29};
  RectI gold_button{10, 190, 30, 20};
  int icon_count_offset_x{22};
  int icon_count_offset_y{20};

  [[nodiscard]] int slot_for_cell(int col, int row) const {
    return first_visible_slot + col + row * columns;
  }
};

struct LegacyEquipmentLayout {
  int resource_index{370};
  RectI window{};
  int close_resource_index{371};
  RectI close_button{8, 39, 14, 20};
  int prev_resource_index{373};
  RectI prev_button{7, 128, 22, 24};
  int next_resource_index{372};
  RectI next_button{7, 187, 22, 24};
  int magic_page_up_resource_index{398};
  RectI magic_page_up_button{213, 113, 22, 24};
  int magic_page_down_resource_index{396};
  RectI magic_page_down_button{213, 143, 22, 24};
  std::array<RectI, 9> visible_slots{};
};

struct LegacyItemHintLayout {
  int mouse_offset_x{12};
  int mouse_offset_y{16};
  wchar_t logical_separator{L'\\'};
  wchar_t render_separator{L'\n'};
};

[[nodiscard]] LegacyBagLayout legacy_bag_layout();
[[nodiscard]] LegacyEquipmentLayout legacy_equipment_layout(int frame_width);
[[nodiscard]] LegacyItemHintLayout legacy_item_hint_layout();
[[nodiscard]] RectI legacy_moving_item_overlay_rect(int mouse_x, int mouse_y, int width,
                                                    int height);
[[nodiscard]] std::wstring legacy_item_hint_text(const client_v1::ItemState& item);
[[nodiscard]] std::wstring legacy_hint_to_render_text(std::wstring text);
[[nodiscard]] std::uint32_t legacy_item_hint_color(std::uint8_t std_mode);

}  // namespace mir2::client::legacy_inventory_ui
