/**
 * @file legacy_ui_lifecycle.hpp
 * @brief 旧版 UI 生命周期管理 —— 场景切换和断线时的 UI 清理行为追踪
 *
 * @details 定义旧版 UI 系统在关键生命周期事件中的行为标签：
 *          - 场景退出时清理整个 UI 树
 *          - 断线时清理游戏内 HUD
 *          - 窗口销毁时释放焦点/捕获/模态/悬停引用
 *          - 物品丢失或移动时隐藏提示
 *          - 窗口关闭时恢复待处理的物品操作
 *          - 丢弃无法到达的世界消息
 *
 * 这些标签用于测试和调试（trace），确保 UI 生命周期行为
 * 与 Delphi 客户端一致。
 */

#pragma once

#include <string_view>

namespace mir2::client::legacy_ui_lifecycle {

/**
 * @enum LegacyUiLifecycleTraceLabel
 * @brief UI 生命周期操作的追踪标签
 */
enum class LegacyUiLifecycleTraceLabel {
  scene_exit_clears_ui_tree,                    ///< 场景退出时清理 UI 树
  disconnect_clears_play_ui,                    ///< 断线时清理游戏 HUD
  destroy_window_clears_focus_capture_modal_hover, ///< 销毁窗口时清除焦点/捕获/模态/悬停
  hide_tooltip_when_item_missing_or_moving,     ///< 物品缺失或移动时隐藏提示
  restore_pending_item_on_window_close,         ///< 窗口关闭时恢复待处理的物品
  drop_unreachable_world_message,               ///< 丢弃无法到达的世界消息
  show_revive_prompt,                           ///< 显示复活提示
  send_revive_request                           ///< 发送复活请求
};

/// 将生命周期追踪标签转换为可读字符串
[[nodiscard]] std::string_view legacy_ui_lifecycle_trace_label(
    LegacyUiLifecycleTraceLabel label);

}  // namespace mir2::client::legacy_ui_lifecycle
