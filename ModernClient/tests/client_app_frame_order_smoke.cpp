#include "app/legacy_frame_scheduler.hpp"

#include <cassert>
#include <string>
#include <vector>

namespace {

void push(std::vector<std::string>& calls, const char* name) { calls.emplace_back(name); }

void test_render_due_order() {
  mir2::client::LegacyFrameScheduler scheduler;
  scheduler.force_render_due_for_test();
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
          [&] { push(calls, "render_scene"); },
          [&] { push(calls, "paint_ui"); },
          [&] { push(calls, "render_modal"); },
          [&] { push(calls, "present"); },
          [] { return true; }});

  const std::vector<std::string> expected{
      "timer1_network_drain", "capture_ui_input", "process_key_messages",
      "process_action_messages", "dwin_process", "render_scene",
      "paint_ui", "render_modal", "present"};
  assert(calls == expected);
}

void test_non_render_runs_scene() {
  mir2::client::LegacyFrameScheduler scheduler;
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
          [&] { push(calls, "render_scene"); },
          [&] { push(calls, "paint_ui"); },
          [&] { push(calls, "render_modal"); },
          [&] { push(calls, "present"); },
          [] { return true; }});

  const std::vector<std::string> expected{
      "timer1_network_drain", "capture_ui_input", "process_key_messages",
      "process_action_messages", "dwin_process", "scene_run"};
  assert(calls == expected);
}

void test_can_draw_false_runs_scene() {
  mir2::client::LegacyFrameScheduler scheduler;
  scheduler.force_render_due_for_test();
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
          [&] { push(calls, "render_scene"); },
          [&] { push(calls, "paint_ui"); },
          [&] { push(calls, "render_modal"); },
          [&] { push(calls, "present"); },
          [] { return false; }});

  const std::vector<std::string> expected{
      "timer1_network_drain", "capture_ui_input", "process_key_messages",
      "process_action_messages", "dwin_process", "scene_run"};
  assert(calls == expected);
}

}  // namespace

int main() {
  test_render_due_order();
  test_non_render_runs_scene();
  test_can_draw_false_runs_scene();
  return 0;
}
