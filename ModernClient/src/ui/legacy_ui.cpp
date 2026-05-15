#include "ui/legacy_ui.hpp"

#include <algorithm>
#include <cassert>

namespace mir2::client::ui {

void LegacyUiTrace::emit(const std::string_view label) { events_.emplace_back(label); }

void LegacyUiTrace::clear() { events_.clear(); }

LegacyUiManager::LegacyUiManager() {
  tree_.set_trace_callback([this](const std::string_view label) { trace_.emit(label); });
  reset_root();
}

UiNode& LegacyUiManager::root() {
  auto* node = tree_.root();
  assert(node != nullptr);
  return *node;
}

const UiNode& LegacyUiManager::root() const {
  auto* node = tree_.root();
  assert(node != nullptr);
  return *node;
}

void LegacyUiManager::set_asset_manager(AssetManager* assets) { tree_.set_asset_manager(assets); }

void LegacyUiManager::show_window(Window& window) {
  trace_.emit("show_window_sets_visible");
  window.set_visible(tree_, true);
  if (window.floating) {
    tree_.bring_to_front(&window);
    trace_.emit("floating_window_bring_to_front");
  }
}

void LegacyUiManager::hide_window(UiNode& window) {
  trace_.emit("hide_window_keeps_instance");
  window.set_visible(tree_, false);
}

void LegacyUiManager::close_window(UiNode& window,
                                   const std::function<void()>& legacy_side_effect) {
  trace_.emit("close_window_runs_legacy_side_effect");
  if (legacy_side_effect) {
    legacy_side_effect();
  }
  window.set_visible(tree_, false);
}

void LegacyUiManager::destroy_window(UiNode& window) {
  trace_.emit("destroy_window_clears_focus_capture_modal_hover");
  tree_.clear_references_if_descendant(&window);

  auto* parent = window.parent();
  if (parent == nullptr) {
    if (tree_.root() == &window) {
      reset_root();
    }
    return;
  }

  auto& siblings = parent->children();
  const auto it = std::find_if(siblings.begin(), siblings.end(),
                               [&window](const auto& child) { return child.get() == &window; });
  if (it != siblings.end()) {
    siblings.erase(it);
  }
}

void LegacyUiManager::show_modal(Window& window) {
  show_window(window);
  tree_.show_modal(&window);
}

void LegacyUiManager::close_modal(UiNode& window) { tree_.close_modal(&window); }

void LegacyUiManager::show_active_menu(UiNode& menu) { tree_.show_active_menu(&menu); }

void LegacyUiManager::close_active_menu(UiNode* menu) { tree_.close_active_menu(menu); }

void LegacyUiManager::set_focus(UiNode* node) { tree_.focus(node); }

void LegacyUiManager::release_focus() { tree_.focus(nullptr); }

void LegacyUiManager::set_capture(UiNode* node) { tree_.set_capture(node); }

void LegacyUiManager::release_capture(UiNode* node) { tree_.release_capture(node); }

UiInputResult LegacyUiManager::capture_input(const InputState& input) {
  return tree_.capture_input(input);
}

void LegacyUiManager::process_queued_events(const InputState& input) {
  tree_.process_queued_events(input);
}

void LegacyUiManager::trace_legacy_shortcut_fallback() {
  tree_.trace_legacy_shortcut_fallback();
}

void LegacyUiManager::direct_paint(SoftwareRenderer& renderer) {
  trace_.emit("ui_windows_dwin_direct_paint");
  tree_.paint(renderer);
}

void LegacyUiManager::clear_for_scene_exit() {
  trace_.emit("scene_exit_clears_scene_ui");
  reset_root();
}

void LegacyUiManager::clear_for_disconnect() {
  trace_.emit("disconnect_clears_play_ui");
  reset_root();
}

void LegacyUiManager::reset_root() {
  tree_.set_root<UiNode>(RectI{0, 0, kLegacyUiScreenWidth, kLegacyUiScreenHeight});
}

}  // namespace mir2::client::ui
