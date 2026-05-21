#pragma once

#include <cstdint>
#include <string_view>

#include "render/software_renderer.hpp"

namespace mir2::client::legacy_trade_group_guild_ui {

enum class LegacyTradeGroupGuildUiTraceLabel {
  group_window_created,
  trade_remote_window_created,
  trade_local_window_created,
  guild_window_created,
  social_prompt_created,
  shortcut_open_group,
  show_group_window,
  click_group_allow,
  send_group_mode,
  recv_group_state_fifo,
  refresh_group_window,
  click_group_create,
  show_group_name_prompt,
  send_create_group,
  refresh_group_members,
  click_group_add,
  send_add_group_member,
  click_group_remove,
  send_remove_group_member,
  shortcut_open_trade,
  send_trade_try,
  recv_trade_state_fifo,
  show_trade_remote_window,
  show_trade_local_window,
  close_trade_window,
  send_trade_cancel,
  hide_trade_windows,
  drop_bag_item_on_trade_grid,
  send_trade_add_item,
  refresh_trade_items,
  drag_trade_item_back_to_bag,
  send_trade_remove_item,
  click_trade_gold,
  show_trade_gold_prompt,
  send_trade_gold,
  click_trade_accept,
  send_trade_accept,
  refresh_trade_accept,
  shortcut_open_guild,
  send_guild_open,
  recv_guild_state_fifo,
  show_guild_window,
  click_guild_home,
  send_guild_home,
  click_guild_members,
  send_guild_members,
  refresh_guild_lines,
  click_guild_add_member,
  show_guild_name_prompt,
  send_guild_add,
  refresh_guild_members,
  click_guild_remove_member,
  send_guild_remove,
  click_guild_scroll_down,
  guild_top_line_plus_three,
  click_guild_scroll_up,
  guild_top_line_minus_three,
  click_guild_chat_toggle,
  show_guild_chat_lines_or_noop,
  append_system_message
};

[[nodiscard]] std::string_view legacy_trade_group_guild_ui_trace_label(
    LegacyTradeGroupGuildUiTraceLabel label);

struct LegacyGroupLayout {
  int resource_index{120};
  RectI window{262, 179, 276, 242};
  int close_resource_index{64};
  RectI close_button{260, 0, 16, 16};
  int allow_resource_index{122};
  RectI allow_button{20, 18, 20, 19};
  int create_resource_index{123};
  RectI create_button{22, 203, 72, 19};
  int add_resource_index{124};
  RectI add_button{97, 203, 72, 19};
  int remove_resource_index{125};
  RectI remove_button{172, 203, 72, 19};
  int member_x{28};
  int member_y{80};
  int member_second_column_x{128};
  int member_row_height{16};

  [[nodiscard]] RectI member_text_rect(int index) const;
};

struct LegacyTradeLayout {
  int local_resource_index{389};
  int remote_resource_index{390};
  RectI remote_window{464, 0, 220, 175};
  RectI local_window{464, 160, 236, 175};
  RectI grid{21, 56, 180, 66};
  int grid_columns{5};
  int grid_rows{2};
  int cell_width{36};
  int cell_height{33};
  int ok_resource_index{391};
  RectI ok_button{155, 128, 44, 16};
  int close_resource_index{64};
  RectI close_button{220, 42, 16, 16};
  int gold_resource_index{28};
  RectI gold_button{11, 137, 44, 16};
  int gold_text_x{64};
  int gold_text_y{131};
  int name_center_x{59};
  int name_center_width{106};
  int name_y{6};

  [[nodiscard]] RectI cell_rect(int slot) const;
  [[nodiscard]] int slot_at(int x, int y) const;
};

struct LegacyGuildLayout {
  int resource_index{180};
  RectI window{0, -3, 628, 453};
  int close_resource_index{64};
  RectI close_button{584, 6, 16, 16};
  int home_resource_index{198};
  RectI home_button{13, 411, 64, 16};
  int list_resource_index{200};
  RectI list_button{13, 429, 64, 16};
  int chat_resource_index{190};
  RectI chat_button{94, 429, 64, 16};
  int add_resource_index{182};
  RectI add_button{243, 411, 64, 16};
  int remove_resource_index{192};
  RectI remove_button{243, 429, 64, 16};
  int edit_notice_resource_index{196};
  RectI edit_notice_button{325, 411, 64, 16};
  int edit_grade_resource_index{194};
  RectI edit_grade_button{325, 429, 64, 16};
  int ally_resource_index{184};
  RectI ally_button{407, 411, 64, 16};
  int break_ally_resource_index{186};
  RectI break_ally_button{407, 429, 64, 16};
  int war_resource_index{202};
  RectI war_button{529, 411, 64, 16};
  int cancel_war_resource_index{188};
  RectI cancel_war_button{529, 429, 64, 16};
  int up_resource_index{373};
  RectI up_button{595, 239, 16, 16};
  int down_resource_index{372};
  RectI down_button{595, 291, 16, 16};
  int title_x{320};
  int title_y{13};
  int line_x{24};
  int line_y{41};
  int line_height{14};
  int max_line_pixel_height{356};
  int scroll_step{3};

  [[nodiscard]] int max_visible_lines() const;
};

struct LegacySocialPromptLayout {
  RectI window{244, 244, 312, 112};
  RectI edit{28, 42, 196, 24};
  RectI ok_button{232, 38, 54, 24};
  RectI cancel_button{232, 68, 54, 24};
};

[[nodiscard]] LegacyGroupLayout legacy_group_layout();
[[nodiscard]] LegacyTradeLayout legacy_trade_layout();
[[nodiscard]] LegacyGuildLayout legacy_guild_layout();
[[nodiscard]] LegacySocialPromptLayout legacy_social_prompt_layout();

}  // namespace mir2::client::legacy_trade_group_guild_ui
