#pragma once

#include <algorithm>
#include <functional>

namespace mir2::client {

class LegacyFrameScheduler {
 public:
  struct Hooks {
    std::function<void()> legacy_input_dispatch;
    std::function<void()> timer1_network_drain;
    std::function<void()> capture_ui_input;
    std::function<void()> process_key_messages;
    std::function<void()> process_action_messages;
    std::function<void()> dwin_process;
    std::function<void()> scene_run;
    std::function<void()> draw_screen;
    std::function<void()> dwin_direct_paint;
    std::function<void()> draw_screen_top;
    std::function<void()> draw_hint;
    std::function<void()> draw_moving_item;
    std::function<void()> flip;
    std::function<bool()> can_draw;
  };

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

  void force_render_due_for_test() { render_elapsed_ms_ = kFrameIntervalMs; }

 private:
  static constexpr float kFrameIntervalMs = 30.0F;

  static void call(const std::function<void()>& hook) {
    if (hook) {
      hook();
    }
  }

  float render_elapsed_ms_{0.0F};
};

}  // namespace mir2::client
