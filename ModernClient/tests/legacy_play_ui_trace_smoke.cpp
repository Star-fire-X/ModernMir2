#include "scene/legacy_play_ui.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

namespace play = mir2::client::legacy_play_ui;

std::map<std::string, std::vector<std::string>> read_trace_sections(
    const std::filesystem::path& path) {
  std::ifstream input(path);
  assert(input);

  std::map<std::string, std::vector<std::string>> sections;
  std::string current;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line.front() == '#') {
      continue;
    }
    if (line.front() == '[' && line.back() == ']') {
      current = line.substr(1, line.size() - 2);
      sections[current] = {};
      continue;
    }
    assert(!current.empty());
    sections[current].push_back(line);
  }
  return sections;
}

std::vector<std::string> labels(const std::vector<play::LegacyPlayUiTraceLabel>& trace) {
  std::vector<std::string> out;
  for (const auto label : trace) {
    out.emplace_back(play::legacy_play_ui_trace_label(label));
  }
  return out;
}

}  // namespace

int main() {
  const auto source_dir = std::filesystem::path{MIR2_CLIENT_SOURCE_DIR};
  const auto sections =
      read_trace_sections(source_dir / "tests" / "golden" / "legacy_play_ui_expected_trace.txt");

  assert(labels({play::LegacyPlayUiTraceLabel::world_scene_enter,
                 play::LegacyPlayUiTraceLabel::legacy_hud_root_created,
                 play::LegacyPlayUiTraceLabel::bottom_board_show,
                 play::LegacyPlayUiTraceLabel::chat_board_show,
                 play::LegacyPlayUiTraceLabel::chat_edit_created}) ==
         sections.at("play.hud.initialize"));
  assert(labels({play::LegacyPlayUiTraceLabel::legacy_shortcut_fallback,
                 play::LegacyPlayUiTraceLabel::shortcut_toggle_bag,
                 play::LegacyPlayUiTraceLabel::shortcut_toggle_state,
                 play::LegacyPlayUiTraceLabel::shortcut_open_magic_page,
                 play::LegacyPlayUiTraceLabel::shortcut_open_minimap}) ==
         sections.at("play.hud.shortcuts"));
  assert(labels({play::LegacyPlayUiTraceLabel::chat_enter_or_space_open,
                 play::LegacyPlayUiTraceLabel::chat_native_edit_focus,
                 play::LegacyPlayUiTraceLabel::chat_enter_submit,
                 play::LegacyPlayUiTraceLabel::queue_chat_send,
                 play::LegacyPlayUiTraceLabel::send_chat}) ==
         sections.at("play.chat.open_submit"));
  assert(labels({play::LegacyPlayUiTraceLabel::chat_prefix_open,
                 play::LegacyPlayUiTraceLabel::chat_slash_uses_whisper_or_literal,
                 play::LegacyPlayUiTraceLabel::chat_escape_cancel,
                 play::LegacyPlayUiTraceLabel::chat_close_hide_edit}) ==
         sections.at("play.chat.prefix_cancel"));
  assert(labels({play::LegacyPlayUiTraceLabel::recv_chat_line_fifo,
                 play::LegacyPlayUiTraceLabel::append_chat_board_line,
                 play::LegacyPlayUiTraceLabel::recv_actor_say_fifo,
                 play::LegacyPlayUiTraceLabel::append_chat_board_line,
                 play::LegacyPlayUiTraceLabel::recv_sys_message_fifo,
                 play::LegacyPlayUiTraceLabel::append_chat_board_line,
                 play::LegacyPlayUiTraceLabel::append_top_sys_message}) ==
         sections.at("play.chat.receive_fifo"));
  assert(labels({play::LegacyPlayUiTraceLabel::draw_top_system_messages,
                 play::LegacyPlayUiTraceLabel::expire_top_system_messages_after_3000ms}) ==
         sections.at("play.system_message.draw_top"));
  assert(labels({play::LegacyPlayUiTraceLabel::paint_bottom_board,
                 play::LegacyPlayUiTraceLabel::paint_hp_mp,
                 play::LegacyPlayUiTraceLabel::paint_exp_weight_gold,
                 play::LegacyPlayUiTraceLabel::paint_quick_belt,
                 play::LegacyPlayUiTraceLabel::paint_chat_board}) ==
         sections.at("play.hud.paint"));
  return 0;
}
