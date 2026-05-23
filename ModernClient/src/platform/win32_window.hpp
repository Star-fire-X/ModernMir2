// ============================================================
// Mir2 现代客户端 — Win32 窗口与输入管理
// 职责：创建和管理游戏窗口，采集鼠标/键盘输入，
//       提供每帧增量输入状态（按下/释放/持续）
//
// 与经典传奇客户端的差异：
// 原版传奇客户端使用 Delphi TForm 作为主窗口，自动处理消息
// 泵送。本实现直接使用 Win32 API 创建无边框窗口（WS_POPUP），
// 手动管理消息循环，以获得更精细的输入控制。
//
// 输入系统设计：
// 每帧的输入状态分为两种类型：
//   1. 瞬态标志（pressed/released/backspace/enter/text_input）
//      每帧开始时由 begin_frame() 清零
//   2. 持续状态（mouse_x/mouse_y/left_down/right_down/key_down）
//      跨帧保持，直到对应的释放消息到达
// 这种设计与原传奇客户端的键盘状态查询兼容。
// ============================================================
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <windows.h>

namespace mir2::client {

enum class LegacyInputEventKind {
  mouse_move,
  left_down,
  left_up,
  right_down,
  right_up,
  mouse_wheel,
  left_double_click,
  right_double_click,
  key_down,
  key_up,
  char_input
};

struct LegacyInputEvent {
  LegacyInputEventKind kind{LegacyInputEventKind::mouse_move};
  std::uint32_t sequence{0};
  int mouse_x{0};
  int mouse_y{0};
  std::uint16_t key{0};
  wchar_t character{0};
  int wheel_delta{0};
  bool shift{false};
  bool ctrl{false};
  bool alt{false};
  bool repeat{false};
  bool left_down{false};
  bool right_down{false};
};

/// 每帧输入状态：包含鼠标位置/按键、键盘状态、文本输入
/// begin_frame() 清除瞬态标志（按下/释放/文本），帧间持续状态保留
/// 确保鼠标按下/释放等事件不会跨帧重复触发
struct InputState {
  int mouse_x{0};
  int mouse_y{0};
  bool left_down{false};       ///< 左键持续按下（跨帧保持，WM_LBUTTONUP 时清除）
  bool left_pressed{false};    ///< 左键刚按下（瞬态标志，每帧清零）
  bool left_released{false};   ///< 左键刚释放（瞬态标志，每帧清零）
  bool right_down{false};      ///< 右键持续按下
  bool right_pressed{false};   ///< 右键刚按下（瞬态）
  bool right_released{false};  ///< 右键刚释放（瞬态）
  int wheel_delta{0};          ///< 鼠标滚轮增量（瞬态，单位为 WHEEL_DELTA 的倍数）
  bool wheel_scrolled{false};  ///< 本帧是否发生滚轮事件（瞬态）
  bool left_double_click{false};   ///< 左键双击边沿（瞬态）
  bool right_double_click{false};  ///< 右键双击边沿（瞬态）
  std::array<bool, 256> key_down{};     ///< 键盘键持续按下（数组索引为虚拟键码）
  std::array<bool, 256> key_pressed{};  ///< 键盘键刚按下（瞬态，仅首次触发）
  std::wstring text_input{};            ///< 本帧输入的文本（WM_CHAR 消息累积）
  bool backspace_pressed{false};        ///< 退格键按下（瞬态）
  bool enter_pressed{false};            ///< 回车键按下（瞬态）
  std::vector<LegacyInputEvent> events{}; ///< 本帧 Win32/VCL 顺序输入事件

  /// 清零所有瞬态标志，每帧开始时调用
  /// 必须在窗口消息泵送之前调用，确保新帧从空白瞬态开始
  void begin_frame() {
    left_pressed = false;
    left_released = false;
    right_pressed = false;
    right_released = false;
    wheel_delta = 0;
    wheel_scrolled = false;
    left_double_click = false;
    right_double_click = false;
    key_pressed.fill(false);
    text_input.clear();
    backspace_pressed = false;
    enter_pressed = false;
    events.clear();
  }
};

/// Win32 窗口：封装窗口创建、消息泵、输入采集
/// 使用无边框窗口样式（WS_POPUP），支持 DPI 感知
/// 默认逻辑分辨率为 800x600（与经典传奇客户端一致）
class Win32Window {
 public:
  Win32Window() = default;
  ~Win32Window();

  /// 创建窗口并注册窗口类
  /// @param title 窗口标题
  /// @param width 窗口宽度（像素）
  /// @param height 窗口高度（像素）
  bool create(const std::wstring& title, int width, int height);
  /// 泵送消息队列：处理所有待处理的 Win32 消息
  /// @return false 表示收到 WM_QUIT，主循环应退出
  bool pump_messages();
  /// 每帧开始：清零瞬态输入标志
  void begin_frame();
  /// 发送 WM_QUIT 关闭窗口
  void close_now();

  [[nodiscard]] HWND handle() const;
  [[nodiscard]] const InputState& input() const;
  [[nodiscard]] int client_width() const;
  [[nodiscard]] int client_height() const;
  [[nodiscard]] bool was_resized() const;
  void clear_resize_flag();
  [[nodiscard]] bool consume_close_request();

 private:
  /// 全局窗口过程（将消息转发到实例方法）
  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  /// 实例窗口过程：处理消息并更新 InputState
  LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam);
  /// 更新缓存的客户区尺寸
  void update_client_size();

  HWND hwnd_{nullptr};
  HINSTANCE instance_{GetModuleHandleW(nullptr)};
  InputState input_{};
  int client_width_{0};
  int client_height_{0};
  bool resized_{false};          ///< 窗口尺寸是否在本帧发生变化（用于渲染器重建）
  bool close_requested_{false};  ///< 是否收到 WM_CLOSE（延迟关闭，由主循环决定时机）
  HBRUSH edit_background_brush_{nullptr};  ///< 黑色背景画刷（原生编辑框 WM_CTLCOLOREDIT 使用）
};

}  // namespace mir2::client
