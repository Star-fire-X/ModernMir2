#include "ui/ui.hpp"

#include <cassert>
#include <memory>

namespace {

using mir2::client::InputState;
using mir2::client::RectI;
using mir2::client::SpriteFrame;
namespace ui = mir2::client::ui;

class CaptureProbe final : public ui::UiNode {
 public:
  explicit CaptureProbe(const RectI bounds) : ui::UiNode(bounds) { focusable = true; }

  bool on_mouse_down(ui::UiTree& tree, const InputState& /*input*/,
                     const ui::UiMouseButton button) override {
    if (button != ui::UiMouseButton::left) {
      return false;
    }
    ++downs;
    tree.set_capture(this);
    return true;
  }

  bool on_mouse_move(ui::UiTree& tree, const InputState& /*input*/) override {
    if (tree.captured() != this) {
      return false;
    }
    ++moves;
    return true;
  }

  bool on_mouse_up(ui::UiTree& tree, const InputState& /*input*/,
                   const ui::UiMouseButton button) override {
    if (button != ui::UiMouseButton::left) {
      return false;
    }
    ++ups;
    tree.release_capture(this);
    return true;
  }

  int downs{0};
  int moves{0};
  int ups{0};
};

std::shared_ptr<SpriteFrame> make_two_pixel_frame() {
  auto frame = std::make_shared<SpriteFrame>();
  frame->width = 2;
  frame->height = 1;
  frame->pixels = {0x00000000U, 0xFFFFFFFFU};
  return frame;
}

void test_reverse_hit_and_bring_to_front() {
  ui::UiTree tree;
  auto* root = tree.set_root<ui::UiNode>(RectI{0, 0, 100, 100});
  auto* first = root->emplace_child<ui::UiNode>(RectI{0, 0, 50, 50});
  auto* second = root->emplace_child<ui::UiNode>(RectI{0, 0, 50, 50});

  assert(root->hit_test(10, 10) == second);
  tree.bring_to_front(first);
  assert(root->hit_test(10, 10) == first);
}

void test_sprite_pixel_hit() {
  ui::SpriteButton button(RectI{0, 0, 2, 1}, make_two_pixel_frame());

  assert(button.hit_test(0, 0) == nullptr);
  assert(button.hit_test(1, 0) == &button);
}

void test_capture_keeps_mouse_target() {
  ui::UiTree tree;
  auto* root = tree.set_root<ui::UiNode>(RectI{0, 0, 100, 100});
  auto* probe = root->emplace_child<CaptureProbe>(RectI{0, 0, 10, 10});

  InputState input{};
  input.mouse_x = 5;
  input.mouse_y = 5;
  input.left_pressed = true;
  input.left_down = true;
  auto result = tree.update(input);
  assert(result.consumed);
  assert(tree.captured() == probe);
  assert(probe->downs == 1);

  InputState drag{};
  drag.mouse_x = 80;
  drag.mouse_y = 80;
  drag.left_down = true;
  result = tree.update(drag);
  assert(result.dragging);
  assert(probe->moves == 1);

  InputState release{};
  release.mouse_x = 80;
  release.mouse_y = 80;
  release.left_released = true;
  result = tree.update(release);
  assert(result.consumed);
  assert(tree.captured() == nullptr);
  assert(probe->ups == 1);
}

void test_button_release_outside_does_not_click() {
  ui::UiTree tree;
  auto* root = tree.set_root<ui::UiNode>(RectI{0, 0, 100, 100});
  auto* button = root->emplace_child<ui::Button>(RectI{0, 0, 10, 10});
  int clicks = 0;
  button->on_click = [&clicks] { ++clicks; };

  InputState press{};
  press.mouse_x = 5;
  press.mouse_y = 5;
  press.left_pressed = true;
  press.left_down = true;
  tree.update(press);

  InputState drag{};
  drag.mouse_x = 50;
  drag.mouse_y = 50;
  drag.left_down = true;
  tree.update(drag);

  InputState release{};
  release.mouse_x = 50;
  release.mouse_y = 50;
  release.left_released = true;
  tree.update(release);
  assert(clicks == 0);

  tree.update(press);
  InputState release_inside{};
  release_inside.mouse_x = 5;
  release_inside.mouse_y = 5;
  release_inside.left_released = true;
  tree.update(release_inside);
  assert(clicks == 1);
}

void test_background_root_does_not_consume_clicks() {
  ui::UiTree tree;
  tree.set_root<ui::UiNode>(RectI{0, 0, 100, 100});

  InputState press{};
  press.mouse_x = 50;
  press.mouse_y = 50;
  press.left_pressed = true;
  press.left_down = true;
  const auto result = tree.update(press);
  assert(!result.consumed);
}

void test_modal_root_consumes_clicks() {
  ui::UiTree tree;
  auto* root = tree.set_root<ui::UiNode>(RectI{0, 0, 100, 100});
  root->background = false;

  InputState press{};
  press.mouse_x = 50;
  press.mouse_y = 50;
  press.left_pressed = true;
  press.left_down = true;
  const auto result = tree.update(press);
  assert(result.consumed);
}

void click_at(ui::UiTree& tree, int x, int y) {
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

void test_three_button_confirm_yes_only() {
  ui::UiTree tree;
  auto* root = tree.set_root<ui::UiNode>(RectI{0, 0, 200, 80});
  root->background = false;
  auto* yes = root->emplace_child<ui::Button>(RectI{10, 10, 40, 20});
  auto* no = root->emplace_child<ui::Button>(RectI{60, 10, 40, 20});
  auto* cancel = root->emplace_child<ui::Button>(RectI{110, 10, 60, 20});

  int confirms = 0;
  int closes = 0;
  auto close = [&closes] { ++closes; };
  yes->on_click = [&] {
    ++confirms;
    close();
  };
  no->on_click = close;
  cancel->on_click = close;

  InputState enter{};
  enter.enter_pressed = true;
  tree.update(enter);
  assert(confirms == 0);
  assert(closes == 0);

  click_at(tree, 70, 20);
  assert(confirms == 0);
  assert(closes == 1);

  click_at(tree, 120, 20);
  assert(confirms == 0);
  assert(closes == 2);

  click_at(tree, 20, 20);
  assert(confirms == 1);
  assert(closes == 3);
}

void test_window_drag_clamps_and_brings_to_front() {
  ui::UiTree tree;
  auto* root = tree.set_root<ui::UiNode>(RectI{0, 0, 800, 600});
  auto* first = root->emplace_child<ui::Window>(RectI{0, 0, 50, 50});
  first->floating = true;
  auto* second = root->emplace_child<ui::Window>(RectI{30, 0, 50, 50});

  assert(root->hit_test(35, 5) == second);

  InputState press{};
  press.mouse_x = 10;
  press.mouse_y = 10;
  press.left_pressed = true;
  press.left_down = true;
  tree.update(press);
  assert(root->hit_test(35, 5) == first);
  assert(tree.captured() == first);

  InputState drag{};
  drag.mouse_x = 1000;
  drag.mouse_y = 1000;
  drag.left_down = true;
  tree.update(drag);
  assert(first->bounds.x == 520);
  assert(first->bounds.y == 520);

  InputState release{};
  release.mouse_x = 1000;
  release.mouse_y = 1000;
  release.left_released = true;
  tree.update(release);
  assert(tree.captured() == nullptr);
}

void test_non_floating_window_click_does_not_bring_to_front() {
  ui::UiTree tree;
  auto* root = tree.set_root<ui::UiNode>(RectI{0, 0, 800, 600});
  auto* first = root->emplace_child<ui::Window>(RectI{0, 0, 50, 50});
  auto* second = root->emplace_child<ui::Window>(RectI{30, 0, 50, 50});

  assert(root->hit_test(35, 5) == second);

  InputState press{};
  press.mouse_x = 10;
  press.mouse_y = 10;
  press.left_pressed = true;
  press.left_down = true;
  const auto result = tree.update(press);
  assert(result.consumed);
  assert(root->hit_test(35, 5) == second);
  assert(tree.focused() == first);
  assert(tree.captured() == nullptr);
}

void test_grid_cell_select_and_double_click() {
  ui::UiTree tree;
  auto* root = tree.set_root<ui::UiNode>(RectI{0, 0, 100, 100});
  auto* grid = root->emplace_child<ui::Grid>(RectI{10, 10, 40, 40});
  grid->col_count = 2;
  grid->row_count = 2;
  grid->col_width = 20;
  grid->row_height = 20;

  const auto cell = grid->cell_at(35, 15);
  assert(cell.has_value());
  assert(cell->first == 1);
  assert(cell->second == 0);

  int selects = 0;
  int doubles = 0;
  grid->on_cell_select = [&selects](ui::Grid&, int, int) { ++selects; };
  grid->on_cell_double_click = [&doubles](ui::Grid&, int, int) { ++doubles; };

  InputState press{};
  press.mouse_x = 15;
  press.mouse_y = 15;
  press.left_pressed = true;
  press.left_down = true;
  tree.update(press);

  InputState release_different{};
  release_different.mouse_x = 35;
  release_different.mouse_y = 15;
  release_different.left_released = true;
  tree.update(release_different);
  assert(selects == 0);

  tree.update(press);
  InputState release_same{};
  release_same.mouse_x = 15;
  release_same.mouse_y = 15;
  release_same.left_released = true;
  tree.update(release_same);
  assert(selects == 1);

  tree.update(press);
  tree.update(release_same);
  assert(selects == 2);
  assert(doubles == 1);
}

void test_tooltip_clamps_to_screen() {
  ui::Tooltip tooltip(RectI{0, 0, 0, 0});
  tooltip.show_at(790, 590, L"legacy tooltip", 0xFFFFFFFFU);
  const auto rect = tooltip.resolved_bounds();
  assert(rect.x + rect.w <= 800);
  assert(rect.y + rect.h <= 600);
  assert(rect.y < 590);
  assert(tooltip.visible);
  tooltip.hide();
  assert(!tooltip.visible);
}

void test_tooltip_multiline_sizes_height() {
  ui::Tooltip tooltip(RectI{0, 0, 0, 0});
  tooltip.show_at(10, 10, L"Name\nDura 1/2\nStdMode 0", 0xFFFFFFFFU);
  const auto rect = tooltip.resolved_bounds();
  assert(rect.h >= 48);
  assert(rect.w >= 71);
}

void test_overlay_layer_order_keeps_modal_on_top() {
  ui::UiTree tree;
  auto* root = tree.set_root<ui::UiNode>(RectI{0, 0, 100, 100});
  auto* window = root->emplace_child<ui::Window>(RectI{0, 0, 100, 100});
  auto* drag = root->emplace_child<ui::DragSpriteOverlay>(RectI{0, 0, 16, 16});
  auto* tooltip = root->emplace_child<ui::Tooltip>(RectI{0, 0, 40, 20});
  auto* modal = root->emplace_child<ui::Window>(RectI{0, 0, 80, 80});
  tooltip->visible = true;

  tree.bring_to_front(window);
  tree.bring_to_front(tooltip);
  tree.bring_to_front(drag);
  tree.bring_to_front(modal);

  const auto& children = root->children();
  assert(children[children.size() - 4].get() == window);
  assert(children[children.size() - 3].get() == tooltip);
  assert(children[children.size() - 2].get() == drag);
  assert(children.back().get() == modal);
}

}  // namespace

int main() {
  test_reverse_hit_and_bring_to_front();
  test_sprite_pixel_hit();
  test_capture_keeps_mouse_target();
  test_button_release_outside_does_not_click();
  test_background_root_does_not_consume_clicks();
  test_modal_root_consumes_clicks();
  test_three_button_confirm_yes_only();
  test_window_drag_clamps_and_brings_to_front();
  test_non_floating_window_click_does_not_bring_to_front();
  test_grid_cell_select_and_double_click();
  test_tooltip_clamps_to_screen();
  test_tooltip_multiline_sizes_height();
  test_overlay_layer_order_keeps_modal_on_top();
  return 0;
}
