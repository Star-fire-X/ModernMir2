#pragma once

#include <string_view>

namespace mir2::client::legacy_ui_lifecycle {

enum class LegacyUiLifecycleTraceLabel {
  scene_exit_clears_ui_tree,
  disconnect_clears_play_ui,
  destroy_window_clears_focus_capture_modal_hover,
  hide_tooltip_when_item_missing_or_moving,
  restore_pending_item_on_window_close,
  drop_unreachable_world_message,
  show_revive_prompt,
  send_revive_request
};

[[nodiscard]] std::string_view legacy_ui_lifecycle_trace_label(
    LegacyUiLifecycleTraceLabel label);

}  // namespace mir2::client::legacy_ui_lifecycle
