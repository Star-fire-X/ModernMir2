#include "app/legacy_frame_scheduler.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

std::map<std::string, std::vector<std::string>> read_trace_sections() {
  const auto path = std::filesystem::path{MIR2_CLIENT_SOURCE_DIR} / "tests" / "golden" /
                    "client_migration_pr1_expected_trace.txt";
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

void push(std::vector<std::string>& calls, const char* name) { calls.emplace_back(name); }

std::vector<std::string> run_frame_trace(const bool force_render_due, const bool can_draw) {
  mir2::client::LegacyFrameScheduler scheduler;
  if (force_render_due) {
    scheduler.force_render_due_for_test();
  }

  std::vector<std::string> calls;
  scheduler.run_frame(
      0.0F,
      mir2::client::LegacyFrameScheduler::Hooks{
          [&] { push(calls, "timer1_network_drain"); },
          [&] { push(calls, "capture_ui_input"); },
          [&] { push(calls, "process_key_messages"); },
          [&] { push(calls, "process_action_messages"); },
          [&] { push(calls, "dwin_process"); },
          [&] { push(calls, "scene_run"); },
          [&] { push(calls, "draw_screen"); },
          [&] { push(calls, "dwin_direct_paint"); },
          [&] { push(calls, "draw_screen_top"); },
          [&] { push(calls, "draw_hint"); },
          [&] { push(calls, "draw_moving_item"); },
          [&] { push(calls, "flip"); },
          [can_draw] { return can_draw; }});
  return calls;
}

}  // namespace

int main() {
  const auto sections = read_trace_sections();
  assert(run_frame_trace(true, true) == sections.at("frame.render_due"));
  assert(run_frame_trace(false, true) == sections.at("frame.not_render_due"));
  assert(run_frame_trace(true, false) == sections.at("frame.cannot_draw"));
  return 0;
}
