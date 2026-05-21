#include "scene/legacy_ui_lifecycle.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

namespace lifecycle = mir2::client::legacy_ui_lifecycle;

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
    const std::vector<lifecycle::LegacyUiLifecycleTraceLabel>& trace) {
  std::vector<std::string> out;
  for (const auto label : trace) {
    out.emplace_back(lifecycle::legacy_ui_lifecycle_trace_label(label));
  }
  return out;
}

}  // namespace

int main() {
  const auto source_dir = std::filesystem::path{MIR2_CLIENT_SOURCE_DIR};
  const auto sections = read_trace_sections(
      source_dir / "tests" / "golden" / "legacy_ui_lifecycle_expected_trace.txt");

  using Label = lifecycle::LegacyUiLifecycleTraceLabel;
  assert(labels({Label::scene_exit_clears_ui_tree,
                 Label::disconnect_clears_play_ui,
                 Label::destroy_window_clears_focus_capture_modal_hover}) ==
         sections.at("lifecycle.scene_exit"));
  assert(labels({Label::hide_tooltip_when_item_missing_or_moving,
                 Label::restore_pending_item_on_window_close,
                 Label::scene_exit_clears_ui_tree}) ==
         sections.at("lifecycle.tooltip_moving_item"));
  assert(labels({Label::drop_unreachable_world_message,
                 Label::disconnect_clears_play_ui,
                 Label::scene_exit_clears_ui_tree}) ==
         sections.at("lifecycle.protocol_unreachable"));
  assert(labels({Label::show_revive_prompt,
                 Label::send_revive_request,
                 Label::disconnect_clears_play_ui}) ==
         sections.at("lifecycle.revive"));
  return 0;
}
