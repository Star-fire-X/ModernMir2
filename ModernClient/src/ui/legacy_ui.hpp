#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ui/ui.hpp"

namespace mir2::client::ui {

inline constexpr int kLegacyUiScreenWidth = 800;
inline constexpr int kLegacyUiScreenHeight = 600;

enum class LegacyUiPaintTraceLabel {
  map_tiles,
  map_objects,
  actors_monsters_npcs,
  skill_effects,
  scene_top_effects,
  ui_windows_dwin_direct_paint,
  top_system_messages_draw_screen_top,
  hint_tooltip_draw_hint,
  moving_item_cursor,
  mouse_cursor,
  present
};

[[nodiscard]] std::string_view legacy_ui_paint_layer_label(LegacyUiPaintTraceLabel label);

class LegacyUiTrace {
 public:
  void emit(std::string_view label);
  void clear();

  [[nodiscard]] const std::vector<std::string>& events() const { return events_; }

 private:
  std::vector<std::string> events_{};
};

using LegacyControl = UiNode;
using LegacyWindow = Window;
using LegacyButton = Button;
using LegacyImageButton = SpriteButton;
using LegacyEdit = TextEdit;
using LegacyListBox = ListBox;
using LegacyScrollBar = ScrollBar;
using LegacyTooltip = Tooltip;

class LegacyUiManager {
 public:
  LegacyUiManager();

  [[nodiscard]] UiTree& tree() { return tree_; }
  [[nodiscard]] const UiTree& tree() const { return tree_; }
  [[nodiscard]] UiNode& root();
  [[nodiscard]] const UiNode& root() const;
  [[nodiscard]] LegacyUiTrace& trace() { return trace_; }
  [[nodiscard]] const LegacyUiTrace& trace() const { return trace_; }

  void set_asset_manager(AssetManager* assets);

  template <typename T = LegacyWindow, typename... Args>
  T* add_window(Args&&... args) {
    return root().emplace_child<T>(std::forward<Args>(args)...);
  }

  template <typename T = LegacyControl, typename... Args>
  T* add_control(UiNode& parent, Args&&... args) {
    return parent.emplace_child<T>(std::forward<Args>(args)...);
  }

  void show_window(Window& window);
  void hide_window(UiNode& window);
  void close_window(UiNode& window, const std::function<void()>& legacy_side_effect = {});
  void destroy_window(UiNode& window);

  void show_modal(Window& window);
  void close_modal(UiNode& window);
  void show_active_menu(UiNode& menu);
  void close_active_menu(UiNode* menu = nullptr);

  void set_focus(UiNode* node);
  void release_focus();
  void set_capture(UiNode* node);
  void release_capture(UiNode* node = nullptr);

  UiInputResult capture_input(const InputState& input);
  void process_queued_events(const InputState& input);
  void trace_legacy_shortcut_fallback();
  void direct_paint(SoftwareRenderer& renderer);
  void draw_hint(SoftwareRenderer& renderer);
  void draw_moving_item(SoftwareRenderer& renderer);
  void trace_mouse_cursor();
  void clear_for_scene_exit();
  void clear_for_disconnect();

 private:
  void reset_root();

  UiTree tree_{};
  LegacyUiTrace trace_{};
};

}  // namespace mir2::client::ui
