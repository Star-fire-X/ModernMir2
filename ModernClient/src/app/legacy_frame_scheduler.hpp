/**
 * @file legacy_frame_scheduler.hpp
 * @brief 旧版帧调度器 —— 模拟 Delphi 客户端的帧内回调执行顺序
 *
 * @details 经典传奇 Delphi 客户端使用 TTimer 组件驱动帧逻辑，
 *          每帧按固定顺序执行一系列回调：输入分派 → 网络接收 →
 *          键盘处理 → 动作处理 → 场景更新 → 渲染。
 *
 *          本调度器在 C++ 中精确复现这一执行顺序，确保逻辑时序
 *          与原版客户端一致。同时通过固定的渲染间隔（30ms）
 *          控制渲染帧率，避免无意义的空帧渲染。
 *
 * 帧内回调执行顺序（与 Delphi 客户端完全对应）：
 * 1. legacy_input_dispatch    — 旧版输入事件分派
 * 2. timer1_network_drain     — 网络数据接收处理
 * 3. [can_draw 检查]          — 如果不可绘制，仅执行键盘/动作/场景更新
 * 4. capture_ui_input         — 捕获 UI 输入状态
 * 5. process_key_messages     — 处理键盘消息
 * 6. process_action_messages  — 处理动作消息
 * 7. dwin_process             — Delphi DWIN 处理
 * 8. 如果到达渲染间隔（30ms）：
 *    a. draw_screen           — 绘制屏幕
 *    b. dwin_direct_paint     — 直接绘制层
 *    c. draw_screen_top       — 绘制屏幕顶层（系统消息）
 *    d. draw_hint             — 绘制提示
 *    e. draw_moving_item      — 绘制拖拽物品
 *    f. flip                  — 呈现到屏幕
 * 9. scene_run                — 场景逻辑更新
 *
 * @note 渲染间隔固定为 30ms（约 33 FPS），与经典传奇客户端一致
 */

#pragma once

#include <algorithm>
#include <functional>

namespace mir2::client {

/**
 * @class LegacyFrameScheduler
 * @brief 旧版帧调度器 —— 按 Delphi 客户端执行顺序驱动每帧回调
 *
 * @details 该调度器将一系列 std::function 回调按固定顺序调用，
 *          精确模拟 Delphi 客户端中 TTimer 事件的触发顺序。
 *          同时内置 30ms 渲染间隔机制，只在需要时触发渲染钩子。
 */
class LegacyFrameScheduler {
 public:
  /// 帧回调钩子集合 —— 每个钩子对应 Delphi 客户端的一个阶段
  struct Hooks {
    std::function<void()> legacy_input_dispatch;    ///< 旧版输入事件分派
    std::function<void()> timer1_network_drain;     ///< 定时器网络数据接收
    std::function<void()> capture_ui_input;         ///< 捕获 UI 输入状态
    std::function<void()> process_key_messages;     ///< 处理键盘消息
    std::function<void()> process_action_messages;  ///< 处理动作消息（移动/攻击）
    std::function<void()> dwin_process;             ///< Delphi DWIN 处理
    std::function<void()> scene_run;                ///< 场景逻辑更新
    std::function<void()> draw_screen;              ///< 绘制屏幕内容
    std::function<void()> dwin_direct_paint;        ///< 直接绘制层（UI 窗口）
    std::function<void()> draw_screen_top;          ///< 绘制顶层（系统消息等）
    std::function<void()> draw_hint;                ///< 绘制提示（Tooltip）
    std::function<void()> draw_moving_item;         ///< 绘制拖拽物品光标
    std::function<void()> flip;                     ///< 画面呈现（Present）
    std::function<bool()> can_draw;                 ///< 检查是否允许绘制（false 时跳过渲染阶段）
  };

  /**
   * @brief 执行一帧的逻辑
   *
   * @details 按固定顺序调用已注册的钩子函数。渲染阶段仅在以下条件同时满足时执行：
   *          1. can_draw 钩子返回 true（或未注册，默认为 true）
   *          2. 距离上次渲染已超过 30ms
   *
   *          如果未到达渲染间隔，跳过渲染阶段，直接执行 scene_run。
   *
   * @param delta_seconds 上一帧经过的时间（秒），用于累积渲染间隔计时
   * @param hooks 帧回调钩子集合（可为空的钩子会被跳过）
   */
  void run_frame(float delta_seconds, const Hooks& hooks) {
    call(hooks.legacy_input_dispatch);
    call(hooks.timer1_network_drain);
    const auto can_draw = hooks.can_draw == nullptr || hooks.can_draw();
    if (!can_draw) {
      call(hooks.process_key_messages);
      call(hooks.process_action_messages);
      call(hooks.scene_run);
    }

    call(hooks.capture_ui_input);
    call(hooks.process_key_messages);
    call(hooks.process_action_messages);
    call(hooks.dwin_process);

    render_elapsed_ms_ += std::max(0.0F, delta_seconds) * 1000.0F;
    const auto render_due = render_elapsed_ms_ >= kFrameIntervalMs;
    if (render_due && can_draw) {
      render_elapsed_ms_ = 0.0F;
      call(hooks.draw_screen);
      call(hooks.dwin_direct_paint);
      call(hooks.draw_screen_top);
      call(hooks.draw_hint);
      call(hooks.draw_moving_item);
      call(hooks.flip);
      return;
    }

    call(hooks.scene_run);
  }

  /// 强制下一次 run_frame 触发渲染（仅用于测试）
  void force_render_due_for_test() { render_elapsed_ms_ = kFrameIntervalMs; }

 private:
  static constexpr float kFrameIntervalMs = 30.0F;  ///< 渲染帧间隔（约 33 FPS）

  /// 安全调用钩子函数（空函数指针则跳过）
  static void call(const std::function<void()>& hook) {
    if (hook) {
      hook();
    }
  }

  float render_elapsed_ms_{0.0F};  ///< 自上次渲染以来的累计时间（毫秒）
};

}  // namespace mir2::client
