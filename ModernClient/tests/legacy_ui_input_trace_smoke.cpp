#include "ui/legacy_ui.hpp"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

using mir2::client::InputState;
using mir2::client::RectI;
namespace ui = mir2::client::ui;

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

std::vector<std::string> keyboard_prefix(
    const std::map<std::string, std::vector<std::string>>& sections) {
  const auto& keyboard = sections.at("ui.input.keyboard");
  assert(keyboard.size() == 6U);
  return std::vector<std::string>(keyboard.begin(), std::prev(keyboard.end()));
}

void click(ui::LegacyUiManager& manager, const int x, const int y) {
  InputState press{};
  press.mouse_x = x;
  press.mouse_y = y;
  press.left_pressed = true;
  press.left_down = true;
  manager.capture_input(press);
  manager.process_queued_events(press);

  InputState release{};
  release.mouse_x = x;
  release.mouse_y = y;
  release.left_released = true;
  manager.capture_input(release);
  manager.process_queued_events(release);
}

void test_mouse_down_trace_blocks_scene(
    const std::map<std::string, std::vector<std::string>>& sections) {
  ui::LegacyUiManager manager;
  manager.add_window<ui::LegacyWindow>(RectI{10, 10, 80, 40});
  manager.trace().clear();

  InputState input{};
  input.mouse_x = 20;
  input.mouse_y = 20;
  input.left_pressed = true;
  input.left_down = true;
  const auto result = manager.capture_input(input);

  assert(result.consumed);
  assert(manager.trace().events() == sections.at("ui.input.mouse_down"));
}

void test_mouse_move_trace_prefers_capture(
    const std::map<std::string, std::vector<std::string>>& sections) {
  ui::LegacyUiManager manager;
  auto* button = manager.add_window<ui::LegacyButton>(RectI{0, 0, 20, 20});
  manager.set_capture(button);
  manager.trace().clear();

  InputState input{};
  input.mouse_x = 200;
  input.mouse_y = 200;
  input.left_down = true;
  const auto result = manager.capture_input(input);

  assert(result.consumed);
  assert(result.dragging);
  assert(manager.trace().events() == sections.at("ui.input.mouse_move"));
}

void test_mouse_up_trace_releases_capture(
    const std::map<std::string, std::vector<std::string>>& sections) {
  ui::LegacyUiManager manager;
  auto* button = manager.add_window<ui::LegacyButton>(RectI{0, 0, 20, 20});
  manager.set_capture(button);
  manager.trace().clear();

  InputState input{};
  input.mouse_x = 5;
  input.mouse_y = 5;
  input.left_released = true;
  const auto result = manager.capture_input(input);
  manager.process_queued_events(input);

  assert(result.consumed);
  assert(manager.tree().captured() == nullptr);
  assert(manager.trace().events() == sections.at("ui.input.mouse_up"));
}

void test_active_menu_blocks_lower_window() {
  ui::LegacyUiManager manager;
  auto* lower = manager.add_window<ui::LegacyButton>(RectI{0, 0, 80, 30});
  auto* menu = manager.add_window<ui::LegacyButton>(RectI{0, 0, 80, 30});
  int lower_clicks = 0;
  int menu_clicks = 0;
  lower->on_click = [&lower_clicks] { ++lower_clicks; };
  menu->on_click = [&menu_clicks] { ++menu_clicks; };

  manager.show_active_menu(*menu);
  click(manager, 10, 10);

  assert(manager.tree().active_menu() == menu);
  assert(lower_clicks == 0);
  assert(menu_clicks == 1);

  manager.close_active_menu(menu);
  assert(manager.tree().active_menu() == nullptr);
}

void test_modal_blocks_lower_window_until_closed() {
  ui::LegacyUiManager manager;
  auto* lower = manager.add_window<ui::LegacyButton>(RectI{0, 0, 80, 30});
  auto* modal = manager.add_window<ui::LegacyWindow>(RectI{120, 120, 100, 80});
  int clicks = 0;
  lower->on_click = [&clicks] { ++clicks; };

  manager.show_modal(*modal);
  manager.trace().clear();
  click(manager, 10, 10);
  assert(clicks == 0);

  manager.close_modal(*modal);
  click(manager, 10, 10);
  assert(clicks == 1);
}

void test_focused_edit_traces_keyboard_without_shortcut_fallback(
    const std::map<std::string, std::vector<std::string>>& sections) {
  ui::LegacyUiManager manager;
  auto* edit = manager.add_window<ui::LegacyEdit>(RectI{0, 0, 120, 20});
  int submits = 0;
  edit->on_submit = [&submits] { ++submits; };
  manager.set_focus(edit);
  manager.trace().clear();

  InputState input{};
  input.text_input = L"a";
  input.key_pressed[VK_F9] = true;
  input.enter_pressed = true;
  const auto result = manager.capture_input(input);
  manager.process_queued_events(input);

  assert(result.text_focus);
  assert(result.consumed);
  assert(edit->value == L"a");
  assert(submits == 1);
  assert(manager.trace().events() == keyboard_prefix(sections));
}

void test_keyboard_shortcut_fallback_completes_golden_trace(
    const std::map<std::string, std::vector<std::string>>& sections) {
  ui::LegacyUiManager manager;
  manager.trace().clear();

  InputState input{};
  input.key_pressed[VK_RETURN] = true;
  input.enter_pressed = true;
  const auto result = manager.capture_input(input);
  manager.trace_legacy_shortcut_fallback();

  assert(!result.text_focus);
  assert(manager.trace().events() == sections.at("ui.input.keyboard"));
}

}  // namespace

int main() {
  const auto source_dir = std::filesystem::path{MIR2_CLIENT_SOURCE_DIR};
  const auto sections =
      read_trace_sections(source_dir / "tests" / "golden" / "legacy_ui_expected_trace.txt");

  test_mouse_down_trace_blocks_scene(sections);
  test_mouse_move_trace_prefers_capture(sections);
  test_mouse_up_trace_releases_capture(sections);
  test_active_menu_blocks_lower_window();
  test_modal_blocks_lower_window_until_closed();
  test_focused_edit_traces_keyboard_without_shortcut_fallback(sections);
  test_keyboard_shortcut_fallback_completes_golden_trace(sections);
  return 0;
}
