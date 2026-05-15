#include "scene/legacy_play_ui.hpp"

namespace mir2::client::legacy_play_ui {

std::string_view legacy_play_ui_trace_label(const LegacyPlayUiTraceLabel label) {
  switch (label) {
    case LegacyPlayUiTraceLabel::world_scene_enter:
      return "world_scene_enter";
    case LegacyPlayUiTraceLabel::legacy_hud_root_created:
      return "legacy_hud_root_created";
    case LegacyPlayUiTraceLabel::bottom_board_show:
      return "bottom_board_show";
    case LegacyPlayUiTraceLabel::chat_board_show:
      return "chat_board_show";
    case LegacyPlayUiTraceLabel::chat_edit_created:
      return "chat_edit_created";
    case LegacyPlayUiTraceLabel::legacy_shortcut_fallback:
      return "legacy_shortcut_fallback";
    case LegacyPlayUiTraceLabel::shortcut_toggle_bag:
      return "shortcut_toggle_bag";
    case LegacyPlayUiTraceLabel::shortcut_toggle_state:
      return "shortcut_toggle_state";
    case LegacyPlayUiTraceLabel::shortcut_open_magic_page:
      return "shortcut_open_magic_page";
    case LegacyPlayUiTraceLabel::shortcut_open_minimap:
      return "shortcut_open_minimap";
    case LegacyPlayUiTraceLabel::chat_enter_or_space_open:
      return "chat_enter_or_space_open";
    case LegacyPlayUiTraceLabel::chat_native_edit_focus:
      return "chat_native_edit_focus";
    case LegacyPlayUiTraceLabel::chat_enter_submit:
      return "chat_enter_submit";
    case LegacyPlayUiTraceLabel::queue_chat_send:
      return "queue_chat_send";
    case LegacyPlayUiTraceLabel::send_chat:
      return "send_chat";
    case LegacyPlayUiTraceLabel::chat_prefix_open:
      return "chat_prefix_open";
    case LegacyPlayUiTraceLabel::chat_slash_uses_whisper_or_literal:
      return "chat_slash_uses_whisper_or_literal";
    case LegacyPlayUiTraceLabel::chat_escape_cancel:
      return "chat_escape_cancel";
    case LegacyPlayUiTraceLabel::chat_close_hide_edit:
      return "chat_close_hide_edit";
    case LegacyPlayUiTraceLabel::recv_chat_line_fifo:
      return "recv_chat_line_fifo";
    case LegacyPlayUiTraceLabel::append_chat_board_line:
      return "append_chat_board_line";
    case LegacyPlayUiTraceLabel::recv_actor_say_fifo:
      return "recv_actor_say_fifo";
    case LegacyPlayUiTraceLabel::recv_sys_message_fifo:
      return "recv_sys_message_fifo";
    case LegacyPlayUiTraceLabel::append_top_sys_message:
      return "append_top_sys_message";
    case LegacyPlayUiTraceLabel::draw_top_system_messages:
      return "draw_top_system_messages";
    case LegacyPlayUiTraceLabel::expire_top_system_messages_after_3000ms:
      return "expire_top_system_messages_after_3000ms";
    case LegacyPlayUiTraceLabel::paint_bottom_board:
      return "paint_bottom_board";
    case LegacyPlayUiTraceLabel::paint_hp_mp:
      return "paint_hp_mp";
    case LegacyPlayUiTraceLabel::paint_exp_weight_gold:
      return "paint_exp_weight_gold";
    case LegacyPlayUiTraceLabel::paint_quick_belt:
      return "paint_quick_belt";
    case LegacyPlayUiTraceLabel::paint_chat_board:
      return "paint_chat_board";
  }
  return "";
}

LegacyHudLayout legacy_hud_layout(const int bottom_height) {
  LegacyHudLayout layout;
  layout.bottom_board = RectI{0, 600 - bottom_height, 800, bottom_height};
  layout.quick_belt = {RectI{285, 59, 32, 29}, RectI{328, 59, 32, 29},
                       RectI{371, 59, 32, 29}, RectI{415, 59, 32, 29},
                       RectI{459, 59, 32, 29}, RectI{503, 59, 32, 29}};
  layout.minimap_button = RectI{219, 104, 28, 18};
  layout.trade_button = RectI{249, 104, 28, 18};
  layout.guild_button = RectI{279, 104, 28, 18};
  layout.group_button = RectI{309, 104, 28, 18};
  layout.plus_button = RectI{339, 104, 28, 18};
  layout.logout_button = RectI{530, 104, 28, 18};
  layout.exit_button = RectI{560, 104, 28, 18};
  layout.state_button = RectI{643, 61, 38, 38};
  layout.bag_button = RectI{682, 41, 38, 38};
  layout.magic_button = RectI{722, 21, 38, 38};
  layout.option_button = RectI{764, 11, 36, 36};
  return layout;
}

LegacyChatLayout legacy_chat_layout() {
  LegacyChatLayout layout;
  layout.board = RectI{208, 600 - 130, 374, 9 * 12};
  layout.edit = RectI{208, 600 - 19, 387, 12};
  return layout;
}

LegacySystemMessageLayout legacy_system_message_layout() {
  LegacySystemMessageLayout layout;
  layout.bounds = RectI{30, 40, 740, 10 * 16};
  return layout;
}

}  // namespace mir2::client::legacy_play_ui
