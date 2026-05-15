#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "render/software_renderer.hpp"

namespace mir2::client::legacy_play_ui {

enum class LegacyPlayUiTraceLabel {
  world_scene_enter,
  legacy_hud_root_created,
  bottom_board_show,
  chat_board_show,
  chat_edit_created,
  legacy_shortcut_fallback,
  shortcut_toggle_bag,
  shortcut_toggle_state,
  shortcut_open_magic_page,
  shortcut_open_minimap,
  chat_enter_or_space_open,
  chat_native_edit_focus,
  chat_enter_submit,
  queue_chat_send,
  send_chat,
  chat_prefix_open,
  chat_slash_uses_whisper_or_literal,
  chat_escape_cancel,
  chat_close_hide_edit,
  recv_chat_line_fifo,
  append_chat_board_line,
  recv_actor_say_fifo,
  recv_sys_message_fifo,
  append_top_sys_message,
  draw_top_system_messages,
  expire_top_system_messages_after_3000ms,
  paint_bottom_board,
  paint_hp_mp,
  paint_exp_weight_gold,
  paint_quick_belt,
  paint_chat_board
};

[[nodiscard]] std::string_view legacy_play_ui_trace_label(LegacyPlayUiTraceLabel label);

struct LegacyHudLayout {
  RectI bottom_board{};
  std::array<RectI, 6> quick_belt{};
  RectI state_button{};
  RectI bag_button{};
  RectI magic_button{};
  RectI option_button{};
  RectI minimap_button{};
  RectI trade_button{};
  RectI guild_button{};
  RectI group_button{};
  RectI plus_button{};
  RectI logout_button{};
  RectI exit_button{};
};

struct LegacyChatLayout {
  RectI board{};
  RectI edit{};
  int visible_lines{9};
  int line_height{12};
  int max_length{70};
};

struct LegacySystemMessageLayout {
  RectI bounds{};
  int line_height{16};
  std::size_t max_messages{10};
  std::uint64_t expire_ms{3000};
  std::uint32_t color{0xFF00FF00U};
};

[[nodiscard]] LegacyHudLayout legacy_hud_layout(int bottom_height);
[[nodiscard]] LegacyChatLayout legacy_chat_layout();
[[nodiscard]] LegacySystemMessageLayout legacy_system_message_layout();

}  // namespace mir2::client::legacy_play_ui
