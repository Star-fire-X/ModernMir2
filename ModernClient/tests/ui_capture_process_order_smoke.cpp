#include "ui/ui.hpp"

#include <cassert>
#include <string>
#include <string_view>
#include <vector>

namespace {

using mir2::client::InputState;
using mir2::client::RectI;
namespace ui = mir2::client::ui;

void test_button_callback_waits_for_dwin_process() {
  ui::UiTree tree;
  std::vector<std::string> trace;
  tree.set_trace_callback([&trace](const std::string_view label) { trace.emplace_back(label); });
  auto* root = tree.set_root<ui::UiNode>(RectI{0, 0, 100, 100});
  auto* button = root->emplace_child<ui::Button>(RectI{0, 0, 40, 20});
  int clicks = 0;
  button->on_click = [&] { ++clicks; };

  InputState press{};
  press.mouse_x = 10;
  press.mouse_y = 10;
  press.left_pressed = true;
  press.left_down = true;
  auto result = tree.capture_input(press);
  assert(result.consumed);
  assert(clicks == 0);
  assert(!trace.empty());
  assert(trace.front() == "cleanup_stale_active_menu_modal_capture");
  assert(trace.back() == "consume_ui_hit_blocks_scene");
  tree.process_queued_events(press);
  assert(clicks == 0);

  trace.clear();
  InputState release{};
  release.mouse_x = 10;
  release.mouse_y = 10;
  release.left_released = true;
  result = tree.capture_input(release);
  assert(result.consumed);
  assert(clicks == 0);
  assert(!trace.empty());
  assert(trace.front() == "cleanup_stale_active_menu_modal_capture");
  assert(trace.back() == "window_close_releases_capture_focus_tooltip");
  tree.process_queued_events(release);
  assert(clicks == 1);
}

void test_text_edit_focus_is_visible_during_capture() {
  ui::UiTree tree;
  std::vector<std::string> trace;
  tree.set_trace_callback([&trace](const std::string_view label) { trace.emplace_back(label); });
  auto* root = tree.set_root<ui::UiNode>(RectI{0, 0, 100, 100});
  auto* edit = root->emplace_child<ui::TextEdit>(RectI{0, 0, 80, 20});

  InputState press{};
  press.mouse_x = 4;
  press.mouse_y = 4;
  press.left_pressed = true;
  press.left_down = true;
  auto result = tree.capture_input(press);
  assert(result.consumed);
  assert(result.text_focus);
  assert(tree.focused() == edit);
  tree.process_queued_events(press);

  trace.clear();
  InputState text{};
  text.text_input = L"a";
  result = tree.capture_input(text);
  assert(result.text_focus);
  assert(result.consumed);
  assert(trace.size() == 5U);
  assert(trace.front() == "active_menu_key_first");
  assert(trace.back() == "chat_escape_cancel");
  assert(edit->value.empty());
  tree.process_queued_events(text);
  assert(edit->value == L"a");
}

}  // namespace

int main() {
  test_button_callback_waits_for_dwin_process();
  test_text_edit_focus_is_visible_during_capture();
  return 0;
}
