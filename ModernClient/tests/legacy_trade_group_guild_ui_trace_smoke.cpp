#include "scene/legacy_trade_group_guild_ui.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

namespace social = mir2::client::legacy_trade_group_guild_ui;

std::map<std::string, std::vector<std::string>> read_trace_sections(
    const std::filesystem::path& path) {
  std::ifstream input(path);
  assert(input);

  std::map<std::string, std::vector<std::string>> sections;
  std::string current;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
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

std::vector<std::string> labels(
    const std::vector<social::LegacyTradeGroupGuildUiTraceLabel>& trace) {
  std::vector<std::string> out;
  for (const auto label : trace) {
    out.emplace_back(social::legacy_trade_group_guild_ui_trace_label(label));
  }
  return out;
}

}  // namespace

int main() {
  const auto source_dir = std::filesystem::path{MIR2_CLIENT_SOURCE_DIR};
  const auto sections = read_trace_sections(
      source_dir / "tests" / "golden" / "legacy_trade_group_guild_ui_expected_trace.txt");

  using Label = social::LegacyTradeGroupGuildUiTraceLabel;
  assert(labels({Label::group_window_created,
                 Label::trade_remote_window_created,
                 Label::trade_local_window_created,
                 Label::guild_window_created,
                 Label::social_prompt_created}) == sections.at("social.window.initialize"));
  assert(labels({Label::shortcut_open_group,
                 Label::show_group_window,
                 Label::click_group_allow,
                 Label::send_group_mode,
                 Label::recv_group_state_fifo,
                 Label::refresh_group_window}) == sections.at("group.window.flow"));
  assert(labels({Label::click_group_create,
                 Label::show_group_name_prompt,
                 Label::send_create_group,
                 Label::recv_group_state_fifo,
                 Label::refresh_group_members,
                 Label::click_group_add,
                 Label::show_group_name_prompt,
                 Label::send_add_group_member,
                 Label::click_group_remove,
                 Label::show_group_name_prompt,
                 Label::send_remove_group_member}) == sections.at("group.member_ops"));
  assert(labels({Label::shortcut_open_trade,
                 Label::send_trade_try,
                 Label::recv_trade_state_fifo,
                 Label::show_trade_remote_window,
                 Label::show_trade_local_window,
                 Label::close_trade_window,
                 Label::send_trade_cancel,
                 Label::hide_trade_windows,
                 Label::recv_trade_state_fifo}) == sections.at("trade.window.flow"));
  assert(labels({Label::drop_bag_item_on_trade_grid,
                 Label::send_trade_add_item,
                 Label::recv_trade_state_fifo,
                 Label::refresh_trade_items,
                 Label::drag_trade_item_back_to_bag,
                 Label::send_trade_remove_item,
                 Label::click_trade_gold,
                 Label::show_trade_gold_prompt,
                 Label::send_trade_gold,
                 Label::click_trade_accept,
                 Label::send_trade_accept,
                 Label::refresh_trade_accept}) == sections.at("trade.item_gold_accept"));
  assert(labels({Label::shortcut_open_guild,
                 Label::send_guild_open,
                 Label::recv_guild_state_fifo,
                 Label::show_guild_window,
                 Label::click_guild_home,
                 Label::send_guild_home,
                 Label::click_guild_members,
                 Label::send_guild_members,
                 Label::refresh_guild_lines}) == sections.at("guild.window.flow"));
  assert(labels({Label::click_guild_add_member,
                 Label::show_guild_name_prompt,
                 Label::send_guild_add,
                 Label::recv_guild_state_fifo,
                 Label::refresh_guild_members,
                 Label::click_guild_remove_member,
                 Label::show_guild_name_prompt,
                 Label::send_guild_remove}) == sections.at("guild.member_admin"));
  assert(labels({Label::click_guild_scroll_down,
                 Label::guild_top_line_plus_three,
                 Label::click_guild_scroll_up,
                 Label::guild_top_line_minus_three,
                 Label::click_guild_chat_toggle,
                 Label::show_guild_chat_lines_or_noop}) == sections.at("guild.scroll_chat"));
  assert(labels({Label::recv_group_state_fifo,
                 Label::refresh_group_window,
                 Label::recv_trade_state_fifo,
                 Label::refresh_trade_items,
                 Label::recv_guild_state_fifo,
                 Label::refresh_guild_lines,
                 Label::append_system_message}) == sections.at("social.protocol_fifo"));
  return 0;
}
