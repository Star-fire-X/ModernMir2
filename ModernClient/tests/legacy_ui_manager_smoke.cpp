#include "ui/legacy_ui.hpp"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

using mir2::client::InputState;
using mir2::client::RectI;
using mir2::client::SpriteFrame;
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

void click_at(ui::UiTree& tree, const int x, const int y) {
  InputState press{};
  press.mouse_x = x;
  press.mouse_y = y;
  press.left_pressed = true;
  press.left_down = true;
  tree.update(press);

  InputState release{};
  release.mouse_x = x;
  release.mouse_y = y;
  release.left_released = true;
  tree.update(release);
}

void test_lifecycle_trace_matches_golden(
    const std::map<std::string, std::vector<std::string>>& sections) {
  ui::LegacyUiManager manager;
  auto* window = manager.add_window<ui::LegacyWindow>(RectI{20, 20, 120, 80});
  window->floating = true;
  window->visible = false;

  int close_count = 0;
  manager.show_window(*window);
  assert(window->visible);
  manager.hide_window(*window);
  assert(!window->visible);
  manager.close_window(*window, [&close_count] { ++close_count; });
  assert(close_count == 1);
  manager.destroy_window(*window);
  assert(manager.root().children().empty());
  manager.clear_for_scene_exit();
  manager.clear_for_disconnect();

  assert(manager.trace().events() == sections.at("ui.window.lifecycle"));
}

void test_z_order_uses_legacy_window_fronting() {
  ui::LegacyUiManager manager;
  auto* first = manager.add_window<ui::LegacyWindow>(RectI{0, 0, 50, 50});
  first->floating = true;
  auto* second = manager.add_window<ui::LegacyWindow>(RectI{10, 0, 50, 50});
  second->floating = true;

  assert(manager.root().hit_test(20, 10) == second);
  manager.show_window(*first);
  assert(manager.root().hit_test(20, 10) == first);
  manager.show_window(*second);
  assert(manager.root().hit_test(20, 10) == second);
}

void test_modal_blocks_lower_window_until_closed() {
  ui::LegacyUiManager manager;
  auto* button = manager.add_window<ui::LegacyButton>(RectI{0, 0, 80, 30});
  auto* modal = manager.add_window<ui::LegacyWindow>(RectI{120, 120, 100, 80});
  int clicks = 0;
  button->on_click = [&clicks] { ++clicks; };

  manager.show_modal(*modal);
  click_at(manager.tree(), 10, 10);
  assert(clicks == 0);

  manager.close_modal(*modal);
  click_at(manager.tree(), 10, 10);
  assert(clicks == 1);
}

void test_destroy_clears_focus_capture_modal_and_children() {
  ui::LegacyUiManager manager;
  auto* window = manager.add_window<ui::LegacyWindow>(RectI{20, 20, 100, 100});
  auto* button = manager.add_control<ui::LegacyButton>(*window, RectI{5, 5, 40, 20});
  int clicks = 0;
  button->on_click = [&clicks] { ++clicks; };

  manager.show_modal(*window);
  manager.show_active_menu(*button);
  manager.set_focus(button);
  manager.set_capture(button);
  InputState press{};
  press.mouse_x = 30;
  press.mouse_y = 30;
  press.left_pressed = true;
  press.left_down = true;
  manager.capture_input(press);
  assert(manager.tree().modal() == window);
  assert(manager.tree().active_menu() == button);
  assert(manager.tree().focused() == button);
  assert(manager.tree().captured() == button);
  assert(manager.tree().hovered() == button);

  manager.destroy_window(*window);
  assert(manager.tree().modal() == nullptr);
  assert(manager.tree().active_menu() == nullptr);
  assert(manager.tree().focused() == nullptr);
  assert(manager.tree().captured() == nullptr);
  assert(manager.tree().hovered() == nullptr);
  assert(manager.root().children().empty());
  manager.process_queued_events(press);
  assert(clicks == 0);
}

void test_legacy_control_aliases_compile_and_join_tree() {
  ui::LegacyUiManager manager;
  auto* window = manager.add_window<ui::LegacyWindow>(RectI{10, 10, 220, 180});
  auto* button = manager.add_control<ui::LegacyButton>(*window, RectI{4, 4, 40, 20});
  auto* image_button = manager.add_control<ui::LegacyImageButton>(
      *window, RectI{4, 28, 20, 20}, std::shared_ptr<const SpriteFrame>{});
  auto* edit = manager.add_control<ui::LegacyEdit>(*window, RectI{4, 52, 90, 22});
  auto* list = manager.add_control<ui::LegacyListBox>(*window, RectI{4, 78, 120, 60});
  auto* scroll = manager.add_control<ui::LegacyScrollBar>(*window, RectI{130, 78, 12, 60});
  auto* tooltip = manager.add_control<ui::LegacyTooltip>(*window, RectI{4, 144, 80, 20});

  assert(button != nullptr);
  assert(image_button != nullptr);
  assert(edit != nullptr);
  assert(list != nullptr);
  assert(scroll != nullptr);
  assert(tooltip != nullptr);
  assert(edit->accepts_text_input());
  assert(window->children().size() == 6U);

  manager.show_active_menu(*button);
  assert(manager.tree().active_menu() == button);
  manager.close_active_menu(button);
  assert(manager.tree().active_menu() == nullptr);

  InputState input{};
  input.key_pressed[VK_RETURN] = true;
  input.enter_pressed = true;
  manager.trace().clear();
  manager.capture_input(input);
  manager.trace_legacy_shortcut_fallback();
  assert(manager.trace().events().back() == "legacy_shortcut_fallback");
}

void test_direct_paint_trace_is_window_layer_only(
    const std::map<std::string, std::vector<std::string>>& sections) {
  const auto& paint_layers = sections.at("ui.paint.layers");
  assert(std::find(paint_layers.begin(), paint_layers.end(), "ui_windows_dwin_direct_paint") !=
         paint_layers.end());

  ui::LegacyUiManager manager;
  mir2::client::SoftwareRenderer renderer;
  manager.direct_paint(renderer);

  const auto& events = manager.trace().events();
  assert(events.size() == 1U);
  assert(events.front() == "ui_windows_dwin_direct_paint");
  for (const auto& event : events) {
    assert(event.find("draw_hint") == std::string::npos);
    assert(event.find("moving_item") == std::string::npos);
    assert(event.find("mouse") == std::string::npos);
  }

  manager.trace().clear();
  manager.draw_hint(renderer);
  manager.draw_moving_item(renderer);
  manager.trace_mouse_cursor();
  assert((manager.trace().events() ==
          std::vector<std::string>{"hint_tooltip_draw_hint", "moving_item_cursor",
                                   "mouse_cursor"}));
}

}  // namespace

int main() {
  const auto source_dir = std::filesystem::path{MIR2_CLIENT_SOURCE_DIR};
  const auto sections =
      read_trace_sections(source_dir / "tests" / "golden" / "legacy_ui_expected_trace.txt");

  test_lifecycle_trace_matches_golden(sections);
  test_z_order_uses_legacy_window_fronting();
  test_modal_blocks_lower_window_until_closed();
  test_destroy_clears_focus_capture_modal_and_children();
  test_legacy_control_aliases_compile_and_join_tree();
  test_direct_paint_trace_is_window_layer_only(sections);
  return 0;
}
