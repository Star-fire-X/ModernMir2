// ============================================================
// Mir2 现代客户端 — Win32 窗口实现
// 职责：窗口创建、消息循环、键盘/鼠标输入采集
//
// 窗口行为设计：
// 1. 使用无边框窗口（WS_POPUP），全屏/窗口模式通过窗口尺寸控制
// 2. DPI 感知启用后，客户区坐标与逻辑像素一致
// 3. 原生文本编辑框（EDIT）控件用于登录/注册输入框，
//    通过 WM_CTLCOLOREDIT 设置黑底白字样式
// 4. WM_CLOSE 不立即销毁窗口，而是设置标志后由主循环
//    决定关闭时机（可能弹出确认对话框）
// ============================================================

#include "platform/win32_window.hpp"

#include <windowsx.h>

namespace mir2::client {

namespace {

/// 窗口类名（每个 Windows 窗口类需要唯一的名字）
constexpr wchar_t kWindowClassName[] = L"ModernMir2ClientWindow";
/// 无边框窗口样式，对应传奇客户端的无装饰窗口
constexpr DWORD kFixedWindowStyle = WS_POPUP;

}  // namespace

Win32Window::~Win32Window() {
  if (hwnd_ != nullptr) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
  if (edit_background_brush_ != nullptr) {
    DeleteObject(edit_background_brush_);
    edit_background_brush_ = nullptr;
  }
}

// 创建游戏窗口：注册窗口类，设置 DPI 感知，显示窗口
// 步骤：1) 启用 DPI 感知  2) 注册 WNDCLASS  3) 创建窗口  4) 显示窗口
bool Win32Window::create(const std::wstring& title, int width, int height) {
  // SetProcessDPIAware 让 Windows 不会对客户区坐标进行虚拟化缩放
  // 确保 GetClientRect 返回真实的像素值
  SetProcessDPIAware();

  // 注册窗口类：指定窗口过程、光标、背景画刷
  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.hInstance = instance_;
  window_class.lpfnWndProc = &Win32Window::WindowProc;
  window_class.lpszClassName = kWindowClassName;
  window_class.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
  window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  RegisterClassExW(&window_class);

  // 创建黑色背景画刷：用于原生编辑框（EDIT 控件）的背景色
  // 传奇客户端的登录输入框使用黑底白字风格
  if (edit_background_brush_ == nullptr) {
    edit_background_brush_ = CreateSolidBrush(RGB(0, 0, 0));
  }

  // 创建窗口：将 this 指针通过 lpCreateParams 传入
  // 以便在 WM_NCCREATE 阶段存储到 GWLP_USERDATA 中
  hwnd_ = CreateWindowExW(0, kWindowClassName, title.c_str(), kFixedWindowStyle | WS_VISIBLE,
                          0, 0, width, height, nullptr, nullptr, instance_, this);
  if (hwnd_ == nullptr) {
    return false;
  }

  ShowWindow(hwnd_, SW_SHOWDEFAULT);
  UpdateWindow(hwnd_);
  update_client_size();
  return true;
}

// 泵送消息队列：使用 PeekMessage 非阻塞处理所有待处理的消息
// 返回 false 表示收到 WM_QUIT（主循环应退出）
// 与 SendMessage/PostMessage 的区别：
// PeekMessage 立即返回，不阻塞主循环（原版使用 Application.ProcessMessages）
bool Win32Window::pump_messages() {
  MSG message{};
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
    if (message.message == WM_QUIT) {
      return false;
    }
    TranslateMessage(&message);   // 将虚拟键消息转换为字符消息（WM_KEYDOWN -> WM_CHAR）
    DispatchMessageW(&message);   // 分派到窗口过程
  }
  return true;
}

void Win32Window::begin_frame() {
  input_.begin_frame();  // 清零瞬态输入标志，准备接收新帧的输入
}

void Win32Window::close_now() {
  PostQuitMessage(0);  // 向消息队列投递 WM_QUIT
}

HWND Win32Window::handle() const { return hwnd_; }

const InputState& Win32Window::input() const { return input_; }

int Win32Window::client_width() const { return client_width_; }

int Win32Window::client_height() const { return client_height_; }

bool Win32Window::was_resized() const { return resized_; }

void Win32Window::clear_resize_flag() { resized_ = false; }

bool Win32Window::consume_close_request() {
  const auto requested = close_requested_;
  close_requested_ = false;
  return requested;
}

// 全局窗口过程（静态方法）
// WM_NCCREATE 时提取 CREATESTRUCT.lpCreateParams 中的 this 指针，
// 存储到 GWLP_USERDATA，后续所有消息转发到实例的 handle_message
LRESULT CALLBACK Win32Window::WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    auto* self = static_cast<Win32Window*>(create->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    self->hwnd_ = hwnd;
  }

  auto* self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self != nullptr) {
    return self->handle_message(message, wparam, lparam);
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

// 实例窗口过程：处理各类 Win32 消息并更新 InputState
// 消息处理顺序：窗口尺寸 -> 鼠标 -> 键盘 -> 文本 -> 编辑框 -> 关闭
LRESULT Win32Window::handle_message(UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_SIZE:
      // 窗口尺寸变化时更新缓存并设置 resized_ 标志
      // 渲染器据此重建后台缓冲区资源
      update_client_size();
      resized_ = true;
      return 0;

    // === 鼠标消息 ===
    // 使用 GET_X_LPARAM/GET_Y_LPARAM 获取鼠标坐标（Windows 10+ 推荐）
    case WM_MOUSEMOVE:
      input_.mouse_x = GET_X_LPARAM(lparam);
      input_.mouse_y = GET_Y_LPARAM(lparam);
      return 0;
    case WM_LBUTTONDOWN:
      SetCapture(hwnd_);  // 捕获鼠标，确保 LBUTTONUP 时能收到消息
      input_.left_down = true;
      input_.left_pressed = true;
      input_.mouse_x = GET_X_LPARAM(lparam);
      input_.mouse_y = GET_Y_LPARAM(lparam);
      return 0;
    case WM_LBUTTONUP:
      ReleaseCapture();   // 释放鼠标捕获
      input_.left_down = false;
      input_.left_released = true;
      input_.mouse_x = GET_X_LPARAM(lparam);
      input_.mouse_y = GET_Y_LPARAM(lparam);
      return 0;
    case WM_RBUTTONDOWN:
      SetCapture(hwnd_);
      input_.right_down = true;
      input_.right_pressed = true;
      input_.mouse_x = GET_X_LPARAM(lparam);
      input_.mouse_y = GET_Y_LPARAM(lparam);
      return 0;
    case WM_RBUTTONUP:
      ReleaseCapture();
      input_.right_down = false;
      input_.right_released = true;
      input_.mouse_x = GET_X_LPARAM(lparam);
      input_.mouse_y = GET_Y_LPARAM(lparam);
      return 0;

    // === 键盘消息 ===
    case WM_KEYDOWN:
      // 仅当该键之前未按下时才设置 pressed 标志
      // 避免 Windows 键盘重复（repeat）导致的重复触发
      if (wparam < input_.key_down.size()) {
        if (!input_.key_down[wparam]) {
          input_.key_pressed[wparam] = true;
        }
        input_.key_down[wparam] = true;
      }
      // 退格键和回车键有独立的瞬态标志，方便 UI 系统直接使用
      if (wparam == VK_BACK) {
        input_.backspace_pressed = true;
      } else if (wparam == VK_RETURN) {
        input_.enter_pressed = true;
      }
      return 0;
    case WM_KEYUP:
      if (wparam < input_.key_down.size()) {
        input_.key_down[wparam] = false;
      }
      return 0;

    // === 文本输入 ===
    case WM_CHAR:
      // 收集可打印字符：排除控制字符（<32）和 DEL（127）
      // 这些字符由 WM_KEYDOWN 直接处理
      if (wparam >= 32 && wparam != 127) {
        input_.text_input.push_back(static_cast<wchar_t>(wparam));
      }
      return 0;

    // === 编辑框控件消息 ===
    case WM_CTLCOLOREDIT:
      // 设置原生 EDIT 控件的文字颜色为白色、背景为黑色
      // 与传奇经典界面的登录输入框风格一致
      SetTextColor(reinterpret_cast<HDC>(wparam), RGB(255, 255, 255));
      SetBkColor(reinterpret_cast<HDC>(wparam), RGB(0, 0, 0));
      return reinterpret_cast<LRESULT>(edit_background_brush_);

    // === 窗口关闭 ===
    case WM_CLOSE:
      close_requested_ = true;  // 不立即关闭，由主循环决定是否退出
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);  // 窗口销毁后投递 WM_QUIT
      return 0;

    default:
      return DefWindowProcW(hwnd_, message, wparam, lparam);
  }
}

// 更新缓存的客户区尺寸
void Win32Window::update_client_size() {
  // GetClientRect 返回窗口客户区（不包括标题栏/边框）的像素尺寸
  RECT rect{};
  GetClientRect(hwnd_, &rect);
  client_width_ = rect.right - rect.left;
  client_height_ = rect.bottom - rect.top;
}

}  // namespace mir2::client
