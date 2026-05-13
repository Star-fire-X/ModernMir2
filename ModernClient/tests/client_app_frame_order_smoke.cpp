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
          [&] { push(calls, "draw_screen"); },
          [&] { push(calls, "dwin_direct_paint"); },
          [&] { push(calls, "draw_screen_top"); },
          [&] { push(calls, "draw_hint"); },
          [&] { push(calls, "draw_moving_item"); },
          [&] { push(calls, "flip"); },
          [] { return true; }});

  const std::vector<std::string> expected{
      "timer1_network_drain", "capture_ui_input", "process_key_messages",
      "process_action_messages", "dwin_process", "draw_screen",
      "dwin_direct_paint", "draw_screen_top", "draw_hint", "draw_moving_item", "flip"};
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
          [&] { push(calls, "draw_screen"); },
          [&] { push(calls, "dwin_direct_paint"); },
          [&] { push(calls, "draw_screen_top"); },
          [&] { push(calls, "draw_hint"); },
          [&] { push(calls, "draw_moving_item"); },
          [&] { push(calls, "flip"); },
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
          [&] { push(calls, "draw_screen"); },
          [&] { push(calls, "dwin_direct_paint"); },
          [&] { push(calls, "draw_screen_top"); },
          [&] { push(calls, "draw_hint"); },
          [&] { push(calls, "draw_moving_item"); },
          [&] { push(calls, "flip"); },
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
