/**
 * @file legacy_ui_lifecycle.cpp
 * @brief 旧版 UI 生命周期管理实现 —— 场景切换/断线时的 UI 清理行为
 * @details 实现 UI 生命周期追踪标签的字符串转换和清理逻辑。
 *          确保 UI 状态在场景切换时正确重置，与 Delphi 客户端行为一致。
 */

#include "scene/legacy_ui_lifecycle.hpp"

namespace mir2::client::legacy_ui_lifecycle {

std::string_view legacy_ui_lifecycle_trace_label(const LegacyUiLifecycleTraceLabel label) {
  switch (label) {
    case LegacyUiLifecycleTraceLabel::scene_exit_clears_ui_tree:
      return "scene_exit_clears_ui_tree";
    case LegacyUiLifecycleTraceLabel::disconnect_clears_play_ui:
      return "disconnect_clears_play_ui";
    case LegacyUiLifecycleTraceLabel::destroy_window_clears_focus_capture_modal_hover:
      return "destroy_window_clears_focus_capture_modal_hover";
    case LegacyUiLifecycleTraceLabel::hide_tooltip_when_item_missing_or_moving:
      return "hide_tooltip_when_item_missing_or_moving";
    case LegacyUiLifecycleTraceLabel::restore_pending_item_on_window_close:
      return "restore_pending_item_on_window_close";
    case LegacyUiLifecycleTraceLabel::drop_unreachable_world_message:
      return "drop_unreachable_world_message";
    case LegacyUiLifecycleTraceLabel::show_revive_prompt:
      return "show_revive_prompt";
    case LegacyUiLifecycleTraceLabel::send_revive_request:
      return "send_revive_request";
  }
  return "";
}

}  // namespace mir2::client::legacy_ui_lifecycle
