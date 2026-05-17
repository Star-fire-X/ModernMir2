#include "scene/legacy_trade_group_guild_ui.hpp"

namespace mir2::client::legacy_trade_group_guild_ui {

std::string_view legacy_trade_group_guild_ui_trace_label(
    const LegacyTradeGroupGuildUiTraceLabel label) {
  switch (label) {
    case LegacyTradeGroupGuildUiTraceLabel::group_window_created:
      return "group_window_created";
    case LegacyTradeGroupGuildUiTraceLabel::trade_remote_window_created:
      return "trade_remote_window_created";
    case LegacyTradeGroupGuildUiTraceLabel::trade_local_window_created:
      return "trade_local_window_created";
    case LegacyTradeGroupGuildUiTraceLabel::guild_window_created:
      return "guild_window_created";
    case LegacyTradeGroupGuildUiTraceLabel::social_prompt_created:
      return "social_prompt_created";
    case LegacyTradeGroupGuildUiTraceLabel::shortcut_open_group:
      return "shortcut_open_group";
    case LegacyTradeGroupGuildUiTraceLabel::show_group_window:
      return "show_group_window";
    case LegacyTradeGroupGuildUiTraceLabel::click_group_allow:
      return "click_group_allow";
    case LegacyTradeGroupGuildUiTraceLabel::send_group_mode:
      return "send_group_mode";
    case LegacyTradeGroupGuildUiTraceLabel::recv_group_state_fifo:
      return "recv_group_state_fifo";
    case LegacyTradeGroupGuildUiTraceLabel::refresh_group_window:
      return "refresh_group_window";
    case LegacyTradeGroupGuildUiTraceLabel::click_group_create:
      return "click_group_create";
    case LegacyTradeGroupGuildUiTraceLabel::show_group_name_prompt:
      return "show_group_name_prompt";
    case LegacyTradeGroupGuildUiTraceLabel::send_create_group:
      return "send_create_group";
    case LegacyTradeGroupGuildUiTraceLabel::refresh_group_members:
      return "refresh_group_members";
    case LegacyTradeGroupGuildUiTraceLabel::click_group_add:
      return "click_group_add";
    case LegacyTradeGroupGuildUiTraceLabel::send_add_group_member:
      return "send_add_group_member";
    case LegacyTradeGroupGuildUiTraceLabel::click_group_remove:
      return "click_group_remove";
    case LegacyTradeGroupGuildUiTraceLabel::send_remove_group_member:
      return "send_remove_group_member";
    case LegacyTradeGroupGuildUiTraceLabel::shortcut_open_trade:
      return "shortcut_open_trade";
    case LegacyTradeGroupGuildUiTraceLabel::send_trade_try:
      return "send_trade_try";
    case LegacyTradeGroupGuildUiTraceLabel::recv_trade_state_fifo:
      return "recv_trade_state_fifo";
    case LegacyTradeGroupGuildUiTraceLabel::show_trade_remote_window:
      return "show_trade_remote_window";
    case LegacyTradeGroupGuildUiTraceLabel::show_trade_local_window:
      return "show_trade_local_window";
    case LegacyTradeGroupGuildUiTraceLabel::close_trade_window:
      return "close_trade_window";
    case LegacyTradeGroupGuildUiTraceLabel::send_trade_cancel:
      return "send_trade_cancel";
    case LegacyTradeGroupGuildUiTraceLabel::hide_trade_windows:
      return "hide_trade_windows";
    case LegacyTradeGroupGuildUiTraceLabel::drop_bag_item_on_trade_grid:
      return "drop_bag_item_on_trade_grid";
    case LegacyTradeGroupGuildUiTraceLabel::send_trade_add_item:
      return "send_trade_add_item";
    case LegacyTradeGroupGuildUiTraceLabel::refresh_trade_items:
      return "refresh_trade_items";
    case LegacyTradeGroupGuildUiTraceLabel::drag_trade_item_back_to_bag:
      return "drag_trade_item_back_to_bag";
    case LegacyTradeGroupGuildUiTraceLabel::send_trade_remove_item:
      return "send_trade_remove_item";
    case LegacyTradeGroupGuildUiTraceLabel::click_trade_gold:
      return "click_trade_gold";
    case LegacyTradeGroupGuildUiTraceLabel::show_trade_gold_prompt:
      return "show_trade_gold_prompt";
    case LegacyTradeGroupGuildUiTraceLabel::send_trade_gold:
      return "send_trade_gold";
    case LegacyTradeGroupGuildUiTraceLabel::click_trade_accept:
      return "click_trade_accept";
    case LegacyTradeGroupGuildUiTraceLabel::send_trade_accept:
      return "send_trade_accept";
    case LegacyTradeGroupGuildUiTraceLabel::refresh_trade_accept:
      return "refresh_trade_accept";
    case LegacyTradeGroupGuildUiTraceLabel::shortcut_open_guild:
      return "shortcut_open_guild";
    case LegacyTradeGroupGuildUiTraceLabel::send_guild_open:
      return "send_guild_open";
    case LegacyTradeGroupGuildUiTraceLabel::recv_guild_state_fifo:
      return "recv_guild_state_fifo";
    case LegacyTradeGroupGuildUiTraceLabel::show_guild_window:
      return "show_guild_window";
    case LegacyTradeGroupGuildUiTraceLabel::click_guild_home:
      return "click_guild_home";
    case LegacyTradeGroupGuildUiTraceLabel::send_guild_home:
      return "send_guild_home";
    case LegacyTradeGroupGuildUiTraceLabel::click_guild_members:
      return "click_guild_members";
    case LegacyTradeGroupGuildUiTraceLabel::send_guild_members:
      return "send_guild_members";
    case LegacyTradeGroupGuildUiTraceLabel::refresh_guild_lines:
      return "refresh_guild_lines";
    case LegacyTradeGroupGuildUiTraceLabel::click_guild_add_member:
      return "click_guild_add_member";
    case LegacyTradeGroupGuildUiTraceLabel::show_guild_name_prompt:
      return "show_guild_name_prompt";
    case LegacyTradeGroupGuildUiTraceLabel::send_guild_add:
      return "send_guild_add";
    case LegacyTradeGroupGuildUiTraceLabel::refresh_guild_members:
      return "refresh_guild_members";
    case LegacyTradeGroupGuildUiTraceLabel::click_guild_remove_member:
      return "click_guild_remove_member";
    case LegacyTradeGroupGuildUiTraceLabel::send_guild_remove:
      return "send_guild_remove";
    case LegacyTradeGroupGuildUiTraceLabel::click_guild_scroll_down:
      return "click_guild_scroll_down";
    case LegacyTradeGroupGuildUiTraceLabel::guild_top_line_plus_three:
      return "guild_top_line_plus_three";
    case LegacyTradeGroupGuildUiTraceLabel::click_guild_scroll_up:
      return "click_guild_scroll_up";
    case LegacyTradeGroupGuildUiTraceLabel::guild_top_line_minus_three:
      return "guild_top_line_minus_three";
    case LegacyTradeGroupGuildUiTraceLabel::click_guild_chat_toggle:
      return "click_guild_chat_toggle";
    case LegacyTradeGroupGuildUiTraceLabel::show_guild_chat_lines_or_noop:
      return "show_guild_chat_lines_or_noop";
    case LegacyTradeGroupGuildUiTraceLabel::append_system_message:
      return "append_system_message";
  }
  return "";
}

RectI LegacyGroupLayout::member_text_rect(const int index) const {
  if (index <= 0) {
    return RectI{member_x, member_y, 210, member_row_height};
  }
  const auto adjusted = index - 1;
  return RectI{member_x + (adjusted % 2) * (member_second_column_x - member_x),
               member_y + member_row_height + (adjusted / 2) * member_row_height,
               96,
               member_row_height};
}

RectI LegacyTradeLayout::cell_rect(const int slot) const {
  return RectI{grid.x + (slot % grid_columns) * cell_width,
               grid.y + (slot / grid_columns) * cell_height,
               cell_width,
               cell_height};
}

int LegacyTradeLayout::slot_at(const int x, const int y) const {
  if (x < grid.x || y < grid.y || x >= grid.x + grid.w || y >= grid.y + grid.h) {
    return -1;
  }
  const auto col = (x - grid.x) / cell_width;
  const auto row = (y - grid.y) / cell_height;
  const auto slot = row * grid_columns + col;
  return slot >= 0 && slot < grid_columns * grid_rows ? slot : -1;
}

int LegacyGuildLayout::max_visible_lines() const {
  return max_line_pixel_height / line_height + 1;
}

LegacyGroupLayout legacy_group_layout() { return LegacyGroupLayout{}; }
LegacyTradeLayout legacy_trade_layout() { return LegacyTradeLayout{}; }
LegacyGuildLayout legacy_guild_layout() { return LegacyGuildLayout{}; }
LegacySocialPromptLayout legacy_social_prompt_layout() { return LegacySocialPromptLayout{}; }

}  // namespace mir2::client::legacy_trade_group_guild_ui
