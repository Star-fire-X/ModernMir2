#include "game/game_state.hpp"
#include "ui/legacy_ui.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <random>
#include <string>
#include <utility>

namespace {

using mir2::client::InputState;
using mir2::client::RectI;
namespace ui = mir2::client::ui;

mir2::client_v1::ItemState item(std::string name, const std::uint32_t make_index) {
  mir2::client_v1::ItemState result;
  result.name = std::move(name);
  result.make_index = make_index;
  result.looks = 1;
  return result;
}

struct UiFixture {
  std::array<ui::LegacyWindow*, 3> windows{};
  std::array<ui::LegacyButton*, 3> buttons{};
  ui::LegacyEdit* edit{nullptr};
};

UiFixture rebuild_fixture(ui::LegacyUiManager& manager, std::array<int, 3>& clicks) {
  UiFixture fixture;
  for (int index = 0; index < 3; ++index) {
    auto* window = manager.add_window<ui::LegacyWindow>(
        RectI{20 + index * 90, 30 + index * 50, 72, 54});
    window->floating = true;
    auto* button = manager.add_control<ui::LegacyButton>(*window, RectI{5, 5, 42, 22});
    button->focusable = true;
    button->on_click = [&clicks, index] { ++clicks[static_cast<std::size_t>(index)]; };
    fixture.windows[static_cast<std::size_t>(index)] = window;
    fixture.buttons[static_cast<std::size_t>(index)] = button;
  }
  fixture.edit = manager.add_control<ui::LegacyEdit>(*fixture.windows[0], RectI{5, 31, 50, 16});
  return fixture;
}

InputState mouse_input(const RectI& rect, const bool pressed) {
  InputState input;
  input.mouse_x = rect.x + 2;
  input.mouse_y = rect.y + 2;
  if (pressed) {
    input.left_pressed = true;
    input.left_down = true;
  } else {
    input.left_released = true;
  }
  return input;
}

void click_button(ui::LegacyUiManager& manager, ui::LegacyButton* button) {
  const auto rect = button->resolved_bounds();
  auto press = mouse_input(rect, true);
  manager.capture_input(press);
  manager.process_queued_events(press);
  auto release = mouse_input(rect, false);
  manager.capture_input(release);
  manager.process_queued_events(release);
}

void assert_blocking_references_clear(const ui::LegacyUiManager& manager) {
  assert(manager.tree().focused() == nullptr);
  assert(manager.tree().captured() == nullptr);
  assert(manager.tree().modal() == nullptr);
  assert(manager.tree().active_menu() == nullptr);
}

void assert_tree_references_clear(const ui::LegacyUiManager& manager) {
  assert_blocking_references_clear(manager);
  assert(manager.tree().hovered() == nullptr);
}

}  // namespace

int main() {
  ui::LegacyUiManager manager;
  mir2::client::GameStateStore state;
  std::array<int, 3> clicks{};
  auto fixture = rebuild_fixture(manager, clicks);
  std::mt19937 rng{0x5549465AU};

  for (int step = 0; step < 256; ++step) {
    const auto index = static_cast<std::size_t>(rng() % fixture.windows.size());
    switch (rng() % 9U) {
      case 0:
        manager.tree().close_modal(nullptr);
        manager.close_active_menu();
        click_button(manager, fixture.buttons[index]);
        break;
      case 1: {
        const auto before = clicks[index];
        auto press = mouse_input(fixture.buttons[index]->resolved_bounds(), true);
        manager.capture_input(press);
        manager.destroy_window(*fixture.windows[index]);
        assert_blocking_references_clear(manager);
        manager.process_queued_events(press);
        assert(clicks[index] == before);
        manager.clear_for_scene_exit();
        assert_tree_references_clear(manager);
        fixture = rebuild_fixture(manager, clicks);
        break;
      }
      case 2:
        manager.show_modal(*fixture.windows[index]);
        manager.show_active_menu(*fixture.buttons[index]);
        manager.set_focus(index == 0U ? static_cast<ui::UiNode*>(fixture.edit)
                                      : static_cast<ui::UiNode*>(fixture.buttons[index]));
        manager.set_capture(fixture.buttons[index]);
        manager.capture_input(mouse_input(fixture.buttons[index]->resolved_bounds(), true));
        manager.destroy_window(*fixture.windows[index]);
        assert_blocking_references_clear(manager);
        manager.clear_for_scene_exit();
        assert_tree_references_clear(manager);
        fixture = rebuild_fixture(manager, clicks);
        break;
      case 3:
        manager.show_modal(*fixture.windows[index]);
        manager.show_active_menu(*fixture.buttons[index]);
        manager.set_capture(fixture.buttons[index]);
        manager.hide_window(*fixture.windows[index]);
        assert_blocking_references_clear(manager);
        manager.show_window(*fixture.windows[index]);
        break;
      case 4:
        manager.set_focus(fixture.edit);
        manager.set_capture(fixture.buttons[index]);
        manager.clear_for_scene_exit();
        assert_tree_references_clear(manager);
        fixture = rebuild_fixture(manager, clicks);
        break;
      case 5:
        manager.set_focus(fixture.edit);
        manager.set_capture(fixture.buttons[index]);
        state.world.moving_item =
            mir2::client::MovingItemState{true, mir2::client::MovingItemSource::bag, 6,
                                          item("Moving", 2001)};
        state.world.pending_item_action.active = true;
        manager.clear_for_disconnect();
        state.clear_play_scene_state();
        assert_tree_references_clear(manager);
        assert(!state.world.moving_item.active);
        assert(!state.world.pending_item_action.active);
        fixture = rebuild_fixture(manager, clicks);
        break;
      case 6: {
        auto pending = item("TradePotion", 3001);
        state.world.bag_items[6] = mir2::client_v1::ItemState{};
        state.begin_pending_item_action(mir2::client::PendingItemActionKind::trade_add,
                                        mir2::client::MovingItemSource::bag, 6, 0, pending,
                                        1234);
        state.apply(mir2::client_v1::TradeState{false, "", {}, {}, 0, 0, false, false});
        assert(!state.world.pending_item_action.active);
        assert(state.world.bag_items[6].name == "TradePotion");
        break;
      }
      case 7:
        state.world.moving_item =
            mir2::client::MovingItemState{true, mir2::client::MovingItemSource::equipment, 1,
                                          item("Equip", 4001)};
        state.world.pending_item_action.active = true;
        state.world.pending_pickup_item_id = 99;
        state.clear_world_ui_state();
        assert(!state.world.moving_item.active);
        assert(!state.world.pending_item_action.active);
        assert(state.world.pending_pickup_item_id == 0);
        break;
      default: {
        manager.tree().close_modal(nullptr);
        const auto before = clicks[0];
        manager.show_modal(*fixture.windows[1]);
        click_button(manager, fixture.buttons[0]);
        assert(clicks[0] == before);
        manager.close_modal(*fixture.windows[1]);
        click_button(manager, fixture.buttons[0]);
        assert(clicks[0] == before + 1);
        break;
      }
    }
  }

  return 0;
}
