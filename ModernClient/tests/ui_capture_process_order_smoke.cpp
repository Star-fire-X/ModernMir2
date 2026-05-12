#include "ui/ui.hpp"

#include <cassert>

namespace {

using mir2::client::InputState;
using mir2::client::RectI;
namespace ui = mir2::client::ui;

void test_button_callback_waits_for_dwin_process() {
  ui::UiTree tree;
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
  tree.process_queued_events(press);
  assert(clicks == 0);

  InputState release{};
  release.mouse_x = 10;
  release.mouse_y = 10;
  release.left_released = true;
  result = tree.capture_input(release);
  assert(result.consumed);
  assert(clicks == 0);
  tree.process_queued_events(release);
  assert(clicks == 1);
}

void test_text_edit_focus_is_visible_during_capture() {
  ui::UiTree tree;
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

  InputState text{};
  text.text_input = L"a";
  result = tree.capture_input(text);
  assert(result.text_focus);
  assert(result.consumed);
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
