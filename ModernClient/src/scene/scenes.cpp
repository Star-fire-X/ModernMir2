// ============================================================
// Mir2 现代客户端 — 场景实现
// 职责：实现客户端生命周期中的所有场景，每个场景是独立的状态机节点
//
// 场景流转（对应 Delphi 客户端的窗口跳转）：
//   BootScene (0.15s) → LoginScene → ServerSelectScene → CharacterSelectScene
//   → LoginNoticeScene → LoadingScene → WorldScene
//   其中 LoginNoticeScene 和 LoadingScene 是进入世界前的过渡场景，
//   分别显示服务端公告和"加载中"提示。
//
// 各场景职责概述：
//   BootScene              — 启动过渡（0.15 秒后跳转登录）
//   LoginScene             — 登录/注册/改密表单（Win32 EDIT 控件覆盖）
//   ServerSelectScene      — 服务器列表选择（支持翻页和状态标签）
//   CharacterSelectScene   — 角色列表（选择/创建/删除，精灵预览）
//   LoginNoticeScene       — 显示服务端登录公告
//   LoadingScene           — 等待世界快照加载的过渡画面
//   WorldScene             — 游戏世界主场景（瓦片渲染+物件+角色+特效+UI）
//
// WorldScene 渲染管线（按层级从下到上）：
//   1. render_tiles         — 地面砖块（背景层，包含动画帧物件）
//   2. render_small_objects — 小物件（物体层下半部，被角色遮挡）
//   3. render_ground        — 地面特效（如火墙持续燃烧效果）
//   4. render_world_rows    — 逐行绘制大物件、掉落物、角色、飞行特效
//   5. render_overlay       — 叠加特效（UI 层之上的辅助特效）
//   6. legacy_hud_.paint    — 旧版 HUD（状态栏/背包/聊天板等）
//
// 输入处理优先级（WorldScene::update）：
//   1. legacy_hud_ 快捷操作（快捷键/面板交互）
//   2. 键盘手势（R=返回选角, ESC=取消目标）
//   3. 键盘操作采集（方向键移动、鼠标点击目标等）
//   4. 动作处理（魔法 > 攻击 > 拾取 > 移动，优先级递减）
// ============================================================
#include "scene/scenes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cwctype>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "animation/legacy_animation.hpp"
#include "app/client_app.hpp"
#include "audio/legacy_audio_cue_tracker.hpp"
#include "audio/legacy_sound_rules.hpp"
#include "audio/sound_constants.hpp"
#include "scene/character_select_state.hpp"
#include "shared/legacy/map_render_math.hpp"
#include "shared/legacy/movement_rules.hpp"
#include "text/encoding.hpp"

namespace mir2::client {

// ====================================================================
// 匿名命名空间：辅助函数和常量
// 包含字符串转换、环境变量检测、调试跟踪、精灵绘制和 UI 组件
// ====================================================================
namespace {

std::wstring widen(const std::string& text) { return text::utf8_to_wide(text); }
std::string narrow(const std::wstring& text) { return text::wide_to_utf8(text); }

bool env_flag_enabled(const char* name) {
  char buffer[8]{};
  return GetEnvironmentVariableA(name, buffer, sizeof(buffer)) > 0 && buffer[0] != '0';
}

bool legacy_trace_enabled() {
  static const bool enabled = env_flag_enabled("MIR2_LEGACY_TRACE");
  return enabled;
}

bool debug_arrow_move_enabled() {
  static const bool enabled = env_flag_enabled("MIR2_DEBUG_ARROW_MOVE");
  return enabled;
}

bool map_debug_overlay_enabled() {
  static const bool enabled = env_flag_enabled("MIR2_MAP_DEBUG_OVERLAY");
  return enabled;
}

void legacy_trace(const std::string_view text) {
  if (!legacy_trace_enabled()) {
    return;
  }
  std::string line{"[mir2-legacy] "};
  line.append(text);
  line.push_back('\n');
  OutputDebugStringA(line.c_str());
  char path[1024]{};
  const auto length = GetEnvironmentVariableA("MIR2_LEGACY_TRACE_FILE", path, sizeof(path));
  if (length > 0 && length < sizeof(path)) {
    std::ofstream file(path, std::ios::app | std::ios::binary);
    file << line;
  }
}

std::wstring trim_copy(const std::wstring& text) {
  const auto first = std::find_if_not(text.begin(), text.end(), [](const wchar_t ch) {
    return std::iswspace(ch) != 0;
  });
  if (first == text.end()) {
    return {};
  }
  const auto last = std::find_if_not(text.rbegin(), text.rend(), [](const wchar_t ch) {
    return std::iswspace(ch) != 0;
  }).base();
  return std::wstring(first, last);
}

std::wstring lower_copy(std::wstring text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](const wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
  return text;
}

std::wstring sanitize_password_copy(std::wstring text, const bool reject_spaces) {
  for (auto& ch : text) {
    if (ch == L'~' || ch == L'\'') {
      ch = L'_';
    }
  }
  if (reject_spaces) {
    text.erase(std::remove(text.begin(), text.end(), L' '), text.end());
  }
  return text;
}

// ====================================================================
// 场景常量
// ====================================================================

constexpr int kLoginBackgroundIndex = 22;
constexpr int kChangePasswordDialogIndex = 50;
constexpr int kCancelButtonIndex = 52;
constexpr int kLoginDialogIndex = 60;
constexpr int kLoginChangePasswordButtonIndex = 53;
constexpr int kLoginCreateButtonIndex = 61;
constexpr int kLoginSubmitButtonIndex = 62;
constexpr int kNewAccountDialogIndex = 63;
constexpr int kLoginCloseButtonIndex = 64;
constexpr int kSelectBackgroundIndex = 65;
constexpr int kServerSelectDialogIndex = 256;
constexpr int kSelectLeftButtonIndex = 66;
constexpr int kSelectRightButtonIndex = 67;
constexpr int kSelectStartButtonIndex = 68;
constexpr int kSelectNewButtonIndex = 69;
constexpr int kSelectEraseButtonIndex = 70;
constexpr int kCreateDialogIndex = 73;
constexpr int kCreateWarriorButtonIndex = 74;
constexpr int kCreateWizardButtonIndex = 75;
constexpr int kCreateTaoistButtonIndex = 76;
constexpr int kCreateMaleButtonIndex = 77;
constexpr int kCreateFemaleButtonIndex = 78;
constexpr int kCreatePrevHairButtonIndex = 79;
constexpr int kCreateNextHairButtonIndex = 80;
constexpr int kCreateWarriorSelectedIndex = 55;
constexpr int kCreateWizardSelectedIndex = 56;
constexpr int kCreateTaoistSelectedIndex = 57;
constexpr int kCreateMaleSelectedIndex = 58;
constexpr int kCreateFemaleSelectedIndex = 59;
constexpr int kSelectEffectFirstIndex = 4;
constexpr int kSelectIdleFirstIndex = 40;
constexpr int kSelectFreezeFirstIndex = 60;
constexpr int kSelectedFrameCount = 16;
constexpr int kFreezeFrameCount = 13;
constexpr int kEffectFrameCount = 14;
constexpr int kMessageDialogIndex = 360;
constexpr int kMessageOkButtonIndex = 361;
constexpr int kBottomBoardIndex = 1;
constexpr int kItemBagDialogIndex = 3;
constexpr int kBottomSplitHpMpIndex = 4;
constexpr int kBottomSingleHpBackIndex = 5;
constexpr int kBottomSingleHpFillIndex = 6;
constexpr int kBottomExpWeightIndex = 7;
constexpr int kBottomStateButtonIndex = 8;
constexpr int kBottomBagButtonIndex = 9;
constexpr int kBottomMagicButtonIndex = 10;
constexpr int kBottomOptionButtonIndex = 11;
constexpr int kBottomDayIconIndex = 15;
constexpr int kBottomHungerFirstIndex = 16;
constexpr int kBottomGroupButtonIndex = 128;
constexpr int kBottomMiniMapButtonIndex = 130;
constexpr int kBottomTradeButtonIndex = 132;
constexpr int kBottomGuildButtonIndex = 134;
constexpr int kBottomLogoutButtonIndex = 136;
constexpr int kBottomExitButtonIndex = 138;
constexpr int kBottomPlusAbilityButtonIndex = 140;
constexpr int kBagCloseButtonIndex = 371;
constexpr int kBagRepairButtonIndex = 26;
constexpr int kBagGoldButtonIndex = 29;
constexpr int kStateDialogIndex = 370;
constexpr int kStateCloseButtonIndex = 371;
constexpr int kStateNextButtonIndex = 372;
constexpr int kStatePrevButtonIndex = 373;
constexpr int kStateBodyMaleIndex = 376;
constexpr int kStateBodyFemaleIndex = 377;
constexpr int kStateDetailPageIndex = 382;
constexpr int kStateMagicPageIndex = 383;
constexpr int kStateMagicPageDownIndex = 396;
constexpr int kStateMagicPageUpIndex = 398;
constexpr int kStateMagicLevelIconIndex = 112;
constexpr int kStateMagicExpIconIndex = 111;
constexpr int kMerchantDialogIndex = 384;
constexpr int kMerchantBuyDialogIndex = 385;
constexpr int kMerchantBuyButtonIndex = 386;
constexpr int kMerchantPrevButtonIndex = 387;
constexpr int kMerchantNextButtonIndex = 388;
constexpr int kMerchantSellDialogIndex = 392;
constexpr int kMerchantSellOkButtonIndex = 393;
constexpr int kMerchantCloseButtonIndex = 64;
constexpr std::uint64_t kWorldMainThemeIntervalMs = 46000;
constexpr int kMagicKeyDialogIndex = 229;
constexpr int kMagicKeyNoneButtonIndex = 230;
constexpr int kMagicKeyOkButtonIndex = 62;
constexpr int kMagicKeyIconFirstIndex = 248;
constexpr int kChatBoardX = 208;
constexpr int kChatBoardY = 600 - 130;
constexpr int kChatBoardWidth = 374;
constexpr int kChatBoardLineHeight = 12;
constexpr int kChatBoardVisibleLines = 9;
constexpr std::array<int, 6> kBeltButtonX{285, 328, 371, 415, 459, 503};
constexpr int kNpcDialogTextX = 30;
constexpr int kNpcDialogTextY = 20;
constexpr int kNpcDialogLineHeight = 16;

/// 资源文本编辑框：基于 Win32 EDIT 控件的文本输入组件
/// 职责：在旧版对话框精灵之上叠加原生文本编辑控件，处理密码模式、
///       字符过滤、特殊字符替换和表单提交
class ResourceTextEdit final : public ui::TextEdit {
 public:
  explicit ResourceTextEdit(const RectI bounds) : ui::TextEdit(bounds) {}

  ~ResourceTextEdit() override { detach_native(); }

  bool attach_native(HWND parent, HFONT font, const int max_length, const bool password,
                     const bool replace_special_chars, const bool reject_space) {
    detach_native();
    replace_special_chars_ = replace_special_chars;
    reject_space_ = reject_space;
    password_mode = password;
    const auto rect = resolved_bounds();
    auto style = WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL;
    if (password) {
      style |= ES_PASSWORD;
    }
    native_hwnd_ =
        CreateWindowExW(0, L"EDIT", L"", style, rect.x, rect.y, rect.w, rect.h, parent, nullptr,
                        GetModuleHandleW(nullptr), nullptr);
    if (native_hwnd_ == nullptr) {
      return false;
    }
    native_proc_ = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(native_hwnd_, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(&ResourceTextEdit::NativeEditProc)));
    SetWindowLongPtrW(native_hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SendMessageW(native_hwnd_, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    SendMessageW(native_hwnd_, EM_LIMITTEXT, static_cast<WPARAM>(max_length), 0);
    SendMessageW(native_hwnd_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));
    if (password) {
      SendMessageW(native_hwnd_, EM_SETPASSWORDCHAR, L'*', 0);
    }
    ShowWindow(native_hwnd_, visible ? SW_SHOW : SW_HIDE);
    sync_to_native();
    return true;
  }

  void detach_native() {
    if (native_hwnd_ == nullptr) {
      native_proc_ = nullptr;
      return;
    }
    SetWindowLongPtrW(native_hwnd_, GWLP_USERDATA, 0);
    if (native_proc_ != nullptr) {
      SetWindowLongPtrW(native_hwnd_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(native_proc_));
    }
    DestroyWindow(native_hwnd_);
    native_hwnd_ = nullptr;
    native_proc_ = nullptr;
  }

  void set_native_visible(const bool is_visible) {
    visible = is_visible;
    if (native_hwnd_ != nullptr) {
      ShowWindow(native_hwnd_, is_visible ? SW_SHOW : SW_HIDE);
    }
  }

  void set_native_enabled(const bool is_enabled) {
    enabled = is_enabled;
    if (native_hwnd_ != nullptr) {
      EnableWindow(native_hwnd_, is_enabled ? TRUE : FALSE);
    }
  }

  void sync_native_bounds(const SoftwareRenderer* renderer = nullptr) {
    if (native_hwnd_ == nullptr) {
      return;
    }
    const auto logical_rect = resolved_bounds();
    const auto rect = renderer != nullptr ? renderer->logical_to_window_rect(logical_rect)
                                          : logical_rect;
    SetWindowPos(native_hwnd_, nullptr, rect.x, rect.y, rect.w, rect.h,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }

  void sync_from_native() {
    if (native_hwnd_ == nullptr) {
      return;
    }
    const auto length = GetWindowTextLengthW(native_hwnd_);
    std::wstring text(static_cast<std::size_t>(length) + 1U, L'\0');
    GetWindowTextW(native_hwnd_, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    value = text;
  }

  void sync_to_native() {
    if (native_hwnd_ == nullptr) {
      return;
    }
    SetWindowTextW(native_hwnd_, value.c_str());
  }

  void set_password_mode(const bool password) {
    password_mode = password;
    if (native_hwnd_ != nullptr) {
      SendMessageW(native_hwnd_, EM_SETPASSWORDCHAR, password ? L'*' : 0, 0);
      InvalidateRect(native_hwnd_, nullptr, TRUE);
    }
  }

  void select_all_to_end() {
    if (native_hwnd_ != nullptr) {
      SendMessageW(native_hwnd_, EM_SETSEL, static_cast<WPARAM>(value.size()),
                   static_cast<LPARAM>(value.size()));
    }
  }

  void paint(SoftwareRenderer& renderer) override {
    (void)renderer;
  }

  void on_focus_gained() override {
    show_caret = true;
    if (native_hwnd_ != nullptr) {
      SetFocus(native_hwnd_);
    }
  }
  void on_focus_lost() override { show_caret = false; }

  std::function<void()> on_cancel{};

 private:
  static LRESULT CALLBACK NativeEditProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self =
        reinterpret_cast<ResourceTextEdit*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr) {
      return self->handle_native_message(message, wparam, lparam);
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
  }

  LRESULT handle_native_message(UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_KEYDOWN && wparam == VK_ESCAPE) {
      sync_from_native();
      if (on_cancel) {
        on_cancel();
      }
      return 0;
    }
    if (message == WM_CHAR) {
      if (wparam == VK_RETURN) {
        sync_from_native();
        if (on_submit) {
          on_submit();
        }
        return 0;
      }
      if (wparam == VK_ESCAPE) {
        sync_from_native();
        if (on_cancel) {
          on_cancel();
        }
        return 0;
      }
      if (replace_special_chars_ && (wparam == L'~' || wparam == L'\'')) {
        wparam = L'_';
      }
      if (reject_space_ && wparam == L' ') {
        return 0;
      }
    }
    const auto result = CallWindowProcW(native_proc_, native_hwnd_, message, wparam, lparam);
    if (message == WM_CHAR || message == WM_PASTE || message == WM_KEYUP || message == WM_SETTEXT) {
      sync_from_native();
    }
    return result;
  }

  bool show_caret{false};
  HWND native_hwnd_{nullptr};
  WNDPROC native_proc_{nullptr};
  bool replace_special_chars_{false};
  bool reject_space_{false};
};

/// 根据区域编号返回对应的物件精灵归档
/// 区域 0-6 分别映射到 ArchiveId::objects1 到 objects7
ArchiveId object_archive_for_area(const int area) {
  switch (area) {
    case 0:
      return ArchiveId::objects1;
    case 1:
      return ArchiveId::objects2;
    case 2:
      return ArchiveId::objects3;
    case 3:
      return ArchiveId::objects4;
    case 4:
      return ArchiveId::objects5;
    case 5:
      return ArchiveId::objects6;
    case 6:
      return ArchiveId::objects7;
    default:
      return ArchiveId::objects1;
  }
}

/// 绘制精灵帧到渲染器表面（带全局透明度）
void draw_sprite(SoftwareRenderer& renderer, const std::shared_ptr<const SpriteFrame>& frame,
                 const int x, const int y, const std::uint8_t alpha = 255U) {
  if (frame == nullptr || frame->empty()) {
    return;
  }
  renderer.surface().blit_rgba(x, y, frame->width, frame->height, frame->pixels.data(), alpha);
}

void draw_sprite_legacy_blend(SoftwareRenderer& renderer,
                              const std::shared_ptr<const SpriteFrame>& frame, const int x,
                              const int y) {
  if (frame == nullptr || frame->empty()) {
    return;
  }
  renderer.surface().blit_rgba_legacy_blend(x, y, frame->width, frame->height,
                                            frame->pixels.data());
}

void draw_sprite_region(SoftwareRenderer& renderer,
                        const std::shared_ptr<const SpriteFrame>& frame, const RectI source,
                        const int x, const int y, const std::uint8_t alpha = 255U) {
  if (frame == nullptr || frame->empty() || source.w <= 0 || source.h <= 0) {
    return;
  }
  const auto left = std::clamp(source.x, 0, frame->width);
  const auto top = std::clamp(source.y, 0, frame->height);
  const auto right = std::clamp(source.x + source.w, 0, frame->width);
  const auto bottom = std::clamp(source.y + source.h, 0, frame->height);
  if (right <= left || bottom <= top) {
    return;
  }
  const auto width = right - left;
  const auto height = bottom - top;
  std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) *
                                    static_cast<std::size_t>(height));
  for (int row = 0; row < height; ++row) {
    const auto* src = frame->pixels.data() +
                      static_cast<std::size_t>(top + row) *
                          static_cast<std::size_t>(frame->width) +
                      static_cast<std::size_t>(left);
    auto* dst = pixels.data() + static_cast<std::size_t>(row) *
                                    static_cast<std::size_t>(width);
    std::copy_n(src, width, dst);
  }
  renderer.surface().blit_rgba(x, y, width, height, pixels.data(), alpha);
}

std::shared_ptr<const SpriteFrame> get_frame(ClientContext& context, const ArchiveId archive_id,
                                             const int index) {
  if (context.assets == nullptr) {
    return nullptr;
  }
  return context.assets->get_frame(archive_id, index);
}

RectI sprite_rect(const std::shared_ptr<const SpriteFrame>& frame, const int x, const int y,
                  const int fallback_width, const int fallback_height) {
  return RectI{x, y, frame != nullptr ? frame->width : fallback_width,
               frame != nullptr ? frame->height : fallback_height};
}

RectI centered_rect(const std::shared_ptr<const SpriteFrame>& frame, const int width,
                    const int height, const int fallback_width, const int fallback_height) {
  const auto sprite_width = frame != nullptr ? frame->width : fallback_width;
  const auto sprite_height = frame != nullptr ? frame->height : fallback_height;
  return RectI{(width - sprite_width) / 2, (height - sprite_height) / 2, sprite_width,
               sprite_height};
}

void draw_archive_sprite(ClientContext& context, const ArchiveId archive_id, const int index,
                         const int x, const int y, const std::uint8_t alpha = 255U) {
  draw_sprite(*context.renderer, get_frame(context, archive_id, index), x, y, alpha);
}

std::uint32_t legacy_color_to_argb(const std::uint32_t color) {
  if (color == 0U) {
    return 0U;
  }
  if ((color & 0xFF000000U) != 0U) {
    return color;
  }
  const auto red = color & 0xFFU;
  const auto green = (color >> 8U) & 0xFFU;
  const auto blue = (color >> 16U) & 0xFFU;
  return 0xFF000000U | (red << 16U) | (green << 8U) | blue;
}

std::string trim_ascii_copy(std::string text) {
  const auto first = std::find_if_not(text.begin(), text.end(), [](const unsigned char ch) {
    return std::isspace(ch) != 0;
  });
  if (first == text.end()) {
    return {};
  }
  const auto last = std::find_if_not(text.rbegin(), text.rend(), [](const unsigned char ch) {
    return std::isspace(ch) != 0;
  }).base();
  return std::string(first, last);
}

std::string extract_chat_user_name(const std::string& line) {
  auto text = trim_ascii_copy(line);
  if (text.empty()) {
    return {};
  }
  if (text.front() == '[') {
    const auto end = text.find(']');
    if (end != std::string::npos && end > 1) {
      return text.substr(1, end - 1);
    }
  }
  while (!text.empty() && (text.front() == '/' || text.front() == '!' ||
                           text.front() == '@' || text.front() == '*')) {
    text.erase(text.begin());
  }
  text = trim_ascii_copy(text);
  if (text.empty()) {
    return {};
  }
  const auto end = text.find_first_of(" :=\t\r\n");
  auto name = end == std::string::npos ? text : text.substr(0, end);
  if (!name.empty() && name.back() == ':') {
    name.pop_back();
  }
  return name.size() >= 2 ? name : std::string{};
}

class ChatBoardNode final : public ui::UiNode {
 public:
  explicit ChatBoardNode(const RectI bounds) : ui::UiNode(bounds) {}

  void paint(SoftwareRenderer& renderer) override {
    if (state == nullptr) {
      return;
    }
    const auto rect = resolved_bounds();
    auto& world = state->world;
    const auto count = static_cast<int>(world.chat_lines.size());
    const auto max_top = std::max(0, count - kChatBoardVisibleLines);
    const auto top_index = std::clamp(world.chat_board_top, 0, max_top);

    int visual_row = 0;
    for (int logical = top_index; logical < count && visual_row < kChatBoardVisibleLines; ++logical) {
      const auto& line = world.chat_lines[static_cast<std::size_t>(logical)];
      const auto back = legacy_color_to_argb(line.back_color);
      const auto fore = legacy_color_to_argb(line.fore_color);
      auto text = widen(line.text);
      if (text.empty()) {
        text = L" ";
      }
      int line_start = 0;
      while (line_start < static_cast<int>(text.size()) && visual_row < kChatBoardVisibleLines) {
        int line_end = line_start;
        int cur_width = 0;
        while (line_end < static_cast<int>(text.size())) {
          const auto ch = text[static_cast<std::size_t>(line_end)];
          const auto ch_width =
              renderer.measure_text_width(text.substr(static_cast<std::size_t>(line_start),
                                                       static_cast<std::size_t>(line_end - line_start + 1)));
          if (ch_width > rect.w && line_end > line_start) {
            break;
          }
          cur_width = ch_width;
          ++line_end;
          if (cur_width >= rect.w) {
            break;
          }
        }
        if (line_end == line_start) {
          ++line_end;
        }
        const auto sub =
            text.substr(static_cast<std::size_t>(line_start),
                        static_cast<std::size_t>(line_end - line_start));
        const RectI line_rect{rect.x, rect.y + visual_row * kChatBoardLineHeight, rect.w,
                              kChatBoardLineHeight};
        if ((back >> 24U) != 0U) {
          renderer.fill_rect(line_rect, back);
        }
        renderer.draw_text(line_rect.x, line_rect.y, sub, fore);
        ++visual_row;
        line_start = line_end;
      }
    }
    UiNode::paint(renderer);
  }

  bool on_mouse_down(ui::UiTree& tree, const InputState& input,
                     const ui::UiMouseButton button) override {
    (void)tree;
    if (button != ui::UiMouseButton::left || state == nullptr) {
      return false;
    }
    const auto rect = resolved_bounds();
    const auto row = (input.mouse_y - rect.y) / kChatBoardLineHeight;
    if (row < 0 || row >= kChatBoardVisibleLines) {
      return false;
    }
    const auto& lines = state->world.chat_lines;
    const auto count = static_cast<int>(lines.size());
    const auto max_top = std::max(0, count - kChatBoardVisibleLines);
    const auto index = std::clamp(state->world.chat_board_top, 0, max_top) + row;
    if (index >= 0 && index < count && on_whisper != nullptr) {
      on_whisper(lines[static_cast<std::size_t>(index)].text);
    }
    return true;
  }

  GameStateStore* state{nullptr};
  std::function<void(const std::string&)> on_whisper{};
};

class NpcDialogNode final : public ui::Window {
 public:
  explicit NpcDialogNode(const RectI bounds) : ui::Window(bounds) {}

  void set_dialog(const NpcDialogState& dialog) {
    merchant_id = dialog.merchant_id;
    face_index = dialog.face;
    npc_name = dialog.npc_name;
    dialog_text = dialog.text;
  }

  void clear_dialog() {
    merchant_id = 0;
    face_index = 0;
    npc_name.clear();
    dialog_text.clear();
    selected_command_.clear();
    click_points_.clear();
  }

  void paint(SoftwareRenderer& renderer) override {
    ui::Window::paint(renderer);
    click_points_.clear();
    const auto rect = resolved_bounds();
    if (!npc_name.empty()) {
      renderer.draw_text(rect.x + kNpcDialogTextX, rect.y + 4, widen(npc_name), 0xFFFFFF66U);
    }

    auto y = rect.y + kNpcDialogTextY;
    auto center = false;
    std::wstring line;
    const auto text = widen(dialog_text);
    for (const auto ch : text) {
      if (ch == L'\r') {
        continue;
      }
      if (ch == L'\n') {
        draw_dialog_line(renderer, line, y, center);
        line.clear();
        y += kNpcDialogLineHeight;
        continue;
      }
      line.push_back(ch);
    }
    if (!line.empty()) {
      draw_dialog_line(renderer, line, y, center);
    }
  }

  bool on_mouse_down(ui::UiTree& tree, const InputState& input,
                     const ui::UiMouseButton button) override {
    if (button != ui::UiMouseButton::left) {
      return ui::Window::on_mouse_down(tree, input, button);
    }
    if (GetTickCount64() < cooldown_until_ms_) {
      return true;
    }
    const auto command = command_at(input.mouse_x, input.mouse_y);
    if (!command.empty()) {
      selected_command_ = command;
      tree.set_capture(this);
      return true;
    }
    return ui::Window::on_mouse_down(tree, input, button);
  }

  bool on_mouse_up(ui::UiTree& tree, const InputState& input,
                   const ui::UiMouseButton button) override {
    if (button != ui::UiMouseButton::left) {
      return ui::Window::on_mouse_up(tree, input, button);
    }
    if (tree.captured() == this && !selected_command_.empty()) {
      const auto command = command_at(input.mouse_x, input.mouse_y);
      if (command == selected_command_ && on_select != nullptr) {
        on_select(merchant_id, command);
        cooldown_until_ms_ = GetTickCount64() + 5000U;
      }
      selected_command_.clear();
      tree.release_capture(this);
      return true;
    }
    return ui::Window::on_mouse_up(tree, input, button);
  }

  std::uint64_t merchant_id{0};
  int face_index{0};
  std::string npc_name{};
  std::string dialog_text{};
  std::function<void(std::uint64_t, std::string)> on_select{};

 private:
  struct LinkRun {
    std::wstring display{};
    std::string command{};
    bool link{false};
    bool center_toggle{false};
    bool center_value{false};
  };

  std::vector<LinkRun> parse_dialog_line(const std::wstring& line) const {
    std::vector<LinkRun> runs;
    std::wstring plain;
    for (std::size_t index = 0; index < line.size();) {
      if (line[index] != L'<') {
        plain.push_back(line[index++]);
        continue;
      }
      const auto close = line.find(L'>', index + 1U);
      if (close == std::wstring::npos) {
        plain.push_back(line[index++]);
        continue;
      }
      if (!plain.empty()) {
        runs.push_back(LinkRun{plain, {}, false, false, false});
        plain.clear();
      }
      const auto tag = line.substr(index + 1U, close - index - 1U);
      if (tag == L"C" || tag == L"c") {
        runs.push_back(LinkRun{{}, {}, false, true, true});
      } else if (tag == L"/C" || tag == L"/c") {
        runs.push_back(LinkRun{{}, {}, false, true, false});
      } else {
        const auto slash = tag.find(L'/');
        const auto display = slash == std::wstring::npos ? tag : tag.substr(0, slash);
        const auto command = slash == std::wstring::npos ? tag : tag.substr(slash + 1U);
        runs.push_back(LinkRun{display, narrow(command), true, false, false});
      }
      index = close + 1U;
    }
    if (!plain.empty()) {
      runs.push_back(LinkRun{plain, {}, false, false, false});
    }
    return runs;
  }

  void draw_dialog_line(SoftwareRenderer& renderer, const std::wstring& line, const int y,
                        bool& center) {
    auto runs = parse_dialog_line(line);
    auto total_width = 0;
    for (const auto& run : runs) {
      if (run.center_toggle) {
        continue;
      }
      total_width += renderer.measure_text_width(run.display);
    }
    const auto rect = resolved_bounds();
    auto x = center ? rect.x + std::max(0, (rect.w - total_width) / 2)
                    : rect.x + kNpcDialogTextX;
    for (const auto& run : runs) {
      if (run.center_toggle) {
        center = run.center_value;
        x = center ? rect.x + std::max(0, (rect.w - total_width) / 2)
                   : rect.x + kNpcDialogTextX;
        continue;
      }
      const auto width = renderer.measure_text_width(run.display);
      if (run.link) {
        const auto pressed = !selected_command_.empty() && selected_command_ == run.command;
        const auto color = pressed ? 0xFFFF4B4BU : 0xFFFFFF66U;
        renderer.draw_text(x, y, run.display, color);
        if (width > 0) {
          renderer.fill_rect(RectI{x, y + 13, width, 1}, color);
          click_points_.push_back(ClickPoint{RectI{x, y, width, kNpcDialogLineHeight},
                                             run.command});
        }
      } else {
        renderer.draw_text(x, y, run.display, 0xFFF8FAFCU);
      }
      x += width;
    }
  }

  [[nodiscard]] std::string command_at(const int screen_x, const int screen_y) const {
    for (const auto& point : click_points_) {
      if (point.rect.contains(screen_x, screen_y)) {
        return point.command;
      }
    }
    return {};
  }

  struct ClickPoint {
    RectI rect{};
    std::string command{};
  };

  std::vector<ClickPoint> click_points_{};
  std::string selected_command_{};
  std::uint64_t cooldown_until_ms_{0};
};

/// 热点按钮：支持自定义绘制回调的精灵按钮
/// 扩展 SpriteButton，添加 fallback 绘制模式和 on_custom_paint 回调
class HotspotButton final : public ui::SpriteButton {
 public:
  HotspotButton(RectI bounds, std::shared_ptr<const SpriteFrame> frame,
                std::shared_ptr<const SpriteFrame> pressed_frame = nullptr)
      : ui::SpriteButton(bounds, std::move(frame), std::move(pressed_frame)) {}

  void paint(SoftwareRenderer& renderer) override {
    if (on_custom_paint) {
      on_custom_paint(renderer, resolved_bounds());
    }
    if (!draw_fallback && (frame == nullptr || frame->empty())) {
      ui::UiNode::paint(renderer);
      return;
    }
    ui::SpriteButton::paint(renderer);
  }

  std::uint32_t text_color{0xFFF5F7FAU};
  bool center_text{false};
  bool draw_fallback{true};
  std::function<void(SoftwareRenderer&, const RectI&)> on_custom_paint{};
};

HotspotButton* add_sprite_button(ui::UiNode* parent, ClientContext& context,
                                 const ArchiveId archive_id, const int index, const int x,
                                 const int y, const int fallback_width = 88,
                                 const int fallback_height = 28) {
  const auto frame = get_frame(context, archive_id, index);
  auto* button = parent->emplace_child<HotspotButton>(
      sprite_rect(frame, x, y, fallback_width, fallback_height), frame);
  button->face = ui::LegacySpriteRef{archive_id, index};
  button->real_hit_test_enabled = true;
  return button;
}

HotspotButton* add_hotspot_button(ui::UiNode* parent, const RectI bounds) {
  return parent->emplace_child<HotspotButton>(bounds, nullptr);
}

void play_legacy_click(AudioService* audio, const LegacyClickSound sound) {
  if (audio == nullptr) {
    return;
  }
  if (const auto sound_id = legacy_click_sound_id(sound); sound_id.has_value()) {
    audio->play_sound(*sound_id);
  }
}

template <typename Callback>
void bind_audio_click(HotspotButton* button, AudioService* audio, const LegacyClickSound sound,
                      Callback&& callback) {
  if (button == nullptr) {
    return;
  }
  button->on_click = [audio, sound,
                      callback = std::function<void()>(std::forward<Callback>(callback))]() {
    play_legacy_click(audio, sound);
    callback();
  };
}

void play_item_click(AudioService* audio, const client_v1::ItemState& item) {
  if (audio != nullptr && !item_empty(item)) {
    audio->play_sound(item_click_sound_id(item.std_mode, item.name));
  }
}

void play_item_use(AudioService* audio, const client_v1::ItemState& item) {
  if (audio == nullptr || item_empty(item)) {
    return;
  }
  if (const auto sound_id = item_use_sound_id(item.std_mode); sound_id.has_value()) {
    audio->play_sound(*sound_id);
  }
}

ui::Window* add_sprite_window(ui::UiNode* parent, ClientContext& context,
                              const ArchiveId archive_id, const int index, const int x,
                              const int y, const int fallback_width,
                              const int fallback_height) {
  const auto frame = get_frame(context, archive_id, index);
  auto* window = parent->emplace_child<ui::Window>(
      sprite_rect(frame, x, y, fallback_width, fallback_height));
  window->background_sprite = ui::LegacySpriteRef{archive_id, index};
  window->background_frame = frame;
  window->face = window->background_sprite;
  window->hit_frame = frame;
  window->real_hit_test_enabled = frame != nullptr && !frame->empty();
  return window;
}

constexpr int kBagGridFirstSlot = 6;
constexpr int kBagGridColumns = 8;
constexpr int kBagGridRows = 5;
constexpr int kBagCellWidth = 36;
constexpr int kBagCellHeight = 32;
constexpr int kEquipDress = 0;
constexpr int kEquipWeapon = 1;
constexpr int kEquipRightHand = 2;
constexpr int kEquipNecklace = 3;
constexpr int kEquipHelmet = 4;
constexpr int kEquipArmRingLeft = 5;
constexpr int kEquipArmRingRight = 6;
constexpr int kEquipRingLeft = 7;
constexpr int kEquipRingRight = 8;
constexpr int kEquipBujuk = 9;
constexpr int kEquipBelt = 10;
constexpr int kEquipBoots = 11;
constexpr int kEquipCharm = 12;
constexpr int kVisibleEquipmentSlotCount = 9;

/// 检查装备槽位是否接受指定 std_mode 类型的物品
bool equipment_slot_accepts_std_mode(const int slot, const std::uint8_t std_mode,
                                     const std::uint8_t sex = 255U) {
  switch (std_mode) {
    case 5:
    case 6:
      return slot == kEquipWeapon;
    case 10:
      return slot == kEquipDress && (sex == 255U || sex == 0U);
    case 11:
      return slot == kEquipDress && (sex == 255U || sex == 1U);
    case 15:
      return slot == kEquipHelmet;
    case 19:
    case 20:
    case 21:
      return slot == kEquipNecklace;
    case 22:
    case 23:
      return slot == kEquipRingRight || slot == kEquipRingLeft;
    case 24:
    case 26:
      return slot == kEquipArmRingRight || slot == kEquipArmRingLeft;
    case 30:
      return slot == kEquipRightHand;
    default:
      return false;
  }
}

bool is_belt_slot(const int slot) { return slot >= 0 && slot < 6; }

bool belt_slot_accepts_item(const client_v1::ItemState& item) { return item.std_mode <= 3; }

bool item_usable_from_bag(const client_v1::ItemState& item) {
  return !item_empty(item) && (item.std_mode <= 4 || item.std_mode == 31);
}

int item_stack_count(const client_v1::ItemState& item) {
  if (item.std_mode > 3 || item.dura == 0 || item.dura_max == 0) {
    return 0;
  }
  const auto count = item.dura / 1000;
  return count > 1 ? count : 0;
}

std::shared_ptr<const SpriteFrame> item_icon_frame(AssetManager* assets,
                                                   const client_v1::ItemState& item,
                                                   const ArchiveId primary_archive) {
  if (assets == nullptr || item_empty(item) || item.looks < 0) {
    return nullptr;
  }
  auto frame = assets->get_frame(primary_archive, item.looks);
  if ((frame == nullptr || frame->empty()) && primary_archive != ArchiveId::items) {
    frame = assets->get_frame(ArchiveId::items, item.looks);
  }
  return frame;
}

void draw_item_fallback(SoftwareRenderer& renderer, const RectI& rect) {
  const RectI icon{rect.x + rect.w / 2 - 8, rect.y + rect.h / 2 - 8, 16, 16};
  renderer.fill_rect(icon, 0x88475569U);
  renderer.stroke_rect(icon, 0xFFCBD5E1U);
}

constexpr std::uint32_t kDuraLowBorderColor = 0xCCEF4444U;

void draw_bag_item_icon(SoftwareRenderer& renderer,
                        const std::shared_ptr<const SpriteFrame>& frame,
                        const RectI& cell_rect, const bool low_dura) {
  if (frame == nullptr || frame->empty()) {
    draw_item_fallback(renderer, cell_rect);
    return;
  }
  const auto x = cell_rect.x + (cell_rect.w - frame->width) / 2 - 1;
  const auto y = cell_rect.y + (cell_rect.h - frame->height) / 2 + 1;
  renderer.surface().blit_rgba(x, y, frame->width, frame->height, frame->pixels.data(), 255U);
  if (low_dura) {
    renderer.stroke_rect(cell_rect, kDuraLowBorderColor);
  }
}

bool item_low_dura(const client_v1::ItemState& item) {
  return item.dura_max > 0 && item.dura > 0 && item.dura < item.dura_max / 3;
}

void draw_equipment_item_icon(SoftwareRenderer& renderer,
                              const std::shared_ptr<const SpriteFrame>& frame,
                              const RectI& slot_rect) {
  if (frame == nullptr || frame->empty()) {
    draw_item_fallback(renderer, slot_rect);
    return;
  }
  const auto x = slot_rect.x + (slot_rect.w - frame->width) / 2;
  const auto y = slot_rect.y + (slot_rect.h - frame->height) / 2;
  renderer.surface().blit_rgba(x, y, frame->width, frame->height, frame->pixels.data(), 255U);
}

const wchar_t* std_mode_name(const std::uint8_t std_mode) {
  switch (std_mode) {
    case 0:  return L"药品";
    case 1:  return L"食物";
    case 2:  return L"食物";
    case 3:  return L"食物";
    case 4:  return L"技能书";
    case 5:
    case 6:  return L"武器";
    case 10:
    case 11: return L"衣服";
    case 15: return L"头盔";
    case 19:
    case 20:
    case 21: return L"项链";
    case 22:
    case 23: return L"戒指";
    case 24:
    case 26: return L"手镯";
    case 25: return L"毒药";
    case 30: return L"蜡烛";
    case 31: return L"特殊";
    case 40: return L"肉类";
    case 42: return L"酒";
    case 43: return L"矿石";
    default: return L"物品";
  }
}

std::uint32_t item_name_color(const std::uint8_t std_mode) {
  if (std_mode >= 25)  return 0xFFFACC15U;
  if (std_mode >= 19)  return 0xFF60A5FAU;
  if (std_mode >= 10)  return 0xFF4ADE80U;
  return 0xFFF5F7FAU;
}

std::wstring item_tooltip_text(const client_v1::ItemState& item) {
  auto text = widen(item.name);
  text.append(L"\n");
  text.append(std_mode_name(item.std_mode));
  if (item.dura_max != 0) {
    text.append(L"\n持久 ");
    text.append(std::to_wstring(item.dura / 1000));
    text.push_back(L'/');
    text.append(std::to_wstring(item.dura_max / 1000));
  }
  return text;
}

int proportional_width(const std::int64_t value, const std::int64_t max_value,
                       const int full_width) {
  if (value <= 0 || max_value <= 0 || full_width <= 0) {
    return 0;
  }
  const auto clamped = std::clamp<std::int64_t>(value, 0, max_value);
  return static_cast<int>((static_cast<double>(full_width) *
                           static_cast<double>(clamped)) /
                          static_cast<double>(max_value));
}

int clipped_bar_top(const std::int64_t value, const std::int64_t max_value,
                    const int full_height) {
  if (max_value <= 0 || full_height <= 0) {
    return full_height;
  }
  const auto clamped = std::clamp<std::int64_t>(value, 0, max_value);
  return std::clamp(static_cast<int>(std::lround(
                        (static_cast<double>(full_height) *
                         static_cast<double>(max_value - clamped)) /
                        static_cast<double>(max_value))),
                    0, full_height);
}

void draw_legacy_text(SoftwareRenderer& renderer, const int x, const int y,
                      const std::wstring& text, const std::uint32_t color = 0xFFF5F7FAU) {
  renderer.draw_text(x + 1, y, text, 0xCC000000U);
  renderer.draw_text(x, y, text, color);
}

class LegacyBottomStatusNode final : public ui::UiNode {
 public:
  explicit LegacyBottomStatusNode(const RectI bounds) : ui::UiNode(bounds) {
    enabled = false;
    background = true;
  }

  void paint(SoftwareRenderer& renderer) override {
    if (state == nullptr || assets == nullptr) {
      ui::UiNode::paint(renderer);
      return;
    }

    const auto rect = resolved_bounds();
    const auto& world = state->world;
    const auto self_it = world.actors.find(world.self_actor_id);
    const ActorState* self = self_it != world.actors.end() ? &self_it->second : nullptr;
    const auto& ability = world.self_ability;

    draw_sprite(renderer, assets->get_frame(ArchiveId::prguse, kBottomDayIconIndex),
                rect.x + 748, rect.y + 79);
    draw_hp_mp(renderer, rect, ability, self);
    draw_exp_weight(renderer, ability);
    draw_hunger(renderer, ability);
    draw_belt(renderer, rect, world);

    ui::UiNode::paint(renderer);
  }

  GameStateStore* state{nullptr};
  AssetManager* assets{nullptr};

 private:
  void draw_hp_mp(SoftwareRenderer& renderer, const RectI& rect,
                  const client_v1::SelfAbility& ability, const ActorState* self) const {
    if (self == nullptr || self->max_hp <= 0 || self->max_mp <= 0) {
      return;
    }
    const auto hp_text = std::to_wstring(self->hp) + L'/' + std::to_wstring(self->max_hp);
    const auto mp_text = std::to_wstring(self->mp) + L'/' + std::to_wstring(self->max_mp);
    if (ability.job == 0 && ability.level < 26) {
      const auto back = assets->get_frame(ArchiveId::prguse, kBottomSingleHpBackIndex);
      if (back != nullptr && !back->empty()) {
        draw_sprite_region(renderer, back, RectI{0, 0, std::max(0, back->width - 2), back->height},
                           rect.x + 38, rect.y + 90);
      }
      const auto fill = assets->get_frame(ArchiveId::prguse, kBottomSingleHpFillIndex);
      if (fill != nullptr && !fill->empty()) {
        const auto top = clipped_bar_top(self->hp, self->max_hp, fill->height);
        draw_sprite_region(renderer, fill,
                           RectI{0, top, std::max(0, fill->width - 2), fill->height - top},
                           rect.x + 38, rect.y + 90 + top);
      }
      draw_legacy_text(renderer, rect.x + 44, rect.y + 95, hp_text);
      draw_legacy_text(renderer, rect.x + 44, rect.y + 107, mp_text);
      return;
    }

    const auto split = assets->get_frame(ArchiveId::prguse, kBottomSplitHpMpIndex);
    if (split == nullptr || split->empty()) {
      return;
    }
    const auto hp_right = std::max(0, split->width / 2 - 1);
    const auto mp_left = std::min(split->width, split->width / 2 + 1);
    const auto hp_top = clipped_bar_top(self->hp, self->max_hp, split->height);
    const auto mp_top = clipped_bar_top(self->mp, self->max_mp, split->height);
    draw_sprite_region(renderer, split, RectI{0, hp_top, hp_right, split->height - hp_top},
                       rect.x + 40, rect.y + 91 + hp_top);
    draw_sprite_region(renderer, split,
                       RectI{mp_left, mp_top, std::max(0, split->width - mp_left - 1),
                             split->height - mp_top},
                       rect.x + 40 + mp_left, rect.y + 91 + mp_top);
    draw_legacy_text(renderer, rect.x + 46, rect.y + 96, hp_text);
    draw_legacy_text(renderer, rect.x + 46 + mp_left, rect.y + 96, mp_text);
  }

  void draw_exp_weight(SoftwareRenderer& renderer,
                       const client_v1::SelfAbility& ability) const {
    draw_legacy_text(renderer, 660, 496, std::to_wstring(ability.level));
    const auto bar = assets->get_frame(ArchiveId::prguse, kBottomExpWeightIndex);
    if (bar == nullptr || bar->empty()) {
      return;
    }
    const auto exp_width = proportional_width(ability.exp, ability.max_exp, bar->width);
    draw_sprite_region(renderer, bar, RectI{0, 0, exp_width, bar->height}, 666, 527);
    draw_legacy_text(renderer, 666, 512,
                     std::to_wstring(ability.exp) + L'/' + std::to_wstring(ability.max_exp));
    const auto weight_width = proportional_width(ability.weight, ability.max_weight, bar->width);
    draw_sprite_region(renderer, bar, RectI{0, 0, weight_width, bar->height}, 666, 560);
    draw_legacy_text(renderer, 666, 546,
                     std::to_wstring(ability.weight) + L'/' + std::to_wstring(ability.max_weight));
    if (ability.gold != 0) {
      const auto gold_text = L"Gold: " + std::to_wstring(ability.gold);
      draw_legacy_text(renderer, 660, 472, gold_text, 0xFFFFE08AU);
    }
  }

  void draw_hunger(SoftwareRenderer& renderer,
                   const client_v1::SelfAbility& ability) const {
    if (ability.hunger_state < 1 || ability.hunger_state > 4) {
      return;
    }
    draw_sprite(renderer,
                assets->get_frame(ArchiveId::prguse,
                                  kBottomHungerFirstIndex + ability.hunger_state - 1),
                754, 553);
  }

  void draw_belt(SoftwareRenderer& renderer, const RectI& rect,
                 const WorldViewState& world) const {
    for (int slot = 0; slot < 6; ++slot) {
      const RectI cell{rect.x + kBeltButtonX[static_cast<std::size_t>(slot)], rect.y + 59, 32,
                       29};
      const auto& item = world.bag_items[static_cast<std::size_t>(slot)];
      if (!item_empty(item)) {
        draw_bag_item_icon(renderer, item_icon_frame(assets, item, ArchiveId::items), cell,
                           item_low_dura(item));
        if (const auto count = item_stack_count(item); count > 0) {
          draw_legacy_text(renderer, cell.x + 18, cell.y + 19,
                           std::to_wstring(count));
        }
      }
      draw_legacy_text(renderer, cell.x + 13, cell.y + 19, std::to_wstring(slot + 1));
    }
  }
};

int magic_key_icon_index(const std::uint8_t key) {
  if (key < 1 || key > 8) {
    return -1;
  }
  return kMagicKeyIconFirstIndex + static_cast<int>(key) - 1;
}

class LegacyStateContentNode final : public ui::UiNode {
 public:
  explicit LegacyStateContentNode(const RectI bounds) : ui::UiNode(bounds) {
    enabled = false;
  }

  void paint(SoftwareRenderer& renderer) override {
    if (state == nullptr || assets == nullptr || state_page == nullptr ||
        magic_page == nullptr) {
      ui::UiNode::paint(renderer);
      return;
    }
    switch (*state_page) {
      case 1:
        draw_base_ability(renderer);
        break;
      case 2:
        draw_detail_ability(renderer);
        break;
      case 3:
        draw_magic_page(renderer);
        break;
      case 0:
      default:
        draw_equip_page(renderer);
        break;
    }
    ui::UiNode::paint(renderer);
  }

  GameStateStore* state{nullptr};
  AssetManager* assets{nullptr};
  int* state_page{nullptr};
  int* magic_page{nullptr};

 private:
  void draw_equip_page(SoftwareRenderer& renderer) const {
    const auto rect = resolved_bounds();
    const auto& detail = state->world.self_ability_detail;
    draw_sprite(renderer,
                assets->get_frame(ArchiveId::prguse,
                                  detail.sex == 1 ? kStateBodyFemaleIndex
                                                  : kStateBodyMaleIndex),
                rect.x + 78, rect.y + 78);
    draw_legacy_text(renderer, rect.x + 41, rect.y + 57, widen(state->display_name),
                     0xFFFFFF66U);
    if (!detail.guild_name.empty()) {
      draw_legacy_text(renderer, rect.x + 41, rect.y + 254, widen(detail.guild_name),
                       0xFFC7D2FEU);
    }
    draw_legacy_text(renderer, rect.x + 155, rect.y + 57,
                     L"Lv " + std::to_wstring(detail.level), 0xFFFFFF66U);
  }

  void draw_base_ability(SoftwareRenderer& renderer) const {
    const auto rect = resolved_bounds();
    const auto& detail = state->world.self_ability_detail;
    draw_legacy_text(renderer, rect.x + 43, rect.y + 58, L"Basic", 0xFFFFFF66U);
    draw_pair(renderer, 48, 84, L"Level", detail.level);
    draw_pair(renderer, 48, 100, L"HP", detail.hp, detail.max_hp);
    draw_pair(renderer, 48, 116, L"MP", detail.mp, detail.max_mp);
    draw_pair(renderer, 48, 132, L"Exp", detail.exp, detail.max_exp);
    draw_pair(renderer, 48, 148, L"Weight", detail.weight, detail.max_weight);
    draw_pair(renderer, 48, 164, L"Wear", detail.wear_weight, detail.max_wear_weight);
    draw_pair(renderer, 48, 180, L"Hand", detail.hand_weight, detail.max_hand_weight);
    draw_pair(renderer, 48, 196, L"AC", detail.ac);
    draw_pair(renderer, 48, 212, L"MAC", detail.mac);
    draw_pair(renderer, 48, 228, L"DC", detail.dc);
    draw_pair(renderer, 48, 244, L"MC", detail.mc);
    draw_pair(renderer, 48, 260, L"SC", detail.sc);
  }

  void draw_detail_ability(SoftwareRenderer& renderer) const {
    const auto rect = resolved_bounds();
    draw_sprite(renderer, assets->get_frame(ArchiveId::prguse, kStateDetailPageIndex),
                rect.x + 37, rect.y + 52);
    const auto& detail = state->world.self_ability_detail;
    draw_pair(renderer, 55, 82, L"Hit", detail.hit);
    draw_pair(renderer, 55, 99, L"Speed", detail.speed);
    draw_pair(renderer, 55, 116, L"AntiMagic", detail.anti_magic);
    draw_pair(renderer, 55, 133, L"AntiPoison", detail.anti_poison);
    draw_pair(renderer, 55, 150, L"PoisonRec", detail.poison_recover);
    draw_pair(renderer, 55, 167, L"HealthRec", detail.health_recover);
    draw_pair(renderer, 55, 184, L"SpellRec", detail.spell_recover);
    if (!detail.guild_rank_name.empty()) {
      draw_legacy_text(renderer, rect.x + 55, rect.y + 216,
                       L"Rank " + widen(detail.guild_rank_name), 0xFFC7D2FEU);
    }
  }

  void draw_magic_page(SoftwareRenderer& renderer) const {
    const auto rect = resolved_bounds();
    draw_sprite(renderer, assets->get_frame(ArchiveId::prguse, kStateMagicPageIndex),
                rect.x + 38, rect.y + 52);
    const auto& magics = state->world.magics;
    const auto max_page =
        std::max(0, (static_cast<int>(magics.size()) + 4) / 5 - 1);
    *magic_page = std::clamp(*magic_page, 0, max_page);
    for (int row = 0; row < 5; ++row) {
      const auto index = *magic_page * 5 + row;
      if (index < 0 || index >= static_cast<int>(magics.size())) {
        continue;
      }
      const auto& magic = magics[static_cast<std::size_t>(index)];
      const auto y = 59 + row * 37;
      auto icon = assets->get_frame(ArchiveId::mag_icon, magic.effect * 2);
      draw_sprite(renderer, icon, rect.x + 46, rect.y + y);
      draw_legacy_text(renderer, rect.x + 84, rect.y + y, widen(magic.name), 0xFFFFFF66U);
      draw_sprite(renderer,
                  assets->get_frame(ArchiveId::prguse, kStateMagicLevelIconIndex),
                  rect.x + 84, rect.y + y + 13);
      draw_sprite(renderer,
                  assets->get_frame(ArchiveId::prguse, kStateMagicExpIconIndex),
                  rect.x + 84 + 26, rect.y + y + 13);
      draw_legacy_text(renderer, rect.x + 84 + 13, rect.y + y + 13,
                       std::to_wstring(magic.level),
                       0xFFE5E7EBU);
      draw_legacy_text(renderer, rect.x + 84 + 26 + 13, rect.y + y + 13,
                       std::to_wstring(magic.train) + L"/" +
                           std::to_wstring(std::max(0, magic.max_train)),
                       0xFFE5E7EBU);
      if (const auto key_index = magic_key_icon_index(magic.key); key_index >= 0) {
        draw_sprite(renderer, assets->get_frame(ArchiveId::prguse, key_index),
                    rect.x + 183, rect.y + y + 1);
      }
    }
  }

  void draw_pair(SoftwareRenderer& renderer, const int x, const int y,
                 const std::wstring& name, const std::uint32_t value) const {
    const auto rect = resolved_bounds();
    draw_legacy_text(renderer, rect.x + x, rect.y + y,
                     name + L": " + std::to_wstring(value), 0xFFE5E7EBU);
  }

  void draw_pair(SoftwareRenderer& renderer, const int x, const int y,
                 const std::wstring& name, const std::uint32_t value,
                 const std::uint32_t max_value) const {
    const auto rect = resolved_bounds();
    draw_legacy_text(renderer, rect.x + x, rect.y + y,
                     name + L": " + std::to_wstring(value) + L"/" +
                         std::to_wstring(max_value),
                     0xFFE5E7EBU);
  }
};

class MerchantGoodsNode final : public ui::UiNode {
 public:
  explicit MerchantGoodsNode(const RectI bounds) : ui::UiNode(bounds) {
    enabled = false;
  }

  void paint(SoftwareRenderer& renderer) override {
    if (state == nullptr || assets == nullptr || selected_index == nullptr) {
      ui::UiNode::paint(renderer);
      return;
    }
    const auto rect = resolved_bounds();
    const auto& shop = state->world.merchant_shop;
    const auto page = std::clamp(shop.page, 0,
                                 std::max(0, (static_cast<int>(shop.goods.size()) + 4) / 5 - 1));
    for (int row = 0; row < 5; ++row) {
      const auto index = page * 5 + row;
      if (index >= static_cast<int>(shop.goods.size())) {
        continue;
      }
      const RectI row_rect{rect.x + 27, rect.y + 28 + row * 28, 244, 25};
      if (index == *selected_index) {
        renderer.fill_rect(row_rect, 0x66475569U);
      }
      const auto& item = shop.goods[static_cast<std::size_t>(index)];
      if (item.looks > 0) {
        draw_sprite(renderer, assets->get_frame(ArchiveId::items, item.looks),
                    row_rect.x + 2, row_rect.y - 4);
      }
      draw_legacy_text(renderer, row_rect.x + 34, row_rect.y + 4, widen(item.name),
                       0xFFFFFF66U);
      draw_legacy_text(renderer, row_rect.x + 162, row_rect.y + 4,
                       std::to_wstring(item.price), 0xFFFFE08AU);
    }
    ui::UiNode::paint(renderer);
  }

  GameStateStore* state{nullptr};
  AssetManager* assets{nullptr};
  int* selected_index{nullptr};
};

class MerchantSellNode final : public ui::UiNode {
 public:
  explicit MerchantSellNode(const RectI bounds) : ui::UiNode(bounds) {
    enabled = false;
  }

  void paint(SoftwareRenderer& renderer) override {
    if (state == nullptr || assets == nullptr) {
      ui::UiNode::paint(renderer);
      return;
    }
    const auto rect = resolved_bounds();
    const auto& shop = state->world.merchant_shop;
    const auto* item = pending_item();
    if (item != nullptr) {
      draw_bag_item_icon(renderer, item_icon_frame(assets, *item, ArchiveId::items),
                         RectI{rect.x + 27, rect.y + 67, 61, 52},
                         item_low_dura(*item));
      draw_legacy_text(renderer, rect.x + 101, rect.y + 73, widen(item->name),
                       0xFFFFFF66U);
    }
    draw_legacy_text(renderer, rect.x + 101, rect.y + 96,
                     L"Price " + std::to_wstring(shop.pending_sell_price), 0xFFFFE08AU);
    ui::UiNode::paint(renderer);
  }

  GameStateStore* state{nullptr};
  AssetManager* assets{nullptr};

 private:
  [[nodiscard]] const client_v1::ItemState* pending_item() const {
    const auto& shop = state->world.merchant_shop;
    if (shop.pending_sell_make_index == 0) {
      return nullptr;
    }
    for (const auto& item : state->world.bag_items) {
      if (!item_empty(item) && item.make_index == shop.pending_sell_make_index) {
        return &item;
      }
    }
    return nullptr;
  }
};

class RepairDialogNode final : public ui::UiNode {
 public:
  explicit RepairDialogNode(const RectI bounds) : ui::UiNode(bounds) {
    enabled = false;
  }

  void paint(SoftwareRenderer& renderer) override {
    if (state == nullptr || assets == nullptr) {
      ui::UiNode::paint(renderer);
      return;
    }
    const auto rect = resolved_bounds();
    const auto& repair = state->world.repair;
    const auto* item = pending_item();
    if (item != nullptr) {
      draw_bag_item_icon(renderer, item_icon_frame(assets, *item, ArchiveId::items),
                         RectI{rect.x + 27, rect.y + 67, 61, 52},
                         item_low_dura(*item));
      draw_legacy_text(renderer, rect.x + 101, rect.y + 73, widen(item->name),
                       0xFFFFFF66U);
    } else if (!repair.pending_name.empty()) {
      draw_legacy_text(renderer, rect.x + 101, rect.y + 73, widen(repair.pending_name),
                       0xFFFFFF66U);
    }
    draw_legacy_text(renderer, rect.x + 101, rect.y + 96,
                     L"Repair " + std::to_wstring(repair.pending_price), 0xFFFFE08AU);
    ui::UiNode::paint(renderer);
  }

  GameStateStore* state{nullptr};
  AssetManager* assets{nullptr};

 private:
  [[nodiscard]] const client_v1::ItemState* pending_item() const {
    const auto& repair = state->world.repair;
    if (repair.pending_make_index == 0) {
      return nullptr;
    }
    for (const auto& item : state->world.bag_items) {
      if (!item_empty(item) && item.make_index == repair.pending_make_index) {
        return &item;
      }
    }
    return nullptr;
  }
};

class StorageListNode final : public ui::UiNode {
 public:
  explicit StorageListNode(const RectI bounds) : ui::UiNode(bounds) {
    enabled = false;
  }

  void paint(SoftwareRenderer& renderer) override {
    if (state == nullptr || assets == nullptr) {
      ui::UiNode::paint(renderer);
      return;
    }
    const auto rect = resolved_bounds();
    const auto& storage = state->world.storage;
    draw_legacy_text(renderer, rect.x + 26, rect.y + 6, L"Storage", 0xFFFFFF66U);
    for (int row = 0; row < 5; ++row) {
      const auto index = storage_page * 5 + row;
      if (index >= static_cast<int>(storage.items.size())) {
        continue;
      }
      const RectI row_rect{rect.x + 27, rect.y + 28 + row * 28, 244, 25};
      if (index == storage.selected_index) {
        renderer.fill_rect(row_rect, 0x66475569U);
      }
      const auto& item = storage.items[static_cast<std::size_t>(index)];
      if (item.looks > 0) {
        draw_sprite(renderer, assets->get_frame(ArchiveId::items, item.looks),
                    row_rect.x + 2, row_rect.y - 4);
      }
      draw_legacy_text(renderer, row_rect.x + 34, row_rect.y + 4, widen(item.name),
                       0xFFFFFF66U);
    }
    ui::UiNode::paint(renderer);
  }

  GameStateStore* state{nullptr};
  AssetManager* assets{nullptr};
  int storage_page{0};
};

class GroupPanelNode final : public ui::UiNode {
 public:
  explicit GroupPanelNode(const RectI bounds) : ui::UiNode(bounds) {}

  void paint(SoftwareRenderer& renderer) override {
    const auto rect = resolved_bounds();
    renderer.fill_rect(rect, 0xE010172AU);
    renderer.stroke_rect(rect, 0xFFCBD5E1U);
    if (state == nullptr) {
      ui::UiNode::paint(renderer);
      return;
    }
    const auto& group = state->world.group;
    draw_legacy_text(renderer, rect.x + 12, rect.y + 10, L"Group", 0xFFFFFF66U);
    draw_legacy_text(renderer, rect.x + 12, rect.y + 34,
                     group.allow_group ? L"Allow invite: Yes" : L"Allow invite: No",
                     0xFFE5E7EBU);
    for (std::size_t index = 0; index < std::min<std::size_t>(group.members.size(), 6);
         ++index) {
      draw_legacy_text(renderer, rect.x + 18, rect.y + 62 + static_cast<int>(index) * 18,
                       widen(group.members[index]), 0xFFFFFF66U);
    }
    ui::UiNode::paint(renderer);
  }

  GameStateStore* state{nullptr};
};

class TradePanelNode final : public ui::UiNode {
 public:
  explicit TradePanelNode(const RectI bounds) : ui::UiNode(bounds) {}

  void paint(SoftwareRenderer& renderer) override {
    const auto rect = resolved_bounds();
    renderer.fill_rect(rect, 0xE010172AU);
    renderer.stroke_rect(rect, 0xFFCBD5E1U);
    if (state == nullptr || assets == nullptr) {
      ui::UiNode::paint(renderer);
      return;
    }
    const auto& trade = state->world.trade;
    draw_legacy_text(renderer, rect.x + 12, rect.y + 10,
                     trade.remote_name.empty() ? L"Trade" : L"Trade: " + widen(trade.remote_name),
                     0xFFFFFF66U);
    draw_legacy_text(renderer, rect.x + 22, rect.y + 34, L"Mine", 0xFFFFE08AU);
    draw_legacy_text(renderer, rect.x + 162, rect.y + 34, L"Theirs", 0xFFFFE08AU);
    draw_items(renderer, trade.local_items, rect.x + 20, rect.y + 56);
    draw_items(renderer, trade.remote_items, rect.x + 160, rect.y + 56);
    draw_legacy_text(renderer, rect.x + 20, rect.y + 154,
                     L"Gold " + std::to_wstring(trade.local_gold), 0xFFE5E7EBU);
    draw_legacy_text(renderer, rect.x + 160, rect.y + 154,
                     L"Gold " + std::to_wstring(trade.remote_gold), 0xFFE5E7EBU);
    draw_legacy_text(renderer, rect.x + 20, rect.y + 174,
                     trade.local_accept ? L"Accepted" : L"Waiting", 0xFFE5E7EBU);
    draw_legacy_text(renderer, rect.x + 160, rect.y + 174,
                     trade.remote_accept ? L"Accepted" : L"Waiting", 0xFFE5E7EBU);
    ui::UiNode::paint(renderer);
  }

  GameStateStore* state{nullptr};
  AssetManager* assets{nullptr};

 private:
  void draw_items(SoftwareRenderer& renderer, const std::vector<client_v1::ItemSlotState>& items,
                  const int x, const int y) const {
    for (int slot = 0; slot < 8; ++slot) {
      const RectI cell{x + (slot % 4) * 31, y + (slot / 4) * 31, 29, 29};
      renderer.stroke_rect(cell, 0x66555F70U);
    }
    for (const auto& entry : items) {
      if (entry.slot < 0 || entry.slot >= 8 || item_empty(entry.item)) {
        continue;
      }
      const RectI cell{x + (entry.slot % 4) * 31, y + (entry.slot / 4) * 31, 29, 29};
      draw_bag_item_icon(renderer, item_icon_frame(assets, entry.item, ArchiveId::items), cell,
                         item_low_dura(entry.item));
    }
  }
};

class GuildPanelNode final : public ui::UiNode {
 public:
  explicit GuildPanelNode(const RectI bounds) : ui::UiNode(bounds) {}

  void paint(SoftwareRenderer& renderer) override {
    const auto rect = resolved_bounds();
    renderer.fill_rect(rect, 0xE010172AU);
    renderer.stroke_rect(rect, 0xFFCBD5E1U);
    if (state == nullptr) {
      ui::UiNode::paint(renderer);
      return;
    }
    const auto& guild = state->world.guild;
    draw_legacy_text(renderer, rect.x + 12, rect.y + 10,
                     guild.guild_name.empty() ? L"Guild" : widen(guild.guild_name),
                     0xFFFFFF66U);
    if (!guild.rank_name.empty()) {
      draw_legacy_text(renderer, rect.x + 12, rect.y + 34, widen(guild.rank_name),
                       0xFFE5E7EBU);
    }
    if (!guild.notice.empty()) {
      draw_legacy_text(renderer, rect.x + 12, rect.y + 58, widen(guild.notice),
                       0xFFE5E7EBU);
    }
    draw_legacy_text(renderer, rect.x + 12, rect.y + 92, L"Members", 0xFFFFE08AU);
    for (std::size_t index = 0; index < std::min<std::size_t>(guild.members.size(), 8);
         ++index) {
      const auto& member = guild.members[index];
      draw_legacy_text(renderer, rect.x + 18, rect.y + 116 + static_cast<int>(index) * 18,
                       widen(member.name + " " + member.rank), 0xFFFFFF66U);
    }
    ui::UiNode::paint(renderer);
  }

  GameStateStore* state{nullptr};
};

class LegacyMiniMapNode final : public ui::UiNode {
 public:
  explicit LegacyMiniMapNode(const RectI bounds) : ui::UiNode(bounds) {}

  void paint(SoftwareRenderer& renderer) override {
    if (state == nullptr) {
      ui::UiNode::paint(renderer);
      return;
    }
    const auto rect = resolved_bounds();
    renderer.fill_rect(rect, 0xD80B1220U);
    renderer.stroke_rect(rect, 0xFFCBD5E1U);
    draw_legacy_text(renderer, rect.x + 6, rect.y + 3, widen(state->world.map_id),
                     0xFFFFFF66U);
    const auto& minimap = state->world.minimap;
    const RectI map_rect{rect.x + 3, rect.y + 16, 160, 120};
    renderer.fill_rect(map_rect, 0xFF0F172AU);
    if (minimap.loaded && minimap.width > 0 && minimap.height > 0 &&
        minimap.pixels.size() >=
            static_cast<std::size_t>(minimap.width) * minimap.height) {
      for (int y = 0; y < std::min<int>(120, minimap.height); ++y) {
        for (int x = 0; x < std::min<int>(160, minimap.width); ++x) {
          const auto value =
              minimap.pixels[static_cast<std::size_t>(y) * minimap.width + x];
          renderer.fill_rect(RectI{map_rect.x + x, map_rect.y + y, 1, 1},
                             value != 0 ? 0xFF475569U : 0xFF111827U);
        }
      }
    } else if (!minimap.error_message.empty()) {
      draw_legacy_text(renderer, map_rect.x + 10, map_rect.y + 50,
                       widen(minimap.error_message), 0xFFFFAAAAU);
    }

    if (const auto it = state->world.actors.find(state->world.self_actor_id);
        it != state->world.actors.end() && state->world.width > 0 && state->world.height > 0) {
      const auto x = std::clamp((it->second.x * 160) / state->world.width, 0, 159);
      const auto y = std::clamp((it->second.y * 120) / state->world.height, 0, 119);
      renderer.fill_rect(RectI{map_rect.x + x - 1, map_rect.y + y - 1, 3, 3}, 0xFFFF3333U);
    }
    ui::UiNode::paint(renderer);
  }

  GameStateStore* state{nullptr};
};

/// 旧版 HUD：兼容经典 Delph 客户端的游戏内界面
/// 管理装备栏、背包网格、底部操作栏、工具提示和拖拽覆盖层
/// 支持从背包/装备栏点击、拖拽和双击操作
class LegacyHud final {
 public:
  /// 初始化 HUD：创建 UI 树节点并建立事件绑定
  void initialize(ClientContext& context, ui::UiTree& tree) {
    reset();
    tree_ = &tree;
    state_ = context.state;
    assets_ = context.assets;
    audio_ = context.audio;
    tree.set_asset_manager(context.assets);
    auto* root = tree.set_root<ui::UiNode>(RectI{0, 0, 800, 600});
    root->background = true;

    item_bag_ = add_sprite_window(root, context, ArchiveId::prguse, kItemBagDialogIndex, 0, 0,
                                  329, 227);
    item_bag_->visible = false;
    item_grid_ = item_bag_->emplace_child<ui::Grid>(RectI{20, 13, 286, 162});
    item_grid_->col_count = kBagGridColumns;
    item_grid_->row_count = kBagGridRows;
    item_grid_->col_width = kBagCellWidth;
    item_grid_->row_height = kBagCellHeight;
    item_grid_->on_cell_select = [this](ui::Grid&, const int col, const int row) {
      pending_bag_click_slot_ = col + row * kBagGridColumns + kBagGridFirstSlot;
    };
    item_grid_->on_cell_hover = [this](ui::Grid&, const int col, const int row) {
      hovered_bag_slot_ = col + row * kBagGridColumns + kBagGridFirstSlot;
      if (state_ != nullptr) {
        state_->world.hovered_bag_slot = hovered_bag_slot_;
      }
    };
    item_grid_->on_cell_double_click = [this](ui::Grid&, const int col, const int row) {
      pending_bag_double_click_slot_ = col + row * kBagGridColumns + kBagGridFirstSlot;
    };
    item_grid_->on_cell_paint = [this](ui::Grid&, const int col, const int row,
                                       const RectI& rect, const bool selected,
                                       SoftwareRenderer& renderer) {
      const auto slot = col + row * kBagGridColumns + kBagGridFirstSlot;
      if (state_ != nullptr && valid_bag_slot(slot)) {
        const auto& item = state_->world.bag_items[static_cast<std::size_t>(slot)];
        if (!item_empty(item)) {
          draw_bag_item_icon(renderer, item_icon_frame(assets_, item, ArchiveId::items), rect,
                             item_low_dura(item));
          if (const auto count = item_stack_count(item); count > 0) {
            draw_legacy_text(renderer, rect.x + 22, rect.y + 20,
                             std::to_wstring(count));
          }
        }
      }
      if (selected) {
        renderer.stroke_rect(rect, 0x99FACC15U);
      }
    };
    auto* bag_close =
        add_sprite_button(item_bag_, context, ArchiveId::prguse, kBagCloseButtonIndex, 309, 203,
                          14, 20);
    bind_audio_click(bag_close, context.audio, LegacyClickSound::normal, [this] {
      if (item_bag_ != nullptr && tree_ != nullptr) {
        item_bag_->hide(*tree_);
      }
    });
    auto* repair_button =
        add_sprite_button(item_bag_, context, ArchiveId::prguse, kBagRepairButtonIndex, 242,
                          203, 30, 20);
    bind_audio_click(repair_button, context.audio, LegacyClickSound::normal,
                     [this, app = context.app] { open_repair_selecting(app); });
    auto* gold_button =
        add_sprite_button(item_bag_, context, ArchiveId::prguse, kBagGoldButtonIndex, 274, 203,
                          30, 20);
    bind_audio_click(gold_button, context.audio, LegacyClickSound::normal,
                     [app = context.app] {
      if (app != nullptr) {
        app->show_info_modal(L"Gold", L"Gold details are not available in this migration phase.");
      }
    });

    const auto state_frame = get_frame(context, ArchiveId::prguse, kStateDialogIndex);
    const auto state_width = state_frame != nullptr ? state_frame->width : 252;
    state_window_ =
        add_sprite_window(root, context, ArchiveId::prguse, kStateDialogIndex,
                          800 - state_width, 0, 252, 308);
    state_window_->visible = false;
    state_content_ = state_window_->emplace_child<LegacyStateContentNode>(
        RectI{0, 0, state_width, 308});
    state_content_->state = state_;
    state_content_->assets = assets_;
    state_content_->state_page = &state_page_;
    state_content_->magic_page = &magic_page_;
    auto* state_close =
        add_sprite_button(state_window_, context, ArchiveId::prguse, kStateCloseButtonIndex, 8,
                          39, 14, 20);
    bind_audio_click(state_close, context.audio, LegacyClickSound::glass, [this] {
      if (state_window_ != nullptr && tree_ != nullptr) {
        state_window_->hide(*tree_);
      }
    });
    state_prev_button_ =
        add_sprite_button(state_window_, context, ArchiveId::prguse, kStatePrevButtonIndex, 7,
                          127, 22, 24);
    bind_audio_click(state_prev_button_, context.audio, LegacyClickSound::stone,
                     [this] { change_state_page(-1); });
    state_next_button_ =
        add_sprite_button(state_window_, context, ArchiveId::prguse, kStateNextButtonIndex, 7,
                          187, 22, 24);
    bind_audio_click(state_next_button_, context.audio, LegacyClickSound::stone,
                     [this] { change_state_page(1); });
    magic_up_button_ =
        add_sprite_button(state_window_, context, ArchiveId::prguse, kStateMagicPageUpIndex, 211,
                          112, 22, 24);
    bind_audio_click(magic_up_button_, context.audio, LegacyClickSound::stone,
                     [this] { change_magic_page(-1); });
    magic_down_button_ =
        add_sprite_button(state_window_, context, ArchiveId::prguse, kStateMagicPageDownIndex,
                          211, 143, 22, 24);
    bind_audio_click(magic_down_button_, context.audio, LegacyClickSound::stone,
                     [this] { change_magic_page(1); });
    for (int row = 0; row < 5; ++row) {
      auto* row_button = add_hotspot_button(state_window_, RectI{38, 55 + row * 37, 168, 35});
      row_button->draw_fallback = false;
      bind_audio_click(row_button, context.audio, LegacyClickSound::stone,
                       [this, row] { select_magic_row(row); });
      magic_row_buttons_[static_cast<std::size_t>(row)] = row_button;
    }
    add_equipment_button(kEquipWeapon, RectI{47, 80, 47, 87});
    add_equipment_button(kEquipDress, RectI{96, 122, 53, 112});
    add_equipment_button(kEquipHelmet, RectI{115, 93, 18, 18});
    add_equipment_button(kEquipNecklace, RectI{168, 87, 34, 31});
    add_equipment_button(kEquipRightHand, RectI{168, 125, 34, 31});
    add_equipment_button(kEquipArmRingRight, RectI{42, 176, 34, 31});
    add_equipment_button(kEquipArmRingLeft, RectI{168, 176, 34, 31});
    add_equipment_button(kEquipRingRight, RectI{42, 215, 34, 31});
    add_equipment_button(kEquipRingLeft, RectI{168, 215, 34, 31});

    key_select_dialog_ =
        add_sprite_window(root, context, ArchiveId::prguse, kMagicKeyDialogIndex, 289, 185,
                          222, 132);
    key_select_dialog_->visible = false;
    add_magic_key_button(context, 0, kMagicKeyNoneButtonIndex, 15, 42);
    for (int key = 1; key <= 8; ++key) {
      const auto row = (key - 1) / 4;
      const auto col = (key - 1) % 4;
      add_magic_key_button(context, key, 230 + key * 2, 58 + col * 38, 42 + row * 28);
    }
    auto* key_ok = add_sprite_button(key_select_dialog_, context, ArchiveId::prguse,
                                     kMagicKeyOkButtonIndex, 78, 103, 70, 24);
    key_ok->on_click = [this] {
      if (key_select_dialog_ != nullptr && tree_ != nullptr) {
        key_select_dialog_->hide(*tree_);
      }
    };

    const auto bottom_frame = get_frame(context, ArchiveId::prguse, kBottomBoardIndex);
    const auto bottom_height = bottom_frame != nullptr ? bottom_frame->height : 132;
    bottom_ = add_sprite_window(root, context, ArchiveId::prguse, kBottomBoardIndex, 0,
                                600 - bottom_height, 800, 132);
    bottom_->real_hit_test_enabled = true;
    bottom_->fallback_fill_color = 0x88202A36U;

    bottom_status_ = bottom_->emplace_child<LegacyBottomStatusNode>(
        RectI{0, 0, 800, bottom_height});
    bottom_status_->state = state_;
    bottom_status_->assets = assets_;

    for (int slot = 0; slot < 6; ++slot) {
      auto* belt_button = add_hotspot_button(
          bottom_, RectI{kBeltButtonX[static_cast<std::size_t>(slot)], 59, 32, 29});
      belt_button->draw_fallback = false;
      belt_button->real_hit_test_enabled = false;
      bind_audio_click(belt_button, context.audio, LegacyClickSound::glass, [this, slot] {
        const auto now = GetTickCount64();
        auto& last = belt_last_click_ms_[static_cast<std::size_t>(slot)];
        if (last != 0 && now - last <= 350U) {
          pending_bag_double_click_slot_ = slot;
          pending_bag_click_slot_ = -1;
        } else {
          pending_bag_click_slot_ = slot;
        }
        last = now;
      });
      belt_buttons_[static_cast<std::size_t>(slot)] = belt_button;
    }

    auto* mini_map_button =
        add_sprite_button(bottom_, context, ArchiveId::prguse, kBottomMiniMapButtonIndex, 219,
                          104, 28, 18);
    bind_audio_click(mini_map_button, context.audio, LegacyClickSound::normal,
                     [this, app = context.app] {
      request_minimap(app);
    });
    auto* trade_button =
        add_sprite_button(bottom_, context, ArchiveId::prguse, kBottomTradeButtonIndex, 249, 104,
                          28, 18);
    bind_audio_click(trade_button, context.audio, LegacyClickSound::normal,
                     [this, app = context.app] { open_trade(app); });
    auto* guild_button =
        add_sprite_button(bottom_, context, ArchiveId::prguse, kBottomGuildButtonIndex, 279, 104,
                          28, 18);
    bind_audio_click(guild_button, context.audio, LegacyClickSound::normal,
                     [this, app = context.app] { open_guild(app); });
    auto* group_button =
        add_sprite_button(bottom_, context, ArchiveId::prguse, kBottomGroupButtonIndex, 309, 104,
                          28, 18);
    bind_audio_click(group_button, context.audio, LegacyClickSound::normal,
                     [this, app = context.app] { open_group(app); });
    auto* plus_button =
        add_sprite_button(bottom_, context, ArchiveId::prguse, kBottomPlusAbilityButtonIndex, 339,
                          104, 28, 18);
    bind_audio_click(plus_button, context.audio, LegacyClickSound::normal,
                     [app = context.app] {
      if (app != nullptr) {
        app->show_info_modal(L"Ability", L"Bonus ability allocation is planned for a later phase.");
      }
    });
    auto* logout_button =
        add_sprite_button(bottom_, context, ArchiveId::prguse, kBottomLogoutButtonIndex, 530, 104,
                          28, 18);
    bind_audio_click(logout_button, context.audio, LegacyClickSound::normal,
                     [app = context.app] {
      if (app != nullptr) {
        app->show_info_modal(L"Logout", L"Logout flow is planned for a later migration phase.");
      }
    });
    auto* exit_button =
        add_sprite_button(bottom_, context, ArchiveId::prguse, kBottomExitButtonIndex, 560, 104,
                          28, 18);
    bind_audio_click(exit_button, context.audio, LegacyClickSound::normal,
                     [app = context.app] {
      if (app != nullptr) {
        app->show_info_modal(L"Exit", L"Exit flow is planned for a later migration phase.");
      }
    });

    auto* state_button =
        add_sprite_button(bottom_, context, ArchiveId::prguse, kBottomStateButtonIndex, 643, 61,
                          38, 38);
    bind_audio_click(state_button, context.audio, LegacyClickSound::normal, [this] {
      if (tree_ != nullptr) {
        toggle_state(*tree_);
      }
    });
    auto* bag_button =
        add_sprite_button(bottom_, context, ArchiveId::prguse, kBottomBagButtonIndex, 682, 41,
                          38, 38);
    bind_audio_click(bag_button, context.audio, LegacyClickSound::glass, [this] {
      if (tree_ != nullptr) {
        toggle_bag(*tree_);
      }
    });
    auto* magic_button =
        add_sprite_button(bottom_, context, ArchiveId::prguse, kBottomMagicButtonIndex, 722, 21,
                          38, 38);
    bind_audio_click(magic_button, context.audio, LegacyClickSound::glass, [this] {
      if (tree_ != nullptr) {
        open_state_page(*tree_, 3);
      }
    });
    auto* option_button =
        add_sprite_button(bottom_, context, ArchiveId::prguse, kBottomOptionButtonIndex, 764, 11,
                          36, 36);
    bind_audio_click(option_button, context.audio, LegacyClickSound::glass,
                     [app = context.app] {
      if (app != nullptr) {
        app->show_info_modal(L"Options", L"Options UI is planned for a later migration phase.");
      }
    });

    chat_board_ = root->emplace_child<ChatBoardNode>(
        RectI{kChatBoardX, kChatBoardY, kChatBoardWidth,
              kChatBoardLineHeight * kChatBoardVisibleLines});
    chat_board_->state = state_;
    chat_board_->on_whisper = [this](const std::string& line) { open_chat_with_whisper(line); };

    chat_edit_ = root->emplace_child<ResourceTextEdit>(RectI{208, 581, 387, 12});
    chat_edit_->visible = false;
    chat_edit_->on_submit = [this] { submit_chat(); };
    chat_edit_->on_cancel = [this] { close_chat(true); };
    create_chat_edit_font(context);
    if (context.app != nullptr && context.app->window_handle() != nullptr &&
        chat_edit_font_ != nullptr) {
      chat_edit_->attach_native(context.app->window_handle(), chat_edit_font_, 70, false, false,
                                false);
      chat_edit_->set_native_visible(false);
    }

    npc_dialog_ = root->emplace_child<NpcDialogNode>(
        sprite_rect(get_frame(context, ArchiveId::prguse, kMerchantDialogIndex), 0, 0, 420, 180));
    npc_dialog_->background_sprite = ui::LegacySpriteRef{ArchiveId::prguse, kMerchantDialogIndex};
    npc_dialog_->background_frame = get_frame(context, ArchiveId::prguse, kMerchantDialogIndex);
    npc_dialog_->face = npc_dialog_->background_sprite;
    npc_dialog_->hit_frame = npc_dialog_->background_frame;
    npc_dialog_->real_hit_test_enabled = npc_dialog_->hit_frame != nullptr &&
                                         !npc_dialog_->hit_frame->empty();
    npc_dialog_->visible = false;
    npc_dialog_->on_select = [this](const std::uint64_t merchant_id, std::string selection) {
      pending_npc_select_merchant_id_ = merchant_id;
      pending_npc_select_ = std::move(selection);
    };
    auto* npc_close = add_sprite_button(npc_dialog_, context, ArchiveId::prguse,
                                        kMerchantCloseButtonIndex, 399, 1, 16, 16);
    bind_audio_click(npc_close, context.audio, LegacyClickSound::normal,
                     [this] { close_npc_dialog_local(); });

    merchant_menu_ =
        add_sprite_window(root, context, ArchiveId::prguse, kMerchantBuyDialogIndex, 138, 163,
                          320, 210);
    merchant_menu_->visible = false;
    merchant_goods_content_ = merchant_menu_->emplace_child<MerchantGoodsNode>(
        RectI{0, 0, merchant_menu_->bounds.w, merchant_menu_->bounds.h});
    merchant_goods_content_->state = state_;
    merchant_goods_content_->assets = assets_;
    merchant_goods_content_->selected_index = &merchant_selected_index_;
    for (int row = 0; row < 5; ++row) {
      auto* row_button = add_hotspot_button(merchant_menu_, RectI{27, 28 + row * 28, 244, 25});
      row_button->draw_fallback = false;
      bind_audio_click(row_button, context.audio, LegacyClickSound::normal,
                       [this, row] { select_merchant_row(row); });
      merchant_row_buttons_[static_cast<std::size_t>(row)] = row_button;
    }
    auto* merchant_prev =
        add_sprite_button(merchant_menu_, context, ArchiveId::prguse, kMerchantPrevButtonIndex,
                          43, 175, 40, 24);
    bind_audio_click(merchant_prev, context.audio, LegacyClickSound::glass,
                     [this] { change_merchant_page(-1); });
    auto* merchant_next =
        add_sprite_button(merchant_menu_, context, ArchiveId::prguse, kMerchantNextButtonIndex,
                          90, 175, 40, 24);
    bind_audio_click(merchant_next, context.audio, LegacyClickSound::glass,
                     [this] { change_merchant_page(1); });
    auto* merchant_buy =
        add_sprite_button(merchant_menu_, context, ArchiveId::prguse, kMerchantBuyButtonIndex,
                          215, 171, 50, 28);
    bind_audio_click(merchant_buy, context.audio, LegacyClickSound::glass,
                     [this, app = context.app] { buy_selected_merchant_item(app); });
    auto* merchant_close =
        add_sprite_button(merchant_menu_, context, ArchiveId::prguse, kMerchantCloseButtonIndex,
                          291, 0, 16, 16);
    bind_audio_click(merchant_close, context.audio, LegacyClickSound::normal,
                     [this] { close_merchant_menu(); });

    merchant_sell_dialog_ =
        add_sprite_window(root, context, ArchiveId::prguse, kMerchantSellDialogIndex, 328, 163,
                          250, 160);
    merchant_sell_dialog_->visible = false;
    merchant_sell_content_ = merchant_sell_dialog_->emplace_child<MerchantSellNode>(
        RectI{0, 0, merchant_sell_dialog_->bounds.w, merchant_sell_dialog_->bounds.h});
    merchant_sell_content_->state = state_;
    merchant_sell_content_->assets = assets_;
    auto* sell_ok = add_sprite_button(merchant_sell_dialog_, context, ArchiveId::prguse,
                                      kMerchantSellOkButtonIndex, 102, 124, 60, 24);
    bind_audio_click(sell_ok, context.audio, LegacyClickSound::normal,
                     [this, app = context.app] { confirm_sell_item(app); });
    auto* sell_close = add_sprite_button(merchant_sell_dialog_, context, ArchiveId::prguse,
                                         kMerchantCloseButtonIndex, 225, 2, 16, 16);
    bind_audio_click(sell_close, context.audio, LegacyClickSound::normal,
                     [this] { clear_sell_dialog(); });

    storage_window_ =
        add_sprite_window(root, context, ArchiveId::prguse, kMerchantBuyDialogIndex, 138, 163,
                          320, 210);
    storage_window_->visible = false;
    storage_content_ = storage_window_->emplace_child<StorageListNode>(
        RectI{0, 0, storage_window_->bounds.w, storage_window_->bounds.h});
    storage_content_->state = state_;
    storage_content_->assets = assets_;
    for (int row = 0; row < 5; ++row) {
      auto* row_button = add_hotspot_button(storage_window_, RectI{27, 28 + row * 28, 244, 25});
      row_button->draw_fallback = false;
      bind_audio_click(row_button, context.audio, LegacyClickSound::normal,
                       [this, row] { select_storage_row(row); });
      storage_row_buttons_[static_cast<std::size_t>(row)] = row_button;
    }
    auto* storage_prev =
        add_sprite_button(storage_window_, context, ArchiveId::prguse, kMerchantPrevButtonIndex,
                          43, 175, 40, 24);
    bind_audio_click(storage_prev, context.audio, LegacyClickSound::glass,
                     [this] { change_storage_page(-1); });
    auto* storage_next =
        add_sprite_button(storage_window_, context, ArchiveId::prguse, kMerchantNextButtonIndex,
                          90, 175, 40, 24);
    bind_audio_click(storage_next, context.audio, LegacyClickSound::glass,
                     [this] { change_storage_page(1); });
    auto* storage_take =
        add_sprite_button(storage_window_, context, ArchiveId::prguse, kMerchantBuyButtonIndex,
                          174, 171, 50, 28);
    bind_audio_click(storage_take, context.audio, LegacyClickSound::glass,
                     [this, app = context.app] { withdraw_selected_storage_item(app); });
    auto* storage_deposit =
        add_sprite_button(storage_window_, context, ArchiveId::prguse, kMerchantSellOkButtonIndex,
                          224, 171, 50, 28);
    bind_audio_click(storage_deposit, context.audio, LegacyClickSound::glass,
                     [this] { open_storage_deposit_selecting(); });
    auto* storage_close =
        add_sprite_button(storage_window_, context, ArchiveId::prguse, kMerchantCloseButtonIndex,
                          291, 0, 16, 16);
    bind_audio_click(storage_close, context.audio, LegacyClickSound::normal,
                     [this] { close_storage_window(); });

    repair_dialog_ =
        add_sprite_window(root, context, ArchiveId::prguse, kMerchantSellDialogIndex, 328, 163,
                          250, 160);
    repair_dialog_->visible = false;
    repair_content_ = repair_dialog_->emplace_child<RepairDialogNode>(
        RectI{0, 0, repair_dialog_->bounds.w, repair_dialog_->bounds.h});
    repair_content_->state = state_;
    repair_content_->assets = assets_;
    auto* repair_ok = add_sprite_button(repair_dialog_, context, ArchiveId::prguse,
                                        kMerchantSellOkButtonIndex, 102, 124, 60, 24);
    bind_audio_click(repair_ok, context.audio, LegacyClickSound::normal,
                     [this, app = context.app] { confirm_repair_item(app); });
    auto* repair_close = add_sprite_button(repair_dialog_, context, ArchiveId::prguse,
                                           kMerchantCloseButtonIndex, 225, 2, 16, 16);
    bind_audio_click(repair_close, context.audio, LegacyClickSound::normal,
                     [this] { clear_repair_dialog(); });

    auto make_text_button = [this, audio = context.audio](ui::UiNode* parent, const RectI rect,
                                                          std::wstring label,
                                                          std::function<void()> callback) {
      auto* button = add_hotspot_button(parent, rect);
      button->on_custom_paint = [label = std::move(label)](SoftwareRenderer& renderer,
                                                           const RectI& bounds) {
        renderer.fill_rect(bounds, 0xAA1E293BU);
        renderer.stroke_rect(bounds, 0xFF64748BU);
        draw_legacy_text(renderer, bounds.x + 6, bounds.y + 4, label, 0xFFF8FAFCU);
      };
      bind_audio_click(button, audio, LegacyClickSound::normal, std::move(callback));
      return button;
    };

    group_window_ = root->emplace_child<ui::Window>(RectI{270, 170, 250, 190});
    group_window_->visible = false;
    group_window_->floating = true;
    group_content_ =
        group_window_->emplace_child<GroupPanelNode>(RectI{0, 0, 250, 190});
    group_content_->state = state_;
    make_text_button(group_window_, RectI{178, 8, 54, 22}, L"Close",
                     [this] { close_group_window(); });
    make_text_button(group_window_, RectI{18, 140, 58, 24}, L"Allow",
                     [this, app = context.app] { toggle_group_mode(app); });
    make_text_button(group_window_, RectI{82, 140, 58, 24}, L"Create",
                     [this, app = context.app] { request_group_create(app); });
    make_text_button(group_window_, RectI{146, 140, 42, 24}, L"Add",
                     [this, app = context.app] { request_group_add(app); });
    make_text_button(group_window_, RectI{194, 140, 42, 24}, L"Del",
                     [this, app = context.app] { request_group_remove(app); });

    trade_window_ = root->emplace_child<ui::Window>(RectI{460, 40, 310, 236});
    trade_window_->visible = false;
    trade_window_->floating = true;
    trade_content_ =
        trade_window_->emplace_child<TradePanelNode>(RectI{0, 0, 310, 236});
    trade_content_->state = state_;
    trade_content_->assets = assets_;
    make_text_button(trade_window_, RectI{238, 8, 54, 22}, L"Close",
                     [this, app = context.app] { close_trade_window(app); });
    make_text_button(trade_window_, RectI{20, 202, 52, 24}, L"Add",
                     [this, app = context.app] { add_selected_bag_item_to_trade(app); });
    make_text_button(trade_window_, RectI{78, 202, 52, 24}, L"Gold",
                     [this, app = context.app] { set_trade_gold(app); });
    make_text_button(trade_window_, RectI{136, 202, 72, 24}, L"Accept",
                     [app = context.app] {
      if (app != nullptr) {
        app->request_trade_accept(client_v1::TradeAcceptRequest{});
      }
    });
    make_text_button(trade_window_, RectI{214, 202, 72, 24}, L"Cancel",
                     [this, app = context.app] { close_trade_window(app); });

    guild_window_ = root->emplace_child<ui::Window>(RectI{18, 60, 330, 300});
    guild_window_->visible = false;
    guild_window_->floating = true;
    guild_content_ =
        guild_window_->emplace_child<GuildPanelNode>(RectI{0, 0, 330, 300});
    guild_content_->state = state_;
    make_text_button(guild_window_, RectI{258, 8, 54, 22}, L"Close",
                     [this] { close_guild_window(); });
    make_text_button(guild_window_, RectI{18, 264, 62, 24}, L"Home",
                     [app = context.app] {
      if (app != nullptr) {
        app->request_guild_home(client_v1::GuildHomeRequest{});
      }
    });
    make_text_button(guild_window_, RectI{86, 264, 78, 24}, L"Members",
                     [app = context.app] {
      if (app != nullptr) {
        app->request_guild_members(client_v1::GuildMemberListRequest{});
      }
    });
    make_text_button(guild_window_, RectI{170, 264, 52, 24}, L"Add",
                     [this, app = context.app] { request_guild_add(app); });
    make_text_button(guild_window_, RectI{228, 264, 52, 24}, L"Del",
                     [this, app = context.app] { request_guild_remove(app); });

    minimap_ = root->emplace_child<LegacyMiniMapNode>(RectI{620, 24, 166, 139});
    minimap_->state = state_;
    minimap_->visible = false;
    auto* minimap_close = add_hotspot_button(minimap_, RectI{146, 2, 18, 14});
    minimap_close->on_custom_paint = [](SoftwareRenderer& renderer, const RectI& rect) {
      renderer.stroke_rect(rect, 0xFFCBD5E1U);
      renderer.draw_text(rect.x + 5, rect.y - 1, L"x", 0xFFF8FAFCU);
    };
    bind_audio_click(minimap_close, context.audio, LegacyClickSound::normal, [this] {
      if (state_ != nullptr) {
        state_->world.minimap.visible = false;
      }
      if (minimap_ != nullptr && tree_ != nullptr) {
        minimap_->set_visible(*tree_, false);
      }
    });

    tooltip_ = root->emplace_child<ui::Tooltip>(RectI{0, 0, 160, 24});
    drag_overlay_ = root->emplace_child<ui::DragSpriteOverlay>(RectI{0, 0, 0, 0});
    initialized_ = true;
  }

  void reset() {
    if (chat_edit_ != nullptr) {
      chat_edit_->detach_native();
    }
    destroy_chat_edit_font();
    initialized_ = false;
    tree_ = nullptr;
    state_ = nullptr;
    assets_ = nullptr;
    audio_ = nullptr;
    bottom_ = nullptr;
    bottom_status_ = nullptr;
    item_bag_ = nullptr;
    item_grid_ = nullptr;
    state_window_ = nullptr;
    state_content_ = nullptr;
    state_prev_button_ = nullptr;
    state_next_button_ = nullptr;
    magic_up_button_ = nullptr;
    magic_down_button_ = nullptr;
    key_select_dialog_ = nullptr;
    merchant_menu_ = nullptr;
    merchant_goods_content_ = nullptr;
    merchant_sell_dialog_ = nullptr;
    merchant_sell_content_ = nullptr;
    storage_window_ = nullptr;
    storage_content_ = nullptr;
    repair_dialog_ = nullptr;
    repair_content_ = nullptr;
    group_window_ = nullptr;
    group_content_ = nullptr;
    trade_window_ = nullptr;
    trade_content_ = nullptr;
    guild_window_ = nullptr;
    guild_content_ = nullptr;
    minimap_ = nullptr;
    belt_buttons_.fill(nullptr);
    belt_last_click_ms_.fill(0);
    equipment_buttons_.fill(nullptr);
    magic_row_buttons_.fill(nullptr);
    merchant_row_buttons_.fill(nullptr);
    storage_row_buttons_.fill(nullptr);
    chat_board_ = nullptr;
    chat_edit_ = nullptr;
    npc_dialog_ = nullptr;
    tooltip_ = nullptr;
    drag_overlay_ = nullptr;
    hovered_bag_slot_ = -1;
    hovered_belt_slot_ = -1;
    hovered_equipment_slot_ = -1;
    selected_bag_slot_ = -1;
    pending_bag_click_slot_ = -1;
    pending_bag_double_click_slot_ = -1;
    pending_equipment_click_slot_ = -1;
    context_menu_ = ItemContextMenu{};
    context_menu_window_ = nullptr;
    context_menu_pointer_consumed_ = false;
    suppress_item_click_until_left_release_ = false;
    pending_chat_send_.clear();
    pending_npc_select_.clear();
    pending_npc_select_merchant_id_ = 0;
    state_page_ = 0;
    magic_page_ = 0;
    selected_magic_id_ = 0;
    merchant_selected_index_ = -1;
    storage_page_ = 0;
    chat_password_mode_ = false;
  }

  void sync(ClientContext& context) {
    state_ = context.state;
    assets_ = context.assets;
    audio_ = context.audio;
    app_ = context.app;
    if (bottom_status_ != nullptr) {
      bottom_status_->state = state_;
      bottom_status_->assets = assets_;
    }
    if (state_content_ != nullptr) {
      state_content_->state = state_;
      state_content_->assets = assets_;
    }
    if (merchant_goods_content_ != nullptr) {
      merchant_goods_content_->state = state_;
      merchant_goods_content_->assets = assets_;
    }
    if (merchant_sell_content_ != nullptr) {
      merchant_sell_content_->state = state_;
      merchant_sell_content_->assets = assets_;
    }
    if (storage_content_ != nullptr) {
      storage_content_->state = state_;
      storage_content_->assets = assets_;
      storage_content_->storage_page = storage_page_;
    }
    if (repair_content_ != nullptr) {
      repair_content_->state = state_;
      repair_content_->assets = assets_;
    }
    if (group_content_ != nullptr) {
      group_content_->state = state_;
    }
    if (trade_content_ != nullptr) {
      trade_content_->state = state_;
      trade_content_->assets = assets_;
    }
    if (guild_content_ != nullptr) {
      guild_content_->state = state_;
    }
    if (minimap_ != nullptr) {
      minimap_->state = state_;
    }
    sync_chat(context);
    sync_npc_dialog(context);
    sync_state_window();
    sync_merchant_windows();
    sync_stage3_windows();
    sync_minimap_window();
    sync_items(context);
    refresh_overlay_layers();
  }

  void sync_items(ClientContext& context) {
    if (!initialized_ || context.state == nullptr || context.input == nullptr) {
      return;
    }
    auto& world = context.state->world;
    update_hover_state(*context.input, world);
    update_drag_overlay(*context.input, world);
    update_tooltip(*context.input, world);
    if (context.input->left_pressed && context_menu_.visible &&
        !context_menu_contains(context.input->mouse_x, context.input->mouse_y)) {
      hide_context_menu(*tree_);
      clear_pending_item_clicks();
      context_menu_pointer_consumed_ = true;
      suppress_item_click_until_left_release_ = true;
    }
    if (context.input->right_pressed && !world.moving_item.active) {
      const auto opened_menu = handle_right_click(*context.input, *tree_);
      if (!opened_menu && context_menu_.visible &&
          !context_menu_contains(context.input->mouse_x, context.input->mouse_y)) {
        hide_context_menu(*tree_);
        context_menu_pointer_consumed_ = true;
      }
    }
  }

  void bring_if_visible(ui::UiNode* node) {
    if (tree_ != nullptr && node != nullptr && node->visible) {
      tree_->bring_to_front(node);
    }
  }

  void refresh_overlay_layers() {
    if (tree_ == nullptr) {
      return;
    }
    bring_if_visible(drag_overlay_);
    bring_if_visible(tooltip_);
    bring_if_visible(key_select_dialog_);
    if (tree_->modal() != nullptr) {
      tree_->bring_to_front(tree_->modal());
    }
  }

  bool process_pending_actions(ClientContext& context) {
    if (!initialized_ || context.state == nullptr || context.input == nullptr) {
      return false;
    }
    auto& world = context.state->world;
    auto consumed = false;
    const auto context_menu_pointer_consumed =
        std::exchange(context_menu_pointer_consumed_, false);
    if (context_menu_pointer_consumed) {
      consumed = true;
    }
    if (suppress_item_click_until_left_release_) {
      clear_pending_item_clicks();
      consumed = true;
      if (!context.input->left_down) {
        suppress_item_click_until_left_release_ = false;
      }
    }
    if (process_waiting_item_timeout(context)) {
      consumed = true;
    }
    if (!pending_chat_send_.empty()) {
      auto text = std::exchange(pending_chat_send_, std::string{});
      if (context.app != nullptr) {
        context.app->request_chat_send(std::move(text));
      }
      consumed = true;
    }
    if (!pending_npc_select_.empty()) {
      auto selection = std::exchange(pending_npc_select_, std::string{});
      const auto merchant_id = std::exchange(pending_npc_select_merchant_id_, 0);
      if (context.app != nullptr) {
        context.app->request_npc_dialog_select(
            client_v1::NpcDialogSelectRequest{merchant_id, std::move(selection)});
      }
      consumed = true;
    }
    if (pending_bag_double_click_slot_ >= 0) {
      const auto slot = std::exchange(pending_bag_double_click_slot_, -1);
      if (pending_bag_click_slot_ == slot) {
        pending_bag_click_slot_ = -1;
      }
      handle_bag_double_click(context, slot);
      consumed = true;
    }
    if (pending_bag_click_slot_ >= 0) {
      const auto slot = std::exchange(pending_bag_click_slot_, -1);
      handle_bag_click(context, slot);
      consumed = true;
    }
    if (pending_equipment_click_slot_ >= 0) {
      const auto slot = std::exchange(pending_equipment_click_slot_, -1);
      handle_equipment_click(context, slot);
      consumed = true;
    }
    if (!context_menu_pointer_consumed && context.input->right_pressed &&
        world.moving_item.active && !context.ui_input.text_focus) {
      cancel_moving_item(context);
      consumed = true;
    }
    if (!context_menu_pointer_consumed && context.input->left_pressed &&
        world.moving_item.active && !context.ui_input.consumed && context.app != nullptr) {
      const auto item = world.moving_item.item;
      if (!item_empty(item) && world.moving_item.source == MovingItemSource::bag) {
        context.state->begin_pending_item_action(PendingItemActionKind::drop,
                                                 world.moving_item.source,
                                                 world.moving_item.source_slot, -1, item,
                                                 detail::monotonic_ms());
        context.app->request_drop_item(client_v1::DropItemRequest{item.make_index, item.name});
        world.moving_item = MovingItemState{};
      } else {
        restore_moving_item(world);
      }
      consumed = true;
    }
    return consumed;
  }

  bool process_waiting_item_timeout(ClientContext& context) {
    if (context.state == nullptr) {
      return false;
    }
    if (!context.state->world.pending_item_action.active) {
      return false;
    }
    if (!context.state->pending_item_action_expired(detail::monotonic_ms())) {
      return false;
    }
    context.state->restore_pending_item_action();
    if (context.app != nullptr) {
      context.app->show_info_modal(L"Item Action", L"No server item update was received.");
    }
    return true;
  }

  void cancel_moving_item(ClientContext& context) {
    if (context.state == nullptr) {
      return;
    }
    auto& world = context.state->world;
    restore_moving_item(world);
  }

  bool handle_shortcuts(ClientContext& context, ui::UiTree& tree) {
    if (!initialized_ || context.input == nullptr) {
      return false;
    }
    if (context.ui_input.text_focus || tree.modal() != nullptr) {
      return false;
    }
    if (context_menu_.visible && context.input->key_pressed[VK_ESCAPE]) {
      hide_context_menu(tree);
      return true;
    }
    if (key_select_dialog_ != nullptr && key_select_dialog_->visible) {
      if (context.input->key_pressed[VK_ESCAPE]) {
        key_select_dialog_->hide(tree);
      }
      return true;
    }
    if (state_ != nullptr && state_->world.moving_item.active &&
        context.input->key_pressed[VK_ESCAPE]) {
      cancel_moving_item(context);
      return true;
    }
    if (tree.captured() != nullptr) {
      return false;
    }
    if (context.input->key_pressed[VK_RETURN] || context.input->key_pressed[VK_SPACE]) {
      open_chat(L"");
      return true;
    }
    if (!context.input->text_input.empty()) {
      for (const auto ch : context.input->text_input) {
        if (ch == L'@' || ch == L'!') {
          open_chat(std::wstring{ch});
          return true;
        }
        if (ch == L'/') {
          if (state_ != nullptr && !state_->world.whisper_name.empty()) {
            open_chat(L"/" + widen(state_->world.whisper_name) + L" ");
          } else {
            open_chat(L"/");
          }
          return true;
        }
      }
    }
    if (context.input->key_pressed[VK_UP] || context.input->key_pressed[VK_PRIOR] ||
        context.input->key_pressed[VK_DOWN] || context.input->key_pressed[VK_NEXT]) {
      if (scroll_chat_board(*context.input)) {
        return true;
      }
    }
    if (trade_window_ != nullptr && trade_window_->visible) {
      if (context.input->key_pressed[VK_ESCAPE]) {
        close_trade_window(context.app);
      }
      return true;
    }
    if (guild_window_ != nullptr && guild_window_->visible) {
      if (context.input->key_pressed[VK_ESCAPE]) {
        close_guild_window();
      }
      return true;
    }
    if (group_window_ != nullptr && group_window_->visible) {
      if (context.input->key_pressed[VK_ESCAPE]) {
        close_group_window();
      }
      return true;
    }
    if (storage_window_ != nullptr && storage_window_->visible) {
      if (context.input->key_pressed[VK_ESCAPE]) {
        close_storage_window();
      }
      return true;
    }
    if (repair_dialog_ != nullptr && repair_dialog_->visible) {
      if (context.input->key_pressed[VK_ESCAPE]) {
        clear_repair_dialog();
      }
      return true;
    }
    if (merchant_sell_dialog_ != nullptr && merchant_sell_dialog_->visible) {
      if (context.input->key_pressed[VK_ESCAPE]) {
        clear_sell_dialog();
      }
      return true;
    }
    if (merchant_menu_ != nullptr && merchant_menu_->visible) {
      if (context.input->key_pressed[VK_ESCAPE]) {
        close_merchant_menu();
      }
      return true;
    }
    if (npc_dialog_visible()) {
      if (context.input->key_pressed[VK_ESCAPE]) {
        close_npc_dialog_local();
        return true;
      }
      return false;
    }
    if (context.input->key_pressed[VK_ESCAPE]) {
      if (minimap_ != nullptr && minimap_->visible) {
        if (state_ != nullptr) {
          state_->world.minimap.visible = false;
        }
        minimap_->set_visible(tree, false);
        return true;
      }
      if (state_window_ != nullptr && state_window_->visible) {
        state_window_->hide(tree);
        return true;
      }
      if (item_bag_ != nullptr && item_bag_->visible) {
        item_bag_->hide(tree);
        return true;
      }
    }
    if (context.input->key_pressed[VK_F9] || context.input->key_pressed['I']) {
      toggle_bag(tree);
      return true;
    }
    if (context.input->key_pressed[VK_F10] || context.input->key_pressed['C']) {
      toggle_state(tree);
      return true;
    }
    if (context.input->key_pressed[VK_F11] || context.input->key_pressed['S']) {
      open_state_page(tree, 3);
      return true;
    }
    if (context.input->key_pressed['V']) {
      request_minimap(context.app);
      return true;
    }
    return false;
  }

  [[nodiscard]] bool initialized() const { return initialized_; }
  [[nodiscard]] bool bag_visible() const { return item_bag_ != nullptr && item_bag_->visible; }
  [[nodiscard]] bool state_visible() const {
    return state_window_ != nullptr && state_window_->visible;
  }
  [[nodiscard]] bool npc_dialog_visible() const {
    return npc_dialog_ != nullptr && npc_dialog_->visible;
  }
  [[nodiscard]] bool blocks_world_input() const {
    return npc_dialog_visible() ||
           (merchant_menu_ != nullptr && merchant_menu_->visible) ||
           (merchant_sell_dialog_ != nullptr && merchant_sell_dialog_->visible) ||
           (storage_window_ != nullptr && storage_window_->visible) ||
           (repair_dialog_ != nullptr && repair_dialog_->visible) ||
           (group_window_ != nullptr && group_window_->visible) ||
           (trade_window_ != nullptr && trade_window_->visible) ||
           (guild_window_ != nullptr && guild_window_->visible) ||
           (key_select_dialog_ != nullptr && key_select_dialog_->visible);
  }
  [[nodiscard]] int equipment_slot_at(const int screen_x, const int screen_y) const {
    for (int slot = 0; slot < kEquipmentSlotCount; ++slot) {
      const auto* button = equipment_buttons_[static_cast<std::size_t>(slot)];
      if (button != nullptr && button->is_visible_in_tree() &&
          button->resolved_bounds().contains(screen_x, screen_y)) {
        return slot;
      }
    }
    return -1;
  }
  [[nodiscard]] int belt_slot_at(const int screen_x, const int screen_y) const {
    for (int slot = 0; slot < 6; ++slot) {
      const auto* button = belt_buttons_[static_cast<std::size_t>(slot)];
      if (button != nullptr && button->is_visible_in_tree() &&
          button->resolved_bounds().contains(screen_x, screen_y)) {
        return slot;
      }
    }
    return -1;
  }

 private:
  void create_chat_edit_font(ClientContext& context) {
    destroy_chat_edit_font();
    if (context.app == nullptr || context.app->window_handle() == nullptr) {
      return;
    }
    const auto dc = GetDC(context.app->window_handle());
    const auto font_height = -MulDiv(10, GetDeviceCaps(dc, LOGPIXELSY), 72);
    ReleaseDC(context.app->window_handle(), dc);
    chat_edit_font_ = CreateFontW(font_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                  L"MS Sans Serif");
  }

  void destroy_chat_edit_font() {
    if (chat_edit_font_ != nullptr) {
      DeleteObject(chat_edit_font_);
      chat_edit_font_ = nullptr;
    }
  }

  void sync_state_window() {
    if (tree_ == nullptr) {
      return;
    }
    state_page_ = std::clamp(state_page_, 0, 3);
    const auto magic_controls_visible = state_page_ == 3;
    if (magic_up_button_ != nullptr) {
      magic_up_button_->set_visible(*tree_, magic_controls_visible);
    }
    if (magic_down_button_ != nullptr) {
      magic_down_button_->set_visible(*tree_, magic_controls_visible);
    }
    for (auto* button : magic_row_buttons_) {
      if (button != nullptr) {
        button->set_visible(*tree_, magic_controls_visible);
      }
    }
    for (auto* button : equipment_buttons_) {
      if (button != nullptr) {
        button->set_visible(*tree_, state_page_ == 0);
      }
    }
  }

  void sync_merchant_windows() {
    if (state_ == nullptr || tree_ == nullptr) {
      return;
    }
    auto& shop = state_->world.merchant_shop;
    if (merchant_menu_ != nullptr) {
      const auto show = shop.visible && !shop.goods.empty();
      merchant_menu_->set_visible(*tree_, show);
      if (show) {
        if (merchant_selected_index_ < 0 ||
            merchant_selected_index_ >= static_cast<int>(shop.goods.size())) {
          merchant_selected_index_ = 0;
        }
        tree_->bring_to_front(merchant_menu_);
      } else if (!shop.visible) {
        merchant_selected_index_ = -1;
      }
    }
    if (shop.sell_selecting && item_bag_ != nullptr && !item_bag_->visible) {
      item_bag_->set_visible(*tree_, true);
      move_bag_for_npc_dialog();
      tree_->bring_to_front(item_bag_);
    }
    if (merchant_sell_dialog_ != nullptr) {
      const auto show = shop.pending_sell_make_index != 0 && shop.pending_sell_price > 0;
      merchant_sell_dialog_->set_visible(*tree_, show);
      if (show) {
        tree_->bring_to_front(merchant_sell_dialog_);
      }
    }
  }

  void sync_stage3_windows() {
    if (state_ == nullptr || tree_ == nullptr) {
      return;
    }
    auto& world = state_->world;
    if (storage_content_ != nullptr) {
      storage_content_->storage_page = storage_page_;
    }
    if (storage_window_ != nullptr) {
      storage_window_->set_visible(*tree_, world.storage.visible);
      if (world.storage.visible) {
        const auto max_page =
            std::max(0, (static_cast<int>(world.storage.items.size()) + 4) / 5 - 1);
        storage_page_ = std::clamp(storage_page_, 0, max_page);
        if (storage_content_ != nullptr) {
          storage_content_->storage_page = storage_page_;
        }
        if (world.storage.deposit_selecting && item_bag_ != nullptr && !item_bag_->visible) {
          item_bag_->set_visible(*tree_, true);
          move_bag_for_npc_dialog();
          tree_->bring_to_front(item_bag_);
        }
        tree_->bring_to_front(storage_window_);
      } else {
        storage_page_ = 0;
      }
    }
    if (world.repair.selecting && item_bag_ != nullptr && !item_bag_->visible) {
      item_bag_->set_visible(*tree_, true);
      move_bag_for_npc_dialog();
      tree_->bring_to_front(item_bag_);
    }
    if (repair_dialog_ != nullptr) {
      repair_dialog_->set_visible(*tree_, world.repair.dialog_visible);
      if (world.repair.dialog_visible) {
        tree_->bring_to_front(repair_dialog_);
      }
    }
    if (group_window_ != nullptr) {
      group_window_->set_visible(*tree_, world.group.visible);
      if (world.group.visible) {
        tree_->bring_to_front(group_window_);
      }
    }
    if (trade_window_ != nullptr) {
      trade_window_->set_visible(*tree_, world.trade.visible);
      if (world.trade.visible) {
        tree_->bring_to_front(trade_window_);
      }
    }
    if (guild_window_ != nullptr) {
      guild_window_->set_visible(*tree_, world.guild.visible);
      if (world.guild.visible) {
        tree_->bring_to_front(guild_window_);
      }
    }
  }

  void sync_minimap_window() {
    if (state_ == nullptr || minimap_ == nullptr || tree_ == nullptr) {
      return;
    }
    minimap_->set_visible(*tree_, state_->world.minimap.visible);
    if (minimap_->visible) {
      tree_->bring_to_front(minimap_);
    }
  }

  void sync_chat(ClientContext& context) {
    if (chat_board_ != nullptr) {
      chat_board_->state = state_;
    }
    if (chat_edit_ == nullptr) {
      return;
    }
    if (chat_edit_->visible) {
      chat_edit_->sync_from_native();
    }
    chat_edit_->sync_native_bounds(context.renderer);
    chat_edit_->set_native_visible(chat_edit_->visible);
  }

  void sync_npc_dialog(ClientContext& context) {
    if (npc_dialog_ == nullptr || context.state == nullptr) {
      return;
    }
    auto& dialog = context.state->world.npc_dialog;
    if (dialog.visible && dialog.opened_x >= 0 && dialog.opened_y >= 0) {
      const auto self_it = context.state->world.actors.find(context.state->world.self_actor_id);
      if (self_it != context.state->world.actors.end() &&
          (std::abs(self_it->second.x - dialog.opened_x) >= 8 ||
           std::abs(self_it->second.y - dialog.opened_y) >= 8)) {
        close_npc_dialog_local();
        return;
      }
    }

    if (!dialog.visible) {
      if (npc_dialog_->visible && tree_ != nullptr) {
        npc_dialog_->set_visible(*tree_, false);
      } else {
        npc_dialog_->visible = false;
      }
      npc_dialog_->clear_dialog();
      restore_bag_origin();
      return;
    }

    npc_dialog_->set_dialog(dialog);
    if (tree_ != nullptr) {
      npc_dialog_->set_visible(*tree_, true);
      tree_->bring_to_front(npc_dialog_);
    } else {
      npc_dialog_->visible = true;
    }
    move_bag_for_npc_dialog();
  }

  void open_chat(const std::wstring& initial) {
    if (chat_edit_ == nullptr) {
      return;
    }
    chat_edit_->value = initial;
    chat_edit_->sync_to_native();
    if (tree_ != nullptr) {
      chat_edit_->set_visible(*tree_, true);
      tree_->focus(chat_edit_);
    } else {
      chat_edit_->visible = true;
    }
    chat_edit_->set_native_visible(true);
    chat_edit_->select_all_to_end();
  }

  void open_chat_with_whisper(const std::string& line) {
    const auto name = extract_chat_user_name(line);
    if (name.empty()) {
      open_chat(L"");
      return;
    }
    if (state_ != nullptr) {
      state_->world.whisper_name = name;
    }
    open_chat(L"/" + widen(name) + L" ");
  }

  void submit_chat() {
    if (chat_edit_ == nullptr) {
      return;
    }
    chat_edit_->sync_from_native();
    auto text = trim_copy(chat_edit_->value);
    if (text.empty()) {
      close_chat(true);
      return;
    }
    if (lower_copy(text) == L"@password") {
      chat_password_mode_ = !chat_password_mode_;
      chat_edit_->set_password_mode(chat_password_mode_);
      close_chat(true);
      return;
    }
    if (state_ != nullptr && text.size() > 1U && text.front() == L'/') {
      const auto space = text.find(L' ', 1U);
      const auto name = text.substr(1U, space == std::wstring::npos ? std::wstring::npos
                                                                    : space - 1U);
      if (!name.empty()) {
        state_->world.whisper_name = narrow(name);
      }
    }
    pending_chat_send_ = narrow(text);
    close_chat(false);
  }

  void close_chat(const bool clear) {
    if (chat_edit_ == nullptr) {
      return;
    }
    if (clear) {
      chat_edit_->value.clear();
      chat_edit_->sync_to_native();
    }
    if (tree_ != nullptr) {
      chat_edit_->set_visible(*tree_, false);
    } else {
      chat_edit_->visible = false;
    }
    chat_edit_->set_native_visible(false);
  }

  void open_state_page(ui::UiTree& tree, const int page) {
    state_page_ = std::clamp(page, 0, 3);
    if (state_window_ == nullptr) {
      return;
    }
    state_window_->set_visible(tree, true);
    tree.bring_to_front(state_window_);
    sync_state_window();
  }

  void change_state_page(const int delta) {
    state_page_ = (state_page_ + delta + 4) % 4;
    if (state_page_ != 3) {
      magic_page_ = 0;
    }
    sync_state_window();
  }

  void change_magic_page(const int delta) {
    if (state_ == nullptr) {
      return;
    }
    const auto max_page =
        std::max(0, (static_cast<int>(state_->world.magics.size()) + 4) / 5 - 1);
    magic_page_ = std::clamp(magic_page_ + delta, 0, max_page);
  }

  void select_magic_row(const int row) {
    if (state_ == nullptr || tree_ == nullptr || key_select_dialog_ == nullptr ||
        state_page_ != 3) {
      return;
    }
    const auto index = magic_page_ * 5 + row;
    if (index < 0 || index >= static_cast<int>(state_->world.magics.size())) {
      return;
    }
    selected_magic_id_ = state_->world.magics[static_cast<std::size_t>(index)].magic_id;
    key_select_dialog_->set_visible(*tree_, true);
    tree_->bring_to_front(key_select_dialog_);
  }

  void assign_magic_key(ClientApp* app, const int key) {
    if (selected_magic_id_ == 0) {
      return;
    }
    const auto clamped_key =
        static_cast<std::uint8_t>(std::clamp(key, 0, 8));
    if (app != nullptr) {
      if (clamped_key != 0 && state_ != nullptr) {
        for (const auto& magic : state_->world.magics) {
          if (magic.magic_id != selected_magic_id_ && magic.key == clamped_key) {
            app->request_magic_key_change(
                client_v1::MagicKeyChangeRequest{magic.magic_id, 0});
          }
        }
      }
      app->request_magic_key_change(
          client_v1::MagicKeyChangeRequest{selected_magic_id_, clamped_key});
    }
    if (state_ != nullptr) {
      state_->bind_magic_key(selected_magic_id_, clamped_key);
    }
  }

  void add_magic_key_button(ClientContext& context, const int key, const int sprite_index,
                            const int x, const int y) {
    if (key_select_dialog_ == nullptr) {
      return;
    }
    auto* button = add_sprite_button(key_select_dialog_, context, ArchiveId::prguse,
                                     sprite_index, x, y, 32, 22);
    bind_audio_click(button, context.audio, LegacyClickSound::stone, [this, key, app = context.app] {
      assign_magic_key(app, key);
      if (key_select_dialog_ != nullptr && tree_ != nullptr) {
        key_select_dialog_->hide(*tree_);
      }
    });
  }

  void select_merchant_row(const int row) {
    if (state_ == nullptr) {
      return;
    }
    const auto& shop = state_->world.merchant_shop;
    const auto index = shop.page * 5 + row;
    if (index >= 0 && index < static_cast<int>(shop.goods.size())) {
      merchant_selected_index_ = index;
      const auto& item = shop.goods[static_cast<std::size_t>(index)];
      if (audio_ != nullptr) {
        audio_->play_sound(item_click_sound_id(item.std_mode, item.name));
      }
    }
  }

  void change_merchant_page(const int delta) {
    if (state_ == nullptr) {
      return;
    }
    auto& shop = state_->world.merchant_shop;
    const auto max_page =
        std::max(0, (static_cast<int>(shop.goods.size()) + 4) / 5 - 1);
    shop.page = std::clamp(shop.page + delta, 0, max_page);
    merchant_selected_index_ = shop.goods.empty() ? -1 : shop.page * 5;
  }

  void buy_selected_merchant_item(ClientApp* app) {
    if (state_ == nullptr || app == nullptr) {
      return;
    }
    const auto& shop = state_->world.merchant_shop;
    if (merchant_selected_index_ < 0 ||
        merchant_selected_index_ >= static_cast<int>(shop.goods.size())) {
      return;
    }
    const auto& item = shop.goods[static_cast<std::size_t>(merchant_selected_index_)];
    app->request_merchant_buy(
        client_v1::MerchantBuyRequest{shop.merchant_id, item.server_index, item.name});
  }

  void close_merchant_menu() {
    if (state_ != nullptr) {
      state_->world.merchant_shop = MerchantShopState{};
    }
    merchant_selected_index_ = -1;
    if (merchant_menu_ != nullptr && tree_ != nullptr) {
      merchant_menu_->hide(*tree_);
    }
    restore_bag_origin();
  }

  void clear_sell_dialog() {
    if (state_ != nullptr) {
      auto& shop = state_->world.merchant_shop;
      shop.sell_selecting = false;
      shop.pending_sell_make_index = 0;
      shop.pending_sell_name.clear();
      shop.pending_sell_price = 0;
    }
    if (merchant_sell_dialog_ != nullptr && tree_ != nullptr) {
      merchant_sell_dialog_->hide(*tree_);
    }
    restore_bag_origin();
  }

  void confirm_sell_item(ClientApp* app) {
    if (state_ == nullptr || app == nullptr) {
      return;
    }
    auto& shop = state_->world.merchant_shop;
    if (shop.pending_sell_make_index == 0 || shop.pending_sell_name.empty()) {
      return;
    }
    app->request_merchant_sell(client_v1::MerchantSellRequest{
        shop.merchant_id, shop.pending_sell_make_index, shop.pending_sell_name});
    shop.sell_selecting = false;
    shop.pending_sell_make_index = 0;
    shop.pending_sell_name.clear();
    shop.pending_sell_price = 0;
    if (merchant_sell_dialog_ != nullptr && tree_ != nullptr) {
      merchant_sell_dialog_->hide(*tree_);
    }
    restore_bag_origin();
  }

  void open_repair_selecting(ClientApp* app) {
    if (state_ == nullptr) {
      return;
    }
    auto& repair = state_->world.repair;
    const auto merchant_id = repair.merchant_id != 0 ? repair.merchant_id
                                                     : state_->world.merchant_shop.merchant_id;
    if (merchant_id == 0) {
      if (app != nullptr) {
        app->show_info_modal(L"Repair", L"Talk to a repair merchant first.");
      }
      return;
    }
    repair.merchant_id = merchant_id;
    repair.selecting = true;
    repair.dialog_visible = false;
    repair.pending_make_index = 0;
    repair.pending_name.clear();
    repair.pending_price = 0;
    if (tree_ != nullptr && item_bag_ != nullptr) {
      item_bag_->set_visible(*tree_, true);
      move_bag_for_npc_dialog();
      tree_->bring_to_front(item_bag_);
    }
  }

  void clear_repair_dialog() {
    if (state_ != nullptr) {
      state_->world.repair = RepairState{};
    }
    if (repair_dialog_ != nullptr && tree_ != nullptr) {
      repair_dialog_->hide(*tree_);
    }
    restore_bag_origin();
  }

  void confirm_repair_item(ClientApp* app) {
    if (state_ == nullptr || app == nullptr) {
      return;
    }
    auto& repair = state_->world.repair;
    if (repair.merchant_id == 0 || repair.pending_make_index == 0 ||
        repair.pending_name.empty()) {
      return;
    }
    app->request_repair_item(client_v1::MerchantRepairRequest{
        repair.merchant_id, repair.pending_make_index, repair.pending_name});
    repair.dialog_visible = false;
    repair.pending_make_index = 0;
    repair.pending_name.clear();
    repair.pending_price = 0;
    if (repair_dialog_ != nullptr && tree_ != nullptr) {
      repair_dialog_->hide(*tree_);
    }
    restore_bag_origin();
  }

  void select_storage_row(const int row) {
    if (state_ == nullptr) {
      return;
    }
    auto& storage = state_->world.storage;
    const auto index = storage_page_ * 5 + row;
    if (index >= 0 && index < static_cast<int>(storage.items.size())) {
      storage.selected_index = index;
      const auto& item = storage.items[static_cast<std::size_t>(index)];
      if (audio_ != nullptr) {
        audio_->play_sound(item_click_sound_id(item.std_mode, item.name));
      }
    }
  }

  void change_storage_page(const int delta) {
    if (state_ == nullptr) {
      return;
    }
    const auto max_page =
        std::max(0, (static_cast<int>(state_->world.storage.items.size()) + 4) / 5 - 1);
    storage_page_ = std::clamp(storage_page_ + delta, 0, max_page);
    state_->world.storage.selected_index =
        state_->world.storage.items.empty() ? -1 : storage_page_ * 5;
    if (storage_content_ != nullptr) {
      storage_content_->storage_page = storage_page_;
    }
  }

  void open_storage_deposit_selecting() {
    if (state_ == nullptr) {
      return;
    }
    auto& storage = state_->world.storage;
    if (storage.merchant_id == 0) {
      return;
    }
    storage.deposit_selecting = true;
    if (tree_ != nullptr && item_bag_ != nullptr) {
      item_bag_->set_visible(*tree_, true);
      move_bag_for_npc_dialog();
      tree_->bring_to_front(item_bag_);
    }
  }

  void withdraw_selected_storage_item(ClientApp* app) {
    if (state_ == nullptr || app == nullptr) {
      return;
    }
    auto& storage = state_->world.storage;
    if (storage.merchant_id == 0 || storage.selected_index < 0 ||
        storage.selected_index >= static_cast<int>(storage.items.size())) {
      return;
    }
    const auto& item = storage.items[static_cast<std::size_t>(storage.selected_index)];
    if (item_empty(item)) {
      return;
    }
    app->request_storage_withdraw(
        client_v1::StorageWithdrawRequest{storage.merchant_id, item.make_index, item.name});
  }

  void close_storage_window() {
    if (state_ != nullptr) {
      state_->world.storage = StorageState{};
    }
    storage_page_ = 0;
    if (storage_window_ != nullptr && tree_ != nullptr) {
      storage_window_->hide(*tree_);
    }
    restore_bag_origin();
  }

  [[nodiscard]] std::string focused_target_name() const {
    if (state_ == nullptr) {
      return {};
    }
    const auto& world = state_->world;
    const auto id = world.target_actor_id != 0 ? world.target_actor_id : world.focus_actor_id;
    if (id != 0 && id != world.self_actor_id) {
      if (const auto it = world.actors.find(id); it != world.actors.end()) {
        return it->second.name;
      }
    }
    return world.whisper_name;
  }

  void open_group(ClientApp* app) {
    if (state_ == nullptr) {
      return;
    }
    state_->world.group.visible = true;
    if (state_->world.group.members.empty()) {
      state_->world.group.members.push_back(state_->selected_character);
    }
    if (tree_ != nullptr && group_window_ != nullptr) {
      group_window_->set_visible(*tree_, true);
      tree_->bring_to_front(group_window_);
    }
    if (app != nullptr) {
      app->request_group_mode(client_v1::GroupModeRequest{state_->world.group.allow_group});
    }
  }

  void close_group_window() {
    if (state_ != nullptr) {
      state_->world.group.visible = false;
    }
    if (group_window_ != nullptr && tree_ != nullptr) {
      group_window_->hide(*tree_);
    }
  }

  void toggle_group_mode(ClientApp* app) {
    if (state_ == nullptr || app == nullptr) {
      return;
    }
    const auto allow = !state_->world.group.allow_group;
    state_->world.group.allow_group = allow;
    state_->world.group.visible = true;
    app->request_group_mode(client_v1::GroupModeRequest{allow});
  }

  void request_group_create(ClientApp* app) {
    request_group_target(app, 0);
  }

  void request_group_add(ClientApp* app) {
    request_group_target(app, 1);
  }

  void request_group_remove(ClientApp* app) {
    request_group_target(app, 2);
  }

  void request_group_target(ClientApp* app, const int op) {
    if (app == nullptr) {
      return;
    }
    const auto target = focused_target_name();
    if (target.empty()) {
      app->show_info_modal(L"Group", L"Select a player first.");
      return;
    }
    if (op == 0) {
      app->request_group_create(client_v1::GroupCreateRequest{target});
    } else if (op == 1) {
      app->request_group_add(client_v1::GroupAddMemberRequest{target});
    } else {
      app->request_group_remove(client_v1::GroupRemoveMemberRequest{target});
    }
  }

  void open_trade(ClientApp* app) {
    if (state_ == nullptr) {
      return;
    }
    state_->world.trade.visible = true;
    if (tree_ != nullptr && trade_window_ != nullptr) {
      trade_window_->set_visible(*tree_, true);
      tree_->bring_to_front(trade_window_);
    }
    if (app != nullptr) {
      app->request_trade_try(client_v1::TradeTryRequest{focused_target_name()});
    }
  }

  void close_trade_window(ClientApp* app) {
    if (app != nullptr) {
      app->request_trade_cancel(client_v1::TradeCancelRequest{});
    }
    if (state_ != nullptr) {
      state_->world.trade = TradeUiState{};
    }
    if (trade_window_ != nullptr && tree_ != nullptr) {
      trade_window_->hide(*tree_);
    }
  }

  void add_selected_bag_item_to_trade(ClientApp* app) {
    if (state_ == nullptr || app == nullptr || !valid_bag_slot(selected_bag_slot_)) {
      if (app != nullptr) {
        app->show_info_modal(L"Trade", L"Select a bag item first.");
      }
      return;
    }
    const auto& item = state_->world.bag_items[static_cast<std::size_t>(selected_bag_slot_)];
    if (item_empty(item)) {
      app->show_info_modal(L"Trade", L"Select a bag item first.");
      return;
    }
    app->request_trade_add_item(client_v1::TradeAddItemRequest{item.make_index, item.name});
  }

  void set_trade_gold(ClientApp* app) {
    if (state_ == nullptr || app == nullptr) {
      return;
    }
    app->request_trade_gold(client_v1::TradeSetGoldRequest{state_->world.self_ability.gold});
  }

  void open_guild(ClientApp* app) {
    if (state_ != nullptr) {
      state_->world.guild.visible = true;
    }
    if (tree_ != nullptr && guild_window_ != nullptr) {
      guild_window_->set_visible(*tree_, true);
      tree_->bring_to_front(guild_window_);
    }
    if (app != nullptr) {
      app->request_guild_open(client_v1::GuildOpenRequest{});
    }
  }

  void close_guild_window() {
    if (state_ != nullptr) {
      state_->world.guild.visible = false;
    }
    if (guild_window_ != nullptr && tree_ != nullptr) {
      guild_window_->hide(*tree_);
    }
  }

  void request_guild_add(ClientApp* app) {
    if (app == nullptr) {
      return;
    }
    const auto target = focused_target_name();
    if (target.empty()) {
      app->show_info_modal(L"Guild", L"Select a player first.");
      return;
    }
    app->request_guild_add(client_v1::GuildAddMemberRequest{target});
  }

  void request_guild_remove(ClientApp* app) {
    if (app == nullptr) {
      return;
    }
    const auto target = focused_target_name();
    if (target.empty()) {
      app->show_info_modal(L"Guild", L"Select a player first.");
      return;
    }
    app->request_guild_remove(client_v1::GuildRemoveMemberRequest{target});
  }

  void request_minimap(ClientApp* app) {
    if (state_ == nullptr) {
      return;
    }
    state_->world.minimap.visible = true;
    if (minimap_ != nullptr && tree_ != nullptr) {
      minimap_->set_visible(*tree_, true);
      tree_->bring_to_front(minimap_);
    }
    if (app != nullptr) {
      app->request_minimap(client_v1::MiniMapRequest{state_->world.map_id});
    }
  }

  bool scroll_chat_board(const InputState& input) {
    if (state_ == nullptr) {
      return false;
    }
    auto& world = state_->world;
    const auto max_top =
        std::max(0, static_cast<int>(world.chat_lines.size()) - kChatBoardVisibleLines);
    const auto before = std::clamp(world.chat_board_top, 0, max_top);
    auto next = before;
    if (input.key_pressed[VK_UP]) {
      next -= 1;
    } else if (input.key_pressed[VK_DOWN]) {
      next += 1;
    } else if (input.key_pressed[VK_PRIOR]) {
      next -= kChatBoardVisibleLines;
    } else if (input.key_pressed[VK_NEXT]) {
      next += kChatBoardVisibleLines;
    }
    world.chat_board_top = std::clamp(next, 0, max_top);
    return world.chat_board_top != before;
  }

  void close_npc_dialog_local() {
    if (npc_dialog_ != nullptr) {
      npc_dialog_->clear_dialog();
      if (tree_ != nullptr) {
        npc_dialog_->set_visible(*tree_, false);
      } else {
        npc_dialog_->visible = false;
      }
    }
    if (state_ != nullptr) {
      state_->close_npc_dialog();
    }
    pending_npc_select_.clear();
    pending_npc_select_merchant_id_ = 0;
    restore_bag_origin();
  }

  void move_bag_for_npc_dialog() {
    if (item_bag_ != nullptr) {
      item_bag_->bounds.x = 475;
      item_bag_->bounds.y = 0;
    }
  }

  void restore_bag_origin() {
    if (item_bag_ != nullptr) {
      item_bag_->bounds.x = 0;
      item_bag_->bounds.y = 0;
    }
  }

  // ---- 右键快捷菜单 ----

  bool handle_right_click(const InputState& input, ui::UiTree& tree) {
    // 背包格子右键
    if (item_bag_ != nullptr && item_bag_->visible && item_grid_ != nullptr) {
      const auto cell = item_grid_->cell_at(input.mouse_x, input.mouse_y);
      if (cell.has_value()) {
        const auto slot = cell->first + cell->second * kBagGridColumns + kBagGridFirstSlot;
        if (state_ != nullptr && valid_bag_slot(slot)) {
          const auto& item = state_->world.bag_items[static_cast<std::size_t>(slot)];
          if (!item_empty(item)) {
            show_context_menu(tree, slot, MovingItemSource::bag, item, input.mouse_x,
                              input.mouse_y);
            return true;
          }
        }
      }
    }
    // 装备槽右键
    if (state_window_ != nullptr && state_window_->visible) {
      const auto eq_slot = equipment_slot_at(input.mouse_x, input.mouse_y);
      if (eq_slot >= 0 && state_ != nullptr) {
        const auto& item = state_->world.equipment[static_cast<std::size_t>(eq_slot)];
        if (!item_empty(item)) {
          show_context_menu(tree, eq_slot, MovingItemSource::equipment, item, input.mouse_x,
                            input.mouse_y);
          return true;
        }
      }
    }
    return false;
  }

  void show_context_menu(ui::UiTree& tree, const int slot, const MovingItemSource source,
                         const client_v1::ItemState& item, const int mouse_x,
                         const int mouse_y) {
    hide_context_menu(tree);
    auto& menu = context_menu_;
    menu.visible = true;
    menu.target_slot = slot;
    menu.source = source;
    const auto is_bag = source == MovingItemSource::bag;
    menu.can_use = is_bag && item_usable_from_bag(item);
    menu.can_equip = is_bag && visible_equipment_slot_for_item(item) >= 0;
    menu.can_unequip = !is_bag;
    menu.can_drop = is_bag;

    // 计算菜单应在的屏幕位置
    const auto* ctx_node = tree.root();
    if (ctx_node == nullptr) {
      return;
    }
    const auto rb = ctx_node->resolved_bounds();
    menu.anchor_x = std::clamp(mouse_x, 0, rb.w - 80);
    menu.anchor_y = std::clamp(mouse_y, 0, rb.h - 100);

    if (context_menu_window_ == nullptr) {
      context_menu_window_ = tree.root()->emplace_child<ui::Window>(RectI{0, 0, 80, 24});
      context_menu_window_->floating = true;
      context_menu_window_->fallback_fill_color = 0xEE1A1A2EU;
      context_menu_window_->fallback_border_color = 0xFF4A5568U;
    }
    build_context_menu_buttons();
    context_menu_window_->bounds.x = menu.anchor_x;
    context_menu_window_->bounds.y = menu.anchor_y;
    context_menu_window_->show(tree);
    tree.bring_to_front(context_menu_window_);
  }

  void hide_context_menu(ui::UiTree& tree) {
    context_menu_ = ItemContextMenu{};
    if (context_menu_window_ != nullptr) {
      context_menu_window_->hide(tree);
    }
  }

  void clear_pending_item_clicks() {
    pending_bag_click_slot_ = -1;
    pending_bag_double_click_slot_ = -1;
    pending_equipment_click_slot_ = -1;
  }

  [[nodiscard]] bool context_menu_contains(const int x, const int y) const {
    return context_menu_window_ != nullptr && context_menu_window_->visible &&
           context_menu_window_->resolved_bounds().contains(x, y);
  }

  [[nodiscard]] bool visible_equipment_slot(const int slot) const {
    return slot >= 0 && slot < kVisibleEquipmentSlotCount &&
           valid_equipment_slot(slot) &&
           equipment_buttons_[static_cast<std::size_t>(slot)] != nullptr;
  }

  [[nodiscard]] int visible_equipment_slot_for_item(
      const client_v1::ItemState& item) const {
    if (state_ == nullptr || item_empty(item)) {
      return -1;
    }
    const auto slot = get_equipment_slot_for_item(item);
    if (!visible_equipment_slot(slot)) {
      return -1;
    }
    const auto& world = state_->world;
    if (!item_empty(world.equipment[static_cast<std::size_t>(slot)])) {
      return -1;
    }
    return equipment_slot_accepts_std_mode(slot, item.std_mode,
                                           world.self_ability_detail.sex)
               ? slot
               : -1;
  }

  void build_context_menu_buttons() {
    if (context_menu_window_ == nullptr) {
      return;
    }
    context_menu_window_->children().clear();
    const auto& menu = context_menu_;
    int row = 0;
    auto add_item = [&](const std::wstring& text, std::function<void()> action) {
      auto* btn = context_menu_window_->emplace_child<ui::Button>(RectI{2, 2 + row * 22, 76, 20});
      btn->text = text;
      btn->on_click = [this, action = std::move(action)] {
        action();
        if (tree_ != nullptr) {
          hide_context_menu(*tree_);
        }
      };
      ++row;
    };
    if (menu.can_use)      add_item(L"使用", [this] { execute_context_use(); });
    if (menu.can_equip)    add_item(L"装备", [this] { execute_context_equip(); });
    if (menu.can_unequip)  add_item(L"卸装", [this] { execute_context_unequip(); });
    if (menu.can_drop)     add_item(L"丢弃", [this] { execute_context_drop(); });
    const auto h = 4 + row * 22;
    context_menu_window_->bounds.h = h;
  }

  void execute_context_use() {
    const auto slot = context_menu_.target_slot;
    if (state_ == nullptr || !valid_bag_slot(slot)) {
      return;
    }
    handle_bag_use(slot);
  }

  void execute_context_equip() {
    const auto slot = context_menu_.target_slot;
    if (state_ == nullptr || !valid_bag_slot(slot)) {
      return;
    }
    handle_bag_equip(slot);
  }

  void execute_context_unequip() {
    const auto slot = context_menu_.target_slot;
    if (state_ == nullptr || !valid_equipment_slot(slot)) {
      return;
    }
    handle_equipment_unequip(slot);
  }

  void execute_context_drop() {
    const auto slot = context_menu_.target_slot;
    if (state_ == nullptr) {
      return;
    }
    if (context_menu_.source == MovingItemSource::bag && valid_bag_slot(slot)) {
      handle_bag_drop(slot);
    }
  }

  void handle_bag_use(const int slot) {
    if (state_ == nullptr || !valid_bag_slot(slot)) {
      return;
    }
    const auto& item = state_->world.bag_items[static_cast<std::size_t>(slot)];
    if (!item_usable_from_bag(item)) {
      return;
    }
    if (app_ != nullptr) {
      state_->begin_pending_item_action(PendingItemActionKind::use, MovingItemSource::bag, slot,
                                        -1, item, GetTickCount64());
      app_->request_use_item(client_v1::UseItemIntent{item.make_index, slot});
    }
  }

  void handle_bag_equip(const int slot) {
    if (state_ == nullptr || !valid_bag_slot(slot)) {
      return;
    }
    const auto& item = state_->world.bag_items[static_cast<std::size_t>(slot)];
    const auto equip_slot = visible_equipment_slot_for_item(item);
    if (equip_slot < 0) {
      return;
    }
    if (app_ != nullptr) {
      state_->begin_pending_item_action(PendingItemActionKind::equip, MovingItemSource::bag, slot,
                                        equip_slot, item, GetTickCount64());
      app_->request_equip_item(
          client_v1::EquipItemRequest{equip_slot, item.make_index, item.name});
    }
  }

  void handle_equipment_unequip(const int slot) {
    if (state_ == nullptr || !valid_equipment_slot(slot)) {
      return;
    }
    const auto& item = state_->world.equipment[static_cast<std::size_t>(slot)];
    if (app_ != nullptr) {
      state_->begin_pending_item_action(PendingItemActionKind::unequip,
                                        MovingItemSource::equipment, slot, -1, item,
                                        GetTickCount64());
      app_->request_unequip_item(
          client_v1::UnequipItemRequest{slot, item.make_index, item.name});
    }
  }

  void handle_bag_drop(const int slot) {
    if (state_ == nullptr || !valid_bag_slot(slot)) {
      return;
    }
    auto& item = state_->world.bag_items[static_cast<std::size_t>(slot)];
    if (app_ != nullptr) {
      state_->begin_pending_item_action(PendingItemActionKind::drop, MovingItemSource::bag, slot,
                                        -1, item, GetTickCount64());
      app_->request_drop_item(
          client_v1::DropItemRequest{item.make_index, item.name});
    }
    item = client_v1::ItemState{};
  }

  /// 根据物品 std_mode 返回对应的装备槽位
  static int get_equipment_slot_for_item(const client_v1::ItemState& item) {
    switch (item.std_mode) {
      case 5:
      case 6:   return kEquipWeapon;
      case 10:
      case 11:  return kEquipDress;
      case 15:  return kEquipHelmet;
      case 19:
      case 20:
      case 21:  return kEquipNecklace;
      case 22:
      case 23:  return kEquipRingLeft;
      case 24:
      case 26:  return kEquipArmRingRight;
      case 25:  return kEquipBujuk;
      case 30:  return kEquipRightHand;
      case 52:  return kEquipBoots;
      case 53:  return kEquipCharm;
      case 54:  return kEquipBelt;
      default:  return -1;
    }
  }

  void add_equipment_button(const int slot, const RectI bounds) {
    if (state_window_ == nullptr || !valid_equipment_slot(slot)) {
      return;
    }
    auto* button = add_hotspot_button(state_window_, bounds);
    button->draw_fallback = false;
    button->on_click = [this, slot] {
      play_legacy_click(audio_, LegacyClickSound::glass);
      pending_equipment_click_slot_ = slot;
    };
    button->on_custom_paint = [this, slot](SoftwareRenderer& renderer, const RectI& rect) {
      if (state_ == nullptr || !valid_equipment_slot(slot)) {
        return;
      }
      const auto& item = state_->world.equipment[static_cast<std::size_t>(slot)];
      if (item_empty(item)) {
        return;
      }
      draw_equipment_item_icon(renderer, item_icon_frame(assets_, item, ArchiveId::state_item),
                               rect);
    };
    equipment_buttons_[static_cast<std::size_t>(slot)] = button;
  }

  void update_hover_state(const InputState& input, WorldViewState& world) {
    hovered_bag_slot_ = -1;
    hovered_belt_slot_ = -1;
    if (item_bag_ != nullptr && item_bag_->visible && item_grid_ != nullptr) {
      const auto cell = item_grid_->cell_at(input.mouse_x, input.mouse_y);
      if (cell.has_value()) {
        hovered_bag_slot_ =
            cell->first + cell->second * kBagGridColumns + kBagGridFirstSlot;
      }
    }
    if (hovered_bag_slot_ < 0) {
      hovered_belt_slot_ = belt_slot_at(input.mouse_x, input.mouse_y);
      if (hovered_belt_slot_ >= 0) {
        hovered_bag_slot_ = hovered_belt_slot_;
      }
    }
    hovered_equipment_slot_ = state_window_ != nullptr && state_window_->visible
                                  ? equipment_slot_at(input.mouse_x, input.mouse_y)
                                  : -1;
    world.hovered_bag_slot = hovered_bag_slot_;
    world.hovered_equipment_slot = hovered_equipment_slot_;
  }

  void update_drag_overlay(const InputState& input, const WorldViewState& world) {
    if (drag_overlay_ == nullptr) {
      return;
    }
    if (!world.moving_item.active) {
      drag_overlay_->clear();
      return;
    }
    drag_overlay_->set_sprite(item_icon_frame(assets_, world.moving_item.item, ArchiveId::items));
    drag_overlay_->set_position(input.mouse_x, input.mouse_y);
  }

  void update_tooltip(const InputState& input, const WorldViewState& world) {
    if (tooltip_ == nullptr) {
      return;
    }
    tooltip_->hide();
    if (world.moving_item.active) {
      return;
    }
    if (valid_bag_slot(hovered_bag_slot_)) {
      const auto& item = world.bag_items[static_cast<std::size_t>(hovered_bag_slot_)];
      if (!item_empty(item)) {
        tooltip_->show_at(input.mouse_x + 12, input.mouse_y + 16, item_tooltip_text(item),
                          item_name_color(item.std_mode));
        return;
      }
    }
    if (valid_equipment_slot(hovered_equipment_slot_)) {
      const auto& item =
          world.equipment[static_cast<std::size_t>(hovered_equipment_slot_)];
      if (!item_empty(item)) {
        tooltip_->show_at(input.mouse_x + 12, input.mouse_y + 16, item_tooltip_text(item),
                          item_name_color(item.std_mode));
        return;
      }
    }
    if (item_bag_ != nullptr && item_bag_->visible) {
      const auto rect = item_bag_->resolved_bounds();
      const auto lx = input.mouse_x - rect.x;
      const auto ly = input.mouse_y - rect.y;
      if (RectI{242, 203, 30, 20}.contains(lx, ly)) {
        tooltip_->show_at(input.mouse_x + 12, input.mouse_y + 16, L"Repair", 0xFFFFFF66U);
        return;
      }
      if (RectI{274, 203, 30, 20}.contains(lx, ly)) {
        tooltip_->show_at(input.mouse_x + 12, input.mouse_y + 16,
                          L"Gold " + std::to_wstring(world.self_ability.gold),
                          0xFFFFFF66U);
        return;
      }
    }
    show_bottom_tooltip(input, world);
  }

  void show_bottom_tooltip(const InputState& input, const WorldViewState& world) {
    if (tooltip_ == nullptr || bottom_ == nullptr || !bottom_->is_visible_in_tree()) {
      return;
    }
    const auto bottom_rect = bottom_->resolved_bounds();
    const auto lx = input.mouse_x - bottom_rect.x;
    const auto ly = input.mouse_y - bottom_rect.y;
    const auto self_it = world.actors.find(world.self_actor_id);
    const auto* self = self_it != world.actors.end() ? &self_it->second : nullptr;
    const auto& ability = world.self_ability;

    if (self != nullptr && lx > 39 && lx < 39 + 90 && ly > 90 && ly < 180) {
      auto text = L"HP(" + std::to_wstring(self->hp) + L"/" +
                  std::to_wstring(self->max_hp) + L")";
      if (ability.job != 0 || ability.level >= 26) {
        text += L" MP(" + std::to_wstring(self->mp) + L"/" +
                std::to_wstring(self->max_mp) + L")";
      }
      tooltip_->show_at(input.mouse_x + 12, input.mouse_y + 16, std::move(text), 0xFFFFFF66U);
      return;
    }

    if (RectI{660, 28, 50, 14}.contains(lx, ly)) {
      tooltip_->show_at(input.mouse_x + 12, input.mouse_y + 16, L"Level", 0xFFFFFF66U);
      return;
    }
    if (RectI{666, 59, 40, 10}.contains(lx, ly)) {
      std::wstringstream out;
      const auto percent = ability.max_exp == 0
                               ? 0.0
                               : (static_cast<double>(ability.exp) /
                                  static_cast<double>(ability.max_exp)) *
                                     100.0;
      out << std::fixed << std::setprecision(2) << percent;
      tooltip_->show_at(input.mouse_x + 12, input.mouse_y + 16, out.str(), 0xFFFFFF66U);
      return;
    }
    if (RectI{666, 92, 40, 10}.contains(lx, ly)) {
      tooltip_->show_at(input.mouse_x + 12, input.mouse_y + 16,
                        std::to_wstring(ability.weight) + L"/" +
                            std::to_wstring(ability.max_weight),
                        0xFFFFFF66U);
      return;
    }

    if (const auto hint = bottom_button_tooltip(lx, ly); !hint.empty()) {
      tooltip_->show_at(input.mouse_x + 12, input.mouse_y + 16, hint, 0xFFFFFF66U);
    }
  }

  [[nodiscard]] std::wstring bottom_button_tooltip(const int lx, const int ly) const {
    struct ButtonHint {
      RectI rect;
      const wchar_t* text;
    };
    const ButtonHint hints[] = {
        {RectI{219, 104, 30, 24}, L"Minimap (V)"},
        {RectI{249, 104, 30, 24}, L"Trade (T)"},
        {RectI{279, 104, 30, 24}, L"Guild (G)"},
        {RectI{309, 104, 30, 24}, L"Group (P)"},
        {RectI{339, 104, 30, 24}, L"Ability"},
        {RectI{530, 104, 30, 24}, L"Logout (Alt-X)"},
        {RectI{560, 104, 30, 24}, L"Exit (Alt-Q)"},
        {RectI{643, 61, 38, 38}, L"Equipment (F10,C)"},
        {RectI{682, 41, 38, 38}, L"Bag (F9,I)"},
        {RectI{722, 21, 38, 38}, L"Magic (F11,S)"},
        {RectI{764, 11, 36, 36}, L"Options"},
    };
    for (const auto& hint : hints) {
      if (hint.rect.contains(lx, ly)) {
        return hint.text;
      }
    }
    return {};
  }

  void handle_bag_click(ClientContext& context, const int slot) {
    if (context.state == nullptr || !valid_bag_slot(slot)) {
      return;
    }
    auto& world = context.state->world;
    if (world.pending_item_action.active) {
      return;
    }
    auto& item = world.bag_items[static_cast<std::size_t>(slot)];
    world.selected_bag_slot = slot;
    selected_bag_slot_ = slot;

    if (world.repair.selecting && !world.moving_item.active) {
      if (item_empty(item) || context.app == nullptr) {
        return;
      }
      play_item_click(audio_, item);
      world.repair.pending_make_index = item.make_index;
      world.repair.pending_name = item.name;
      world.repair.pending_price = 0;
      context.app->request_repair_price(client_v1::MerchantRepairPriceRequest{
          world.repair.merchant_id, item.make_index, item.name});
      return;
    }

    if (world.storage.deposit_selecting && !world.moving_item.active) {
      if (item_empty(item) || context.app == nullptr) {
        return;
      }
      play_item_click(audio_, item);
      world.storage.deposit_selecting = false;
      context.app->request_storage_deposit(client_v1::StorageDepositRequest{
          world.storage.merchant_id, item.make_index, item.name});
      return;
    }

    if (world.merchant_shop.sell_selecting && !world.moving_item.active) {
      if (item_empty(item) || context.app == nullptr) {
        return;
      }
      play_item_click(audio_, item);
      world.merchant_shop.pending_sell_make_index = item.make_index;
      world.merchant_shop.pending_sell_name = item.name;
      context.app->request_merchant_sell_price(client_v1::MerchantSellPriceRequest{
          world.merchant_shop.merchant_id, item.make_index, item.name});
      return;
    }

    if (world.trade.visible && !world.moving_item.active) {
      if (!item_empty(item)) {
        play_item_click(audio_, item);
      }
      return;
    }

    if (!world.moving_item.active) {
      if (item_empty(item)) {
        return;
      }
      play_item_click(audio_, item);
      world.moving_item.active = true;
      world.moving_item.source = MovingItemSource::bag;
      world.moving_item.source_slot = slot;
      world.moving_item.item = item;
      item = client_v1::ItemState{};
      return;
    }

    const auto moving_from_equipment = world.moving_item.source == MovingItemSource::equipment;
    const auto moving_source_slot = world.moving_item.source_slot;
    const auto moving_item = world.moving_item.item;
    if (is_belt_slot(slot) && !belt_slot_accepts_item(moving_item)) {
      return;
    }
    if (moving_from_equipment) {
      if (context.app == nullptr || !valid_equipment_slot(moving_source_slot) ||
          !item_empty(item)) {
        return;
      }
      context.state->begin_pending_item_action(PendingItemActionKind::unequip,
                                               MovingItemSource::equipment, moving_source_slot,
                                               slot, moving_item, detail::monotonic_ms());
      context.app->request_unequip_item(client_v1::UnequipItemRequest{
          moving_source_slot, moving_item.make_index, moving_item.name});
      play_item_click(audio_, moving_item);
      world.moving_item = MovingItemState{};
      return;
    }

    if (item_empty(item)) {
      play_item_click(audio_, moving_item);
      item = moving_item;
      world.moving_item = MovingItemState{};
      return;
    }

    auto swapped_item = item;
    play_item_click(audio_, moving_item);
    item = moving_item;
    world.moving_item.active = true;
    world.moving_item.source = MovingItemSource::bag;
    world.moving_item.source_slot = slot;
    world.moving_item.item = swapped_item;
  }

  void handle_bag_double_click(ClientContext& context, const int slot) {
    if (context.state == nullptr || context.app == nullptr || !valid_bag_slot(slot)) {
      return;
    }
    auto& world = context.state->world;
    auto item = world.bag_items[static_cast<std::size_t>(slot)];
    const auto using_moving_item =
        item_empty(item) && world.moving_item.active &&
        world.moving_item.source == MovingItemSource::bag && world.moving_item.source_slot == slot;
    if (using_moving_item) {
      item = world.moving_item.item;
    }
    if (world.pending_item_action.active) {
      return;
    }
    if (!item_usable_from_bag(item)) {
      const auto equip_slot = visible_equipment_slot_for_item(item);
      if (equip_slot < 0) {
        return;
      }
      context.state->begin_pending_item_action(PendingItemActionKind::equip,
                                               MovingItemSource::bag, slot, equip_slot, item,
                                               detail::monotonic_ms());
      context.app->request_equip_item(
          client_v1::EquipItemRequest{equip_slot, item.make_index, item.name});
      play_item_click(audio_, item);
      if (using_moving_item) {
        world.moving_item = MovingItemState{};
      }
      return;
    }
    play_item_use(audio_, item);
    client_v1::UseItemIntent intent;
    intent.item_slot = slot;
    intent.item_make_index = item.make_index;
    intent.name = item.name;
    world.eating_item_slot = slot;
    world.eating_item_make_index = item.make_index;
    world.eat_time_ms = detail::monotonic_ms();
    if (using_moving_item) {
      context.state->begin_pending_item_action(PendingItemActionKind::use,
                                               world.moving_item.source,
                                               world.moving_item.source_slot, slot, item,
                                               world.eat_time_ms);
      world.moving_item = MovingItemState{};
    } else {
      context.state->begin_pending_item_action(PendingItemActionKind::use,
                                               MovingItemSource::bag, slot, slot, item,
                                               world.eat_time_ms);
      world.bag_items[static_cast<std::size_t>(slot)] = client_v1::ItemState{};
    }
    context.app->request_use_item(intent);
  }

  void handle_equipment_click(ClientContext& context, const int slot) {
    if (context.state == nullptr || !valid_equipment_slot(slot)) {
      return;
    }
    auto& world = context.state->world;
    if (world.pending_item_action.active) {
      return;
    }
    auto& equipped = world.equipment[static_cast<std::size_t>(slot)];

    if (!world.moving_item.active) {
      if (item_empty(equipped)) {
        return;
      }
      play_item_click(audio_, equipped);
      world.moving_item.active = true;
      world.moving_item.source = MovingItemSource::equipment;
      world.moving_item.source_slot = slot;
      world.moving_item.item = equipped;
      equipped = client_v1::ItemState{};
      return;
    }

    const auto moving_item = world.moving_item.item;
    if (!equipment_slot_accepts_std_mode(slot, moving_item.std_mode,
                                         world.self_ability_detail.sex)) {
      return;
    }

    if (world.moving_item.source == MovingItemSource::equipment &&
        world.moving_item.source_slot == slot) {
      play_item_click(audio_, moving_item);
      equipped = moving_item;
      world.moving_item = MovingItemState{};
      return;
    }

    if (world.moving_item.source == MovingItemSource::bag && context.app != nullptr) {
      if (!item_empty(equipped)) {
        return;
      }
      context.state->begin_pending_item_action(PendingItemActionKind::equip,
                                               MovingItemSource::bag,
                                               world.moving_item.source_slot, slot, moving_item,
                                               detail::monotonic_ms());
      context.app->request_equip_item(
          client_v1::EquipItemRequest{slot, moving_item.make_index, moving_item.name});
      play_item_click(audio_, moving_item);
      world.moving_item = MovingItemState{};
      return;
    }
  }

  void restore_moving_item(WorldViewState& world) {
    if (!world.moving_item.active) {
      return;
    }
    const auto moving = world.moving_item;
    if (moving.source == MovingItemSource::bag && valid_bag_slot(moving.source_slot)) {
      auto& item = world.bag_items[static_cast<std::size_t>(moving.source_slot)];
      if (item_empty(item)) {
        item = moving.item;
        world.moving_item = MovingItemState{};
        return;
      }
    }
    if (moving.source == MovingItemSource::equipment && valid_equipment_slot(moving.source_slot)) {
      auto& item = world.equipment[static_cast<std::size_t>(moving.source_slot)];
      if (item_empty(item)) {
        item = moving.item;
        world.moving_item = MovingItemState{};
        return;
      }
    }
    for (auto& item : world.bag_items) {
      if (item_empty(item)) {
        item = moving.item;
        world.moving_item = MovingItemState{};
        return;
      }
    }
  }

  void toggle_bag(ui::UiTree& tree) {
    if (item_bag_ == nullptr) {
      return;
    }
    item_bag_->set_visible(tree, !item_bag_->visible);
    if (item_bag_->visible) {
      if (npc_dialog_visible()) {
        move_bag_for_npc_dialog();
      } else {
        restore_bag_origin();
      }
      tree.bring_to_front(item_bag_);
    }
  }

  void toggle_state(ui::UiTree& tree) {
    if (state_window_ == nullptr) {
      return;
    }
    const auto show = !state_window_->visible;
    if (show) {
      state_page_ = 0;
      magic_page_ = 0;
    }
    state_window_->set_visible(tree, show);
    if (state_window_->visible) {
      tree.bring_to_front(state_window_);
    }
    sync_state_window();
  }

  bool initialized_{false};
  ui::UiTree* tree_{nullptr};
  GameStateStore* state_{nullptr};
  AssetManager* assets_{nullptr};
  AudioService* audio_{nullptr};
  ui::Window* bottom_{nullptr};
  LegacyBottomStatusNode* bottom_status_{nullptr};
  ui::Window* item_bag_{nullptr};
  ui::Grid* item_grid_{nullptr};
  ui::Window* state_window_{nullptr};
  LegacyStateContentNode* state_content_{nullptr};
  HotspotButton* state_prev_button_{nullptr};
  HotspotButton* state_next_button_{nullptr};
  HotspotButton* magic_up_button_{nullptr};
  HotspotButton* magic_down_button_{nullptr};
  ui::Window* key_select_dialog_{nullptr};
  ui::Window* merchant_menu_{nullptr};
  MerchantGoodsNode* merchant_goods_content_{nullptr};
  ui::Window* merchant_sell_dialog_{nullptr};
  MerchantSellNode* merchant_sell_content_{nullptr};
  ui::Window* storage_window_{nullptr};
  StorageListNode* storage_content_{nullptr};
  ui::Window* repair_dialog_{nullptr};
  RepairDialogNode* repair_content_{nullptr};
  ui::Window* group_window_{nullptr};
  GroupPanelNode* group_content_{nullptr};
  ui::Window* trade_window_{nullptr};
  TradePanelNode* trade_content_{nullptr};
  ui::Window* guild_window_{nullptr};
  GuildPanelNode* guild_content_{nullptr};
  LegacyMiniMapNode* minimap_{nullptr};
  std::array<HotspotButton*, 6> belt_buttons_{};
  std::array<std::uint64_t, 6> belt_last_click_ms_{};
  std::array<HotspotButton*, kEquipmentSlotCount> equipment_buttons_{};
  std::array<HotspotButton*, 5> magic_row_buttons_{};
  std::array<HotspotButton*, 5> merchant_row_buttons_{};
  std::array<HotspotButton*, 5> storage_row_buttons_{};
  ChatBoardNode* chat_board_{nullptr};
  ResourceTextEdit* chat_edit_{nullptr};
  NpcDialogNode* npc_dialog_{nullptr};
  ui::Tooltip* tooltip_{nullptr};
  ui::DragSpriteOverlay* drag_overlay_{nullptr};
  ClientApp* app_{nullptr};
  HFONT chat_edit_font_{nullptr};
  int hovered_bag_slot_{-1};
  int hovered_belt_slot_{-1};
  int hovered_equipment_slot_{-1};
  int selected_bag_slot_{-1};
  int pending_bag_click_slot_{-1};
  int pending_bag_double_click_slot_{-1};
  int pending_equipment_click_slot_{-1};

  // 右键快捷菜单状态
  struct ItemContextMenu {
    bool visible{false};
    int anchor_x{0};
    int anchor_y{0};
    int target_slot{-1};
    MovingItemSource source{MovingItemSource::bag};
    bool can_use{false};
    bool can_equip{false};
    bool can_unequip{false};
    bool can_drop{false};
  };
  ItemContextMenu context_menu_{};
  ui::Window* context_menu_window_{nullptr};
  bool context_menu_pointer_consumed_{false};
  bool suppress_item_click_until_left_release_{false};

  std::string pending_chat_send_{};
  std::string pending_npc_select_{};
  std::uint64_t pending_npc_select_merchant_id_{0};
  int state_page_{0};
  int magic_page_{0};
  std::uint16_t selected_magic_id_{0};
  int merchant_selected_index_{-1};
  int storage_page_{0};
  bool chat_password_mode_{false};
};

/// 将职业编号转换为显示名称
std::wstring job_name(const std::uint8_t job) {
  switch (job) {
    case 0:
      return L"Warrior";
    case 1:
      return L"Wizard";
    case 2:
      return L"Taoist";
    default:
      return L"Unknown";
  }
}

std::wstring connection_phase_text(const GameStateStore::ConnectionPhase phase) {
  switch (phase) {
    case GameStateStore::ConnectionPhase::login:
      return L"cnsLogin";
    case GameStateStore::ConnectionPhase::select_character:
      return L"cnsSelChr";
    case GameStateStore::ConnectionPhase::reselect_character:
      return L"cnsReSelChr";
    case GameStateStore::ConnectionPhase::play:
      return L"cnsPlay";
  }
  return L"cnsLogin";
}

std::wstring selected_server_text(const LobbyViewState& lobby) {
  if (!lobby.selected_server_name.empty()) {
    return widen(lobby.selected_server_name);
  }
  if (!lobby.servers.empty()) {
    return widen(lobby.servers.front().name);
  }
  return L"ModernServer";
}

RectI select_button_bounds(ClientContext& context, const int x, const int y,
                           const int sprite_index) {
  return sprite_rect(get_frame(context, ArchiveId::prguse, sprite_index), x, y, 88, 28);
}

/// 绘制角色选择界面中的角色预览
/// 包含待机、冻结/解冻和角色信息文字
void draw_select_character(ClientContext& context, const client_v1::CharacterSummary& character,
                           const int slot_index, const CharacterSelectPose& pose) {
  auto base_x = 71;
  auto base_y = 52;
  auto frame_x = base_x;
  auto frame_y = base_y;
  auto effect_x = 90;
  auto effect_y = 58;

  switch (character.job) {
    case 0:
      if (character.sex == 0) {
        base_x = 71;
        base_y = 52;
        frame_x = base_x;
        frame_y = base_y;
      } else {
        base_x = 65;
        base_y = 55;
        frame_x = base_x;
        frame_y = base_y;
      }
      break;
    case 1:
      if (character.sex == 0) {
        base_x = 77;
        base_y = 46;
        frame_x = base_x;
        frame_y = base_y;
      } else {
        base_x = 171;
        base_y = 97;
        frame_x = base_x - 30;
        frame_y = base_y - 14;
      }
      break;
    case 2:
      if (character.sex == 0) {
        base_x = 85;
        base_y = 63;
        frame_x = base_x;
        frame_y = base_y;
      } else {
        base_x = 164;
        base_y = 103;
        frame_x = base_x - 23;
        frame_y = base_y - 20;
      }
      break;
    default:
      break;
  }

  if (slot_index == 1) {
    effect_x = 430;
    effect_y = 60;
    base_x += 340;
    base_y += 2;
    frame_x += 340;
    frame_y += 2;
  }

  if (pose.draw_effect) {
    const auto effect_frame = kSelectEffectFirstIndex + pose.effect_frame;
    draw_archive_sprite(context, ArchiveId::chr_sel, effect_frame, effect_x, effect_y, 216U);
  }

  if (pose.kind == CharacterSelectPoseKind::idle) {
    const auto body_frame = kSelectIdleFirstIndex + static_cast<int>(character.job) * 40 +
                            pose.body_frame + static_cast<int>(character.sex) * 120;
    draw_archive_sprite(context, ArchiveId::chr_sel, body_frame, frame_x, frame_y);
  } else {
    const auto body_frame = kSelectFreezeFirstIndex + static_cast<int>(character.job) * 40 +
                            pose.body_frame + static_cast<int>(character.sex) * 120;
    draw_archive_sprite(context, ArchiveId::chr_sel, body_frame, base_x, base_y);
  }

  if (slot_index == 0) {
    context.renderer->draw_text(117, 494, widen(character.name), 0xFFF5F7FAU);
    context.renderer->draw_text(117, 523, widen(std::to_string(character.level)), 0xFFF5F7FAU);
    context.renderer->draw_text(117, 553, job_name(character.job), 0xFFF5F7FAU);
    return;
  }

  context.renderer->draw_text(671, 496, widen(character.name), 0xFFF5F7FAU);
  context.renderer->draw_text(671, 525, widen(std::to_string(character.level)), 0xFFF5F7FAU);
  context.renderer->draw_text(671, 555, job_name(character.job), 0xFFF5F7FAU);
}

/// 启动场景：短暂延迟后自动切换到登录场景
class BootScene final : public Scene {
 public:
  void enter(ClientContext& /*context*/) override { elapsed_ = 0.0f; }
  void exit(ClientContext& /*context*/) override {}
  void update(ClientContext& context, float delta_seconds) override {
    elapsed_ += delta_seconds;
    if (elapsed_ >= 0.15f) {
      context.app->request_scene_change(SceneId::login);
    }
  }
  void render(ClientContext& context) override {
    (void)context;
  }
  ui::UiTree& ui_tree() override { return ui_; }

 private:
  ui::UiTree ui_{};
  float elapsed_{0.0f};
};

/// 登录场景：处理登录、创建账号和修改密码
/// 使用 Win32 EDIT 原生控件叠加在精灵界面上
/// 管理 13 个注册表单字段的焦点切换和验证
class LoginScene final : public Scene {
 public:
  enum class LoginMode {
    login,
    new_account,
    change_password,
    closed
  };

  void enter(ClientContext& context) override {
    app_ = context.app;
    state_ = context.state;
    ui_.clear();
    create_edit_font();
    login_rect_ =
        centered_rect(get_frame(context, ArchiveId::prguse, kLoginDialogIndex), 800, 600, 360, 280);
    auto* root = ui_.set_root<ui::UiNode>(RectI{0, 0, 800, 600});
    new_account_rect_ =
        centered_rect(get_frame(context, ArchiveId::prguse, kNewAccountDialogIndex), 800, 600, 642, 472);
    change_password_rect_ =
        centered_rect(get_frame(context, ArchiveId::prguse, kChangePasswordDialogIndex), 800, 600, 416, 320);

    account_edit_ = root->emplace_child<ResourceTextEdit>(RectI{350, 259, 137, 16});
    account_edit_->placeholder = L"";
    account_edit_->on_submit = [this] { focus_password_edit(); };

    password_edit_ = root->emplace_child<ResourceTextEdit>(RectI{350, 291, 137, 16});
    password_edit_->password_mode = true;
    password_edit_->placeholder = L"";
    password_edit_->on_submit = [this] { submit(); };

    change_password_button_ =
        add_sprite_button(root, context, ArchiveId::prguse, kLoginChangePasswordButtonIndex,
                          login_rect_.x + 111, login_rect_.y + 207);
    bind_audio_click(change_password_button_, context.audio, LegacyClickSound::normal,
                     [this] { open_change_password_dialog(); });

    create_account_button_ =
        add_sprite_button(root, context, ArchiveId::prguse, kLoginCreateButtonIndex,
                          login_rect_.x + 24, login_rect_.y + 207);
    bind_audio_click(create_account_button_, context.audio, LegacyClickSound::stone,
                     [this] { open_create_account_dialog(); });

    login_button_ = add_sprite_button(root, context, ArchiveId::prguse, kLoginSubmitButtonIndex,
                                      login_rect_.x + 171, login_rect_.y + 165);
    bind_audio_click(login_button_, context.audio, LegacyClickSound::stone,
                     [this] { submit(); });

    close_button_ = add_sprite_button(root, context, ArchiveId::prguse, kLoginCloseButtonIndex,
                                      login_rect_.x + 252, login_rect_.y + 28);
    bind_audio_click(close_button_, context.audio, LegacyClickSound::stone, [this] {
      if (app_ != nullptr) {
        app_->request_close();
      }
    });

    new_account_overlay_ = root->emplace_child<ui::UiNode>(RectI{0, 0, 800, 600});
    new_account_overlay_->visible = false;
    new_account_id_edit_ = new_account_overlay_->emplace_child<ResourceTextEdit>(
        RectI{new_account_rect_.x + 161, new_account_rect_.y + 116, 116, 16});
    new_account_id_edit_->on_submit = [this] { focus_next_new_account_field(new_account_id_edit_); };
    new_account_password_edit_ = new_account_overlay_->emplace_child<ResourceTextEdit>(
        RectI{new_account_rect_.x + 161, new_account_rect_.y + 137, 116, 16});
    new_account_password_edit_->password_mode = true;
    new_account_password_edit_->on_submit = [this] {
      focus_next_new_account_field(new_account_password_edit_);
    };
    new_account_confirm_edit_ = new_account_overlay_->emplace_child<ResourceTextEdit>(
        RectI{new_account_rect_.x + 161, new_account_rect_.y + 158, 116, 16});
    new_account_confirm_edit_->password_mode = true;
    new_account_confirm_edit_->on_submit = [this] {
      focus_next_new_account_field(new_account_confirm_edit_);
    };
    new_account_display_name_edit_ = new_account_overlay_->emplace_child<ResourceTextEdit>(
        RectI{new_account_rect_.x + 161, new_account_rect_.y + 187, 116, 16});
    new_account_display_name_edit_->on_submit = [this] {
      focus_next_new_account_field(new_account_display_name_edit_);
    };
    new_account_ss_no_edit_ = new_account_overlay_->emplace_child<ResourceTextEdit>(
        RectI{new_account_rect_.x + 161, new_account_rect_.y + 207, 116, 16});
    new_account_ss_no_edit_->on_submit = [this] {
      focus_next_new_account_field(new_account_ss_no_edit_);
    };
    new_account_birthday_edit_ = new_account_overlay_->emplace_child<ResourceTextEdit>(
        RectI{new_account_rect_.x + 161, new_account_rect_.y + 227, 116, 16});
    new_account_birthday_edit_->on_submit = [this] {
      focus_next_new_account_field(new_account_birthday_edit_);
    };
    new_account_quiz1_edit_ = new_account_overlay_->emplace_child<ResourceTextEdit>(
        RectI{new_account_rect_.x + 161, new_account_rect_.y + 256, 163, 16});
    new_account_quiz1_edit_->on_submit = [this] {
      focus_next_new_account_field(new_account_quiz1_edit_);
    };
    new_account_answer1_edit_ = new_account_overlay_->emplace_child<ResourceTextEdit>(
        RectI{new_account_rect_.x + 161, new_account_rect_.y + 276, 163, 16});
    new_account_answer1_edit_->on_submit = [this] {
      focus_next_new_account_field(new_account_answer1_edit_);
    };
    new_account_quiz2_edit_ = new_account_overlay_->emplace_child<ResourceTextEdit>(
        RectI{new_account_rect_.x + 161, new_account_rect_.y + 297, 163, 16});
    new_account_quiz2_edit_->on_submit = [this] {
      focus_next_new_account_field(new_account_quiz2_edit_);
    };
    new_account_answer2_edit_ = new_account_overlay_->emplace_child<ResourceTextEdit>(
        RectI{new_account_rect_.x + 161, new_account_rect_.y + 317, 163, 16});
    new_account_answer2_edit_->on_submit = [this] {
      focus_next_new_account_field(new_account_answer2_edit_);
    };
    new_account_phone_edit_ = new_account_overlay_->emplace_child<ResourceTextEdit>(
        RectI{new_account_rect_.x + 161, new_account_rect_.y + 347, 116, 16});
    new_account_phone_edit_->on_submit = [this] {
      focus_next_new_account_field(new_account_phone_edit_);
    };
    new_account_mobile_phone_edit_ = new_account_overlay_->emplace_child<ResourceTextEdit>(
        RectI{new_account_rect_.x + 161, new_account_rect_.y + 368, 116, 16});
    new_account_mobile_phone_edit_->on_submit = [this] {
      focus_next_new_account_field(new_account_mobile_phone_edit_);
    };
    new_account_email_edit_ = new_account_overlay_->emplace_child<ResourceTextEdit>(
        RectI{new_account_rect_.x + 161, new_account_rect_.y + 388, 116, 16});
    new_account_email_edit_->on_submit = [this] {
      focus_next_new_account_field(new_account_email_edit_);
    };
    new_account_ok_button_ =
        add_sprite_button(new_account_overlay_, context, ArchiveId::prguse, kLoginSubmitButtonIndex,
                          new_account_rect_.x + 160, new_account_rect_.y + 417);
    bind_audio_click(new_account_ok_button_, context.audio, LegacyClickSound::stone,
                     [this] { submit_create_account_dialog(); });
    new_account_cancel_button_ =
        add_sprite_button(new_account_overlay_, context, ArchiveId::prguse, kCancelButtonIndex,
                          new_account_rect_.x + 448, new_account_rect_.y + 419);
    bind_audio_click(new_account_cancel_button_, context.audio, LegacyClickSound::normal,
                     [this] { close_overlay(); });
    new_account_close_button_ =
        add_sprite_button(new_account_overlay_, context, ArchiveId::prguse, kLoginCloseButtonIndex,
                          new_account_rect_.x + 587, new_account_rect_.y + 33);
    bind_audio_click(new_account_close_button_, context.audio, LegacyClickSound::stone,
                     [this] { close_overlay(); });

    change_password_overlay_ = root->emplace_child<ui::UiNode>(RectI{0, 0, 800, 600});
    change_password_overlay_->visible = false;
    change_id_edit_ = change_password_overlay_->emplace_child<ResourceTextEdit>(
        RectI{change_password_rect_.x + 239, change_password_rect_.y + 117, 137, 16});
    change_id_edit_->on_submit = [this] { focus_next_change_password_field(change_id_edit_); };
    change_current_password_edit_ = change_password_overlay_->emplace_child<ResourceTextEdit>(
        RectI{change_password_rect_.x + 239, change_password_rect_.y + 149, 137, 16});
    change_current_password_edit_->password_mode = true;
    change_current_password_edit_->on_submit = [this] {
      focus_next_change_password_field(change_current_password_edit_);
    };
    change_new_password_edit_ = change_password_overlay_->emplace_child<ResourceTextEdit>(
        RectI{change_password_rect_.x + 239, change_password_rect_.y + 176, 137, 16});
    change_new_password_edit_->password_mode = true;
    change_new_password_edit_->on_submit = [this] {
      focus_next_change_password_field(change_new_password_edit_);
    };
    change_repeat_password_edit_ = change_password_overlay_->emplace_child<ResourceTextEdit>(
        RectI{change_password_rect_.x + 239, change_password_rect_.y + 208, 137, 16});
    change_repeat_password_edit_->password_mode = true;
    change_repeat_password_edit_->on_submit = [this] {
      if (change_id_edit_ != nullptr) {
        ui_.focus(change_id_edit_);
      }
    };
    change_password_ok_button_ =
        add_sprite_button(change_password_overlay_, context, ArchiveId::prguse,
                          kLoginSubmitButtonIndex, change_password_rect_.x + 182,
                          change_password_rect_.y + 252);
    bind_audio_click(change_password_ok_button_, context.audio, LegacyClickSound::stone,
                     [this] { submit_change_password_dialog(); });
    change_password_cancel_button_ =
        add_sprite_button(change_password_overlay_, context, ArchiveId::prguse, kCancelButtonIndex,
                          change_password_rect_.x + 277, change_password_rect_.y + 251);
    bind_audio_click(change_password_cancel_button_, context.audio, LegacyClickSound::stone,
                     [this] { close_overlay(); });

    const auto initial_login_state = context.state->login.login_state;
    attach_native_edit_controls();
    change_login_state(LoginMode::login);
    if (account_edit_ != nullptr) {
      account_edit_->value = widen(context.state->login.account_id);
      account_edit_->sync_to_native();
    }
    if (password_edit_ != nullptr) {
      password_edit_->value = widen(context.state->login.password);
      password_edit_->sync_to_native();
    }
    if (initial_login_state == LoginState::lsNewidRetry) {
      open_create_account_dialog();
      if (new_account_id_edit_ != nullptr) {
        new_account_id_edit_->value = widen(context.state->login.account_id);
        new_account_id_edit_->sync_to_native();
      }
      if (new_account_password_edit_ != nullptr && new_account_confirm_edit_ != nullptr) {
        new_account_password_edit_->value = widen(context.state->login.password);
        new_account_confirm_edit_->value = widen(context.state->login.password);
        new_account_password_edit_->sync_to_native();
        new_account_confirm_edit_->sync_to_native();
      }
      apply_profile_to_fields(context.state->login.account_profile);
      context.state->login.login_state = LoginState::lsNewidRetry;
    }
    if (context.audio != nullptr) {
      context.audio->play_bgm(bmg_intro);
    }
  }

  void exit(ClientContext& context) override {
    if (context.audio != nullptr) {
      context.audio->silence();
    }
    sync_native_edit_values();
    if (login_mode_ == LoginMode::login) {
      context.state->login.account_id = narrow(account_edit_ != nullptr ? account_edit_->value : L"");
      context.state->login.password = narrow(password_edit_ != nullptr ? password_edit_->value : L"");
    }
    detach_native_edit_controls();
    destroy_edit_font();
    app_ = nullptr;
    state_ = nullptr;
  }

  void update(ClientContext& context, float /*delta_seconds*/) override {
    sync_native_edit_values();
    if (context.state->login.needs_account_update &&
        (login_mode_ != LoginMode::new_account || !account_update_mode_)) {
      open_update_account_dialog(context.state->login.account_profile);
    }
    if (login_mode_ == LoginMode::login) {
      set_login_controls_visible(true);
    }
    sync_native_edit_visibility(context.state->modal.visible);
  }

  void render(ClientContext& context) override {
    draw_archive_sprite(context, ArchiveId::chr_sel, kLoginBackgroundIndex, 0, 0);
    render_login_dialog(context);
    if (login_mode_ == LoginMode::new_account) {
      render_create_account_dialog(context);
    } else if (login_mode_ == LoginMode::change_password) {
      render_change_password_dialog(context);
    }
  }

  ui::UiTree& ui_tree() override { return ui_; }

 private:
  void create_edit_font() {
    destroy_edit_font();
    if (app_ == nullptr || app_->window_handle() == nullptr) {
      return;
    }
    const auto dc = GetDC(app_->window_handle());
    const auto font_height = -MulDiv(10, GetDeviceCaps(dc, LOGPIXELSY), 72);
    ReleaseDC(app_->window_handle(), dc);
    edit_font_ = CreateFontW(font_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"MS Sans Serif");
  }

  void destroy_edit_font() {
    if (edit_font_ != nullptr) {
      DeleteObject(edit_font_);
      edit_font_ = nullptr;
    }
  }

  [[nodiscard]] std::array<ResourceTextEdit*, 13> new_account_fields() const {
    return {new_account_id_edit_,       new_account_password_edit_,
            new_account_confirm_edit_,  new_account_display_name_edit_,
            new_account_ss_no_edit_,    new_account_birthday_edit_,
            new_account_quiz1_edit_,    new_account_answer1_edit_,
            new_account_quiz2_edit_,    new_account_answer2_edit_,
            new_account_phone_edit_,    new_account_mobile_phone_edit_,
            new_account_email_edit_};
  }

  void attach_native_edit_controls() {
    if (app_ == nullptr || app_->window_handle() == nullptr || edit_font_ == nullptr) {
      return;
    }
    account_edit_->attach_native(app_->window_handle(), edit_font_, 20, false, false, false);
    password_edit_->attach_native(app_->window_handle(), edit_font_, 10, true, true, false);
    new_account_id_edit_->attach_native(app_->window_handle(), edit_font_, 10, false, false, false);
    new_account_password_edit_->attach_native(app_->window_handle(), edit_font_, 10, true, true, true);
    new_account_confirm_edit_->attach_native(app_->window_handle(), edit_font_, 10, true, true, true);
    new_account_display_name_edit_->attach_native(app_->window_handle(), edit_font_, 20, false, false,
                                                 false);
    new_account_ss_no_edit_->attach_native(app_->window_handle(), edit_font_, 14, false, false, false);
    new_account_birthday_edit_->attach_native(app_->window_handle(), edit_font_, 10, false, false,
                                              false);
    new_account_quiz1_edit_->attach_native(app_->window_handle(), edit_font_, 20, false, false,
                                           false);
    new_account_answer1_edit_->attach_native(app_->window_handle(), edit_font_, 12, false, false,
                                             false);
    new_account_quiz2_edit_->attach_native(app_->window_handle(), edit_font_, 20, false, false,
                                           false);
    new_account_answer2_edit_->attach_native(app_->window_handle(), edit_font_, 12, false, false,
                                             false);
    new_account_phone_edit_->attach_native(app_->window_handle(), edit_font_, 14, false, false,
                                           false);
    new_account_mobile_phone_edit_->attach_native(app_->window_handle(), edit_font_, 13, false,
                                                  false, false);
    new_account_email_edit_->attach_native(app_->window_handle(), edit_font_, 40, false, false,
                                           false);
    change_id_edit_->attach_native(app_->window_handle(), edit_font_, 10, false, false, false);
    change_current_password_edit_->attach_native(app_->window_handle(), edit_font_, 10, true, false,
                                                 false);
    change_new_password_edit_->attach_native(app_->window_handle(), edit_font_, 10, true, true, true);
    change_repeat_password_edit_->attach_native(app_->window_handle(), edit_font_, 10, true, true,
                                                true);
  }

  void detach_native_edit_controls() {
    if (account_edit_ != nullptr) {
      account_edit_->detach_native();
    }
    if (password_edit_ != nullptr) {
      password_edit_->detach_native();
    }
    for (auto* field : new_account_fields()) {
      if (field != nullptr) {
        field->detach_native();
      }
    }
    if (change_id_edit_ != nullptr) {
      change_id_edit_->detach_native();
    }
    if (change_current_password_edit_ != nullptr) {
      change_current_password_edit_->detach_native();
    }
    if (change_new_password_edit_ != nullptr) {
      change_new_password_edit_->detach_native();
    }
    if (change_repeat_password_edit_ != nullptr) {
      change_repeat_password_edit_->detach_native();
    }
  }

  void sync_native_edit_values() {
    if (account_edit_ != nullptr) {
      account_edit_->sync_from_native();
    }
    if (password_edit_ != nullptr) {
      password_edit_->sync_from_native();
    }
    for (auto* field : new_account_fields()) {
      if (field != nullptr) {
        field->sync_from_native();
      }
    }
    if (change_id_edit_ != nullptr) {
      change_id_edit_->sync_from_native();
    }
    if (change_current_password_edit_ != nullptr) {
      change_current_password_edit_->sync_from_native();
    }
    if (change_new_password_edit_ != nullptr) {
      change_new_password_edit_->sync_from_native();
    }
    if (change_repeat_password_edit_ != nullptr) {
      change_repeat_password_edit_->sync_from_native();
    }
  }

  void sync_native_edit_visibility(const bool modal_visible) {
    const auto login_visible = login_mode_ == LoginMode::login && !modal_visible;
    const auto new_account_visible = login_mode_ == LoginMode::new_account && !modal_visible;
    const auto change_password_visible =
        login_mode_ == LoginMode::change_password && !modal_visible;

    if (account_edit_ != nullptr) {
      account_edit_->set_native_visible(login_visible);
    }
    if (password_edit_ != nullptr) {
      password_edit_->set_native_visible(login_visible);
    }
    for (auto* field : new_account_fields()) {
      if (field != nullptr) {
        field->set_native_visible(new_account_visible);
        field->set_native_enabled(new_account_visible);
      }
    }
    if (new_account_id_edit_ != nullptr && account_update_mode_) {
      new_account_id_edit_->set_native_enabled(false);
    }
    if (change_id_edit_ != nullptr) {
      change_id_edit_->set_native_visible(change_password_visible);
      change_id_edit_->set_native_enabled(change_password_visible);
    }
    if (change_current_password_edit_ != nullptr) {
      change_current_password_edit_->set_native_visible(change_password_visible);
      change_current_password_edit_->set_native_enabled(change_password_visible);
    }
    if (change_new_password_edit_ != nullptr) {
      change_new_password_edit_->set_native_visible(change_password_visible);
      change_new_password_edit_->set_native_enabled(change_password_visible);
    }
    if (change_repeat_password_edit_ != nullptr) {
      change_repeat_password_edit_->set_native_visible(change_password_visible);
      change_repeat_password_edit_->set_native_enabled(change_password_visible);
    }
  }

  void render_login_dialog(ClientContext& context) {
    if (login_mode_ == LoginMode::login) {
      draw_archive_sprite(context, ArchiveId::prguse, kLoginDialogIndex, login_rect_.x,
                          login_rect_.y);
    }
  }

  void clear_all_fields() {
    if (account_edit_ != nullptr) {
      account_edit_->value.clear();
      account_edit_->sync_to_native();
    }
    if (password_edit_ != nullptr) {
      password_edit_->value.clear();
      password_edit_->sync_to_native();
    }
    for (auto* field : new_account_fields()) {
      if (field != nullptr) {
        field->value.clear();
        field->sync_to_native();
      }
    }
    if (change_id_edit_ != nullptr) {
      change_id_edit_->value.clear();
      change_id_edit_->sync_to_native();
    }
    if (change_current_password_edit_ != nullptr) {
      change_current_password_edit_->value.clear();
      change_current_password_edit_->sync_to_native();
    }
    if (change_new_password_edit_ != nullptr) {
      change_new_password_edit_->value.clear();
      change_new_password_edit_->sync_to_native();
    }
    if (change_repeat_password_edit_ != nullptr) {
      change_repeat_password_edit_->value.clear();
      change_repeat_password_edit_->sync_to_native();
    }
  }

  void set_login_controls_visible(const bool visible) {
    if (account_edit_ != nullptr) {
      account_edit_->visible = visible;
    }
    if (password_edit_ != nullptr) {
      password_edit_->visible = visible;
    }
    if (change_password_button_ != nullptr) {
      change_password_button_->visible = visible;
    }
    if (create_account_button_ != nullptr) {
      create_account_button_->visible = visible;
    }
    if (login_button_ != nullptr) {
      login_button_->visible = visible;
    }
    if (close_button_ != nullptr) {
      close_button_->visible = visible;
    }
  }

  void change_login_state(const LoginMode mode) {
    if (mode != LoginMode::new_account) {
      account_update_mode_ = false;
    }
    login_mode_ = mode;
    if (state_ != nullptr) {
      switch (mode) {
        case LoginMode::login:
          state_->login.login_state = LoginState::lsLogin;
          break;
        case LoginMode::new_account:
          state_->login.login_state = LoginState::lsNewid;
          break;
        case LoginMode::change_password:
          state_->login.login_state = LoginState::lsChgpw;
          break;
        case LoginMode::closed:
          state_->login.login_state = LoginState::lsCloseAll;
          break;
      }
    }
    clear_all_fields();
    set_login_controls_visible(mode == LoginMode::login);
    if (new_account_overlay_ != nullptr) {
      new_account_overlay_->visible = mode == LoginMode::new_account;
    }
    if (change_password_overlay_ != nullptr) {
      change_password_overlay_->visible = mode == LoginMode::change_password;
    }

    switch (mode) {
      case LoginMode::login:
        if (account_edit_ != nullptr) {
          ui_.focus(account_edit_);
        }
        break;
      case LoginMode::new_account:
        if (account_update_mode_ && new_account_password_edit_ != nullptr) {
          ui_.focus(new_account_password_edit_);
        } else if (new_account_id_edit_ != nullptr) {
          ui_.focus(new_account_id_edit_);
        }
        break;
      case LoginMode::change_password:
        if (change_id_edit_ != nullptr) {
          ui_.focus(change_id_edit_);
        }
        break;
      case LoginMode::closed:
        ui_.focus(nullptr);
        break;
    }
    sync_native_edit_visibility(false);
  }

  void focus_password_edit() {
    if (account_edit_ == nullptr || password_edit_ == nullptr) {
      return;
    }
    account_edit_->value = lower_copy(trim_copy(account_edit_->value));
    account_edit_->sync_to_native();
    if (!account_edit_->value.empty()) {
      ui_.focus(password_edit_);
    }
  }

  [[nodiscard]] static bool valid_birthday(const std::wstring& value) {
    std::wistringstream stream(value);
    int year = 0;
    int month = 0;
    int day = 0;
    wchar_t slash1 = 0;
    wchar_t slash2 = 0;
    if (!(stream >> year >> slash1 >> month >> slash2 >> day)) {
      return false;
    }
    if (slash1 != L'/' || slash2 != L'/') {
      return false;
    }
    stream >> std::ws;
    return stream.eof() && year > 1890 && year <= 2101 && month > 0 && month <= 12 &&
           day > 0 && day <= 31;
  }

  bool require_new_account_field(ResourceTextEdit* field, const std::wstring& message) {
    if (field == nullptr) {
      return false;
    }
    field->value = trim_copy(field->value);
    field->sync_to_native();
    if (!field->value.empty()) {
      return true;
    }
    state_->login.status = message;
    ui_.focus(field);
    return false;
  }

  void focus_next_new_account_field(ResourceTextEdit* current) {
    if (current == nullptr) {
      return;
    }
    current->value = trim_copy(current->value);
    if (current == new_account_id_edit_) {
      new_account_id_edit_->value = lower_copy(new_account_id_edit_->value);
      new_account_id_edit_->sync_to_native();
      if (new_account_id_edit_->value.size() < 3) {
        state_->login.status = L"ID must be at least 3 characters.";
        ui_.focus(new_account_id_edit_);
        return;
      }
      ui_.focus(new_account_password_edit_);
      return;
    }
    if (current == new_account_password_edit_) {
      new_account_password_edit_->value = sanitize_password_copy(new_account_password_edit_->value, true);
      new_account_password_edit_->sync_to_native();
      if (new_account_password_edit_->value.size() < 4) {
        state_->login.status = L"Password must be at least 4 characters.";
        ui_.focus(new_account_password_edit_);
        return;
      }
      ui_.focus(new_account_confirm_edit_);
      return;
    }
    if (current == new_account_confirm_edit_) {
      new_account_confirm_edit_->value = sanitize_password_copy(new_account_confirm_edit_->value, true);
      new_account_confirm_edit_->sync_to_native();
      if (new_account_confirm_edit_->value != new_account_password_edit_->value) {
        state_->login.status = L"Password confirmation does not match.";
        ui_.focus(new_account_confirm_edit_);
        return;
      }
      ui_.focus(new_account_display_name_edit_);
      return;
    }
    if (current == new_account_display_name_edit_) {
      if (!require_new_account_field(new_account_display_name_edit_, L"Name is required.")) {
        return;
      }
      ui_.focus(new_account_ss_no_edit_);
      return;
    }
    if (current == new_account_ss_no_edit_) {
      ui_.focus(new_account_birthday_edit_);
      return;
    }
    if (current == new_account_birthday_edit_) {
      new_account_birthday_edit_->value = trim_copy(new_account_birthday_edit_->value);
      new_account_birthday_edit_->sync_to_native();
      if (!valid_birthday(new_account_birthday_edit_->value)) {
        state_->login.status = L"Birthday must be YYYY/MM/DD.";
        ui_.focus(new_account_birthday_edit_);
        return;
      }
      ui_.focus(new_account_quiz1_edit_);
      return;
    }
    if (current == new_account_quiz1_edit_) {
      if (!require_new_account_field(new_account_quiz1_edit_, L"First question is required.")) {
        return;
      }
      ui_.focus(new_account_answer1_edit_);
      return;
    }
    if (current == new_account_answer1_edit_) {
      if (!require_new_account_field(new_account_answer1_edit_, L"First answer is required.")) {
        return;
      }
      ui_.focus(new_account_quiz2_edit_);
      return;
    }
    if (current == new_account_quiz2_edit_) {
      if (!require_new_account_field(new_account_quiz2_edit_, L"Second question is required.")) {
        return;
      }
      ui_.focus(new_account_answer2_edit_);
      return;
    }
    if (current == new_account_answer2_edit_) {
      if (!require_new_account_field(new_account_answer2_edit_, L"Second answer is required.")) {
        return;
      }
      ui_.focus(new_account_phone_edit_);
      return;
    }
    if (current == new_account_phone_edit_) {
      ui_.focus(new_account_mobile_phone_edit_);
      return;
    }
    if (current == new_account_mobile_phone_edit_) {
      ui_.focus(new_account_email_edit_);
      return;
    }
    if (current == new_account_email_edit_) {
      ui_.focus(account_update_mode_ ? static_cast<ui::UiNode*>(new_account_password_edit_)
                                     : static_cast<ui::UiNode*>(new_account_id_edit_));
    }
  }

  void focus_next_change_password_field(ResourceTextEdit* current) {
    if (current == nullptr) {
      return;
    }
    current->value = trim_copy(current->value);
    if (current == change_id_edit_) {
      change_id_edit_->value = lower_copy(change_id_edit_->value);
      change_id_edit_->sync_to_native();
      if (change_id_edit_->value.empty()) {
        ui_.focus(change_id_edit_);
        return;
      }
      ui_.focus(change_current_password_edit_);
      return;
    }
    if (current == change_current_password_edit_) {
      change_current_password_edit_->value =
          sanitize_password_copy(change_current_password_edit_->value, false);
      change_current_password_edit_->sync_to_native();
      if (change_current_password_edit_->value.empty()) {
        ui_.focus(change_current_password_edit_);
        return;
      }
      ui_.focus(change_new_password_edit_);
      return;
    }
    if (current == change_new_password_edit_) {
      change_new_password_edit_->value = sanitize_password_copy(change_new_password_edit_->value, true);
      change_new_password_edit_->sync_to_native();
      if (change_new_password_edit_->value.empty()) {
        ui_.focus(change_new_password_edit_);
        return;
      }
      ui_.focus(change_repeat_password_edit_);
    }
  }

  void submit() {
    if (app_ == nullptr || state_ == nullptr || account_edit_ == nullptr || password_edit_ == nullptr) {
      return;
    }
    account_edit_->value = lower_copy(trim_copy(account_edit_->value));
    password_edit_->value = sanitize_password_copy(password_edit_->value, false);
    account_edit_->sync_to_native();
    password_edit_->sync_to_native();
    if (account_edit_->value.empty()) {
      ui_.focus(account_edit_);
      return;
    }
    if (password_edit_->value.empty()) {
      ui_.focus(password_edit_);
      return;
    }
    state_->login.account_id = narrow(account_edit_->value);
    state_->login.password = narrow(password_edit_->value);
    app_->request_login(state_->login.account_id, state_->login.password);
  }

  [[nodiscard]] bool new_account_controls_ready() const {
    if (new_account_overlay_ == nullptr) {
      return false;
    }
    for (auto* field : new_account_fields()) {
      if (field == nullptr) {
        return false;
      }
    }
    return true;
  }

  client_v1::AccountProfile build_account_profile_from_fields() {
    client_v1::AccountProfile profile;
    profile.display_name = narrow(new_account_display_name_edit_->value);
    profile.user_name = profile.display_name;
    profile.ss_no = narrow(new_account_ss_no_edit_->value);
    if (profile.ss_no.empty()) {
      profile.ss_no = "650101-1455111";
    }
    profile.birthday = narrow(new_account_birthday_edit_->value);
    profile.quiz = narrow(new_account_quiz1_edit_->value);
    profile.answer = narrow(new_account_answer1_edit_->value);
    profile.quiz2 = narrow(new_account_quiz2_edit_->value);
    profile.answer2 = narrow(new_account_answer2_edit_->value);
    profile.phone = narrow(new_account_phone_edit_->value);
    profile.mobile_phone = narrow(new_account_mobile_phone_edit_->value);
    profile.email = narrow(new_account_email_edit_->value);
    return profile;
  }

  void apply_profile_to_fields(const client_v1::AccountProfile& profile) {
    const auto name = profile.user_name.empty() ? profile.display_name : profile.user_name;
    new_account_display_name_edit_->value = widen(name);
    new_account_ss_no_edit_->value = widen(profile.ss_no);
    new_account_birthday_edit_->value = widen(profile.birthday);
    new_account_quiz1_edit_->value = widen(profile.quiz);
    new_account_answer1_edit_->value = widen(profile.answer);
    new_account_quiz2_edit_->value = widen(profile.quiz2);
    new_account_answer2_edit_->value = widen(profile.answer2);
    new_account_phone_edit_->value = widen(profile.phone);
    new_account_mobile_phone_edit_->value = widen(profile.mobile_phone);
    new_account_email_edit_->value = widen(profile.email);
    for (auto* field : new_account_fields()) {
      if (field != nullptr) {
        field->sync_to_native();
      }
    }
  }

  void open_create_account_dialog() {
    if (!new_account_controls_ready()) {
      return;
    }
    account_update_mode_ = false;
    change_login_state(LoginMode::new_account);
  }

  void open_update_account_dialog(const client_v1::AccountProfile& profile) {
    if (!new_account_controls_ready() || state_ == nullptr) {
      return;
    }
    account_update_mode_ = true;
    change_login_state(LoginMode::new_account);
    new_account_id_edit_->value = widen(state_->login.account_id);
    new_account_password_edit_->value = widen(state_->login.password);
    new_account_confirm_edit_->value = widen(state_->login.password);
    apply_profile_to_fields(profile);
    state_->login.status = L"Complete account details to continue.";
    sync_native_edit_visibility(false);
  }

  void submit_create_account_dialog() {
    if (app_ == nullptr || state_ == nullptr || !new_account_controls_ready()) {
      return;
    }
    new_account_id_edit_->value = lower_copy(trim_copy(new_account_id_edit_->value));
    new_account_password_edit_->value = sanitize_password_copy(new_account_password_edit_->value, true);
    new_account_confirm_edit_->value = sanitize_password_copy(new_account_confirm_edit_->value, true);
    new_account_display_name_edit_->value = trim_copy(new_account_display_name_edit_->value);
    new_account_ss_no_edit_->value = trim_copy(new_account_ss_no_edit_->value);
    new_account_birthday_edit_->value = trim_copy(new_account_birthday_edit_->value);
    new_account_quiz1_edit_->value = trim_copy(new_account_quiz1_edit_->value);
    new_account_answer1_edit_->value = trim_copy(new_account_answer1_edit_->value);
    new_account_quiz2_edit_->value = trim_copy(new_account_quiz2_edit_->value);
    new_account_answer2_edit_->value = trim_copy(new_account_answer2_edit_->value);
    new_account_phone_edit_->value = trim_copy(new_account_phone_edit_->value);
    new_account_mobile_phone_edit_->value = trim_copy(new_account_mobile_phone_edit_->value);
    new_account_email_edit_->value = trim_copy(new_account_email_edit_->value);
    for (auto* field : new_account_fields()) {
      if (field != nullptr) {
        field->sync_to_native();
      }
    }
    if (new_account_id_edit_->value.size() < 3) {
      state_->login.status = L"ID must be at least 3 characters.";
      ui_.focus(new_account_id_edit_);
      return;
    }
    if (new_account_password_edit_->value.size() < 4) {
      state_->login.status = L"Password must be at least 4 characters.";
      ui_.focus(new_account_password_edit_);
      return;
    }
    if (new_account_password_edit_->value != new_account_confirm_edit_->value) {
      state_->login.status = L"Password confirmation does not match.";
      ui_.focus(new_account_confirm_edit_);
      return;
    }
    if (new_account_display_name_edit_->value.empty()) {
      state_->login.status = L"Name is required.";
      ui_.focus(new_account_display_name_edit_);
      return;
    }
    if (!valid_birthday(new_account_birthday_edit_->value)) {
      state_->login.status = L"Birthday must be YYYY/MM/DD.";
      ui_.focus(new_account_birthday_edit_);
      return;
    }
    if (new_account_quiz1_edit_->value.empty()) {
      state_->login.status = L"First question is required.";
      ui_.focus(new_account_quiz1_edit_);
      return;
    }
    if (new_account_answer1_edit_->value.empty()) {
      state_->login.status = L"First answer is required.";
      ui_.focus(new_account_answer1_edit_);
      return;
    }
    if (new_account_quiz2_edit_->value.empty()) {
      state_->login.status = L"Second question is required.";
      ui_.focus(new_account_quiz2_edit_);
      return;
    }
    if (new_account_answer2_edit_->value.empty()) {
      state_->login.status = L"Second answer is required.";
      ui_.focus(new_account_answer2_edit_);
      return;
    }

    const auto account_id = narrow(new_account_id_edit_->value);
    const auto password = narrow(new_account_password_edit_->value);
    const auto profile = build_account_profile_from_fields();
    if (account_update_mode_) {
      state_->login.needs_account_update = false;
      app_->request_update_account(account_id, password, profile);
    } else {
      app_->request_create_account(account_id, password, profile);
    }
    change_login_state(LoginMode::login);
    if (state_ != nullptr) {
      state_->login.login_state = LoginState::lsNewid;
    }
  }

  void open_change_password_dialog() {
    if (change_password_overlay_ == nullptr || change_id_edit_ == nullptr ||
        change_current_password_edit_ == nullptr || change_new_password_edit_ == nullptr ||
        change_repeat_password_edit_ == nullptr) {
      return;
    }
    change_login_state(LoginMode::change_password);
  }

  void submit_change_password_dialog() {
    if (app_ == nullptr || state_ == nullptr || account_edit_ == nullptr || password_edit_ == nullptr ||
        change_id_edit_ == nullptr || change_current_password_edit_ == nullptr ||
        change_new_password_edit_ == nullptr || change_repeat_password_edit_ == nullptr) {
      return;
    }
    change_id_edit_->value = lower_copy(trim_copy(change_id_edit_->value));
    change_current_password_edit_->value =
        sanitize_password_copy(change_current_password_edit_->value, false);
    change_new_password_edit_->value = sanitize_password_copy(change_new_password_edit_->value, true);
    change_repeat_password_edit_->value =
        sanitize_password_copy(change_repeat_password_edit_->value, true);
    change_id_edit_->sync_to_native();
    change_current_password_edit_->sync_to_native();
    change_new_password_edit_->sync_to_native();
    change_repeat_password_edit_->sync_to_native();
    if (change_id_edit_->value.empty() || change_current_password_edit_->value.empty() ||
        change_new_password_edit_->value.empty()) {
      state_->login.status = L"Change password requires all fields.";
      ui_.focus(change_id_edit_->value.empty()
                    ? static_cast<ui::UiNode*>(change_id_edit_)
                    : (change_current_password_edit_->value.empty()
                           ? static_cast<ui::UiNode*>(change_current_password_edit_)
                           : static_cast<ui::UiNode*>(change_new_password_edit_)));
      return;
    }
    if (change_new_password_edit_->value != change_repeat_password_edit_->value) {
      state_->login.status = L"New passwords do not match.";
      ui_.focus(change_repeat_password_edit_);
      return;
    }

    app_->request_change_password(narrow(change_id_edit_->value),
                                  narrow(change_current_password_edit_->value),
                                  narrow(change_new_password_edit_->value));
    change_login_state(LoginMode::login);
    if (state_ != nullptr) {
      state_->login.login_state = LoginState::lsChgpw;
    }
  }

  void close_overlay() {
    if (account_update_mode_) {
      if (state_ != nullptr) {
        state_->login.status = L"Account details are required.";
      }
      return;
    }
    change_login_state(LoginMode::login);
  }

  void render_create_account_dialog(ClientContext& context) {
    draw_archive_sprite(context, ArchiveId::prguse, kNewAccountDialogIndex, new_account_rect_.x,
                        new_account_rect_.y);
  }

  void render_change_password_dialog(ClientContext& context) {
    draw_archive_sprite(context, ArchiveId::prguse, kChangePasswordDialogIndex,
                        change_password_rect_.x, change_password_rect_.y);
  }

  ClientApp* app_{nullptr};
  GameStateStore* state_{nullptr};
  ui::UiTree ui_{};
  HFONT edit_font_{nullptr};
  ResourceTextEdit* account_edit_{nullptr};
  ResourceTextEdit* password_edit_{nullptr};
  HotspotButton* change_password_button_{nullptr};
  HotspotButton* create_account_button_{nullptr};
  HotspotButton* login_button_{nullptr};
  HotspotButton* close_button_{nullptr};
  LoginMode login_mode_{LoginMode::login};
  RectI login_rect_{};
  RectI new_account_rect_{};
  RectI change_password_rect_{};
  ui::UiNode* new_account_overlay_{nullptr};
  ResourceTextEdit* new_account_id_edit_{nullptr};
  ResourceTextEdit* new_account_password_edit_{nullptr};
  ResourceTextEdit* new_account_confirm_edit_{nullptr};
  ResourceTextEdit* new_account_display_name_edit_{nullptr};
  ResourceTextEdit* new_account_ss_no_edit_{nullptr};
  ResourceTextEdit* new_account_birthday_edit_{nullptr};
  ResourceTextEdit* new_account_quiz1_edit_{nullptr};
  ResourceTextEdit* new_account_answer1_edit_{nullptr};
  ResourceTextEdit* new_account_quiz2_edit_{nullptr};
  ResourceTextEdit* new_account_answer2_edit_{nullptr};
  ResourceTextEdit* new_account_phone_edit_{nullptr};
  ResourceTextEdit* new_account_mobile_phone_edit_{nullptr};
  ResourceTextEdit* new_account_email_edit_{nullptr};
  HotspotButton* new_account_ok_button_{nullptr};
  HotspotButton* new_account_cancel_button_{nullptr};
  HotspotButton* new_account_close_button_{nullptr};
  ui::UiNode* change_password_overlay_{nullptr};
  ResourceTextEdit* change_id_edit_{nullptr};
  ResourceTextEdit* change_current_password_edit_{nullptr};
  ResourceTextEdit* change_new_password_edit_{nullptr};
  ResourceTextEdit* change_repeat_password_edit_{nullptr};
  HotspotButton* change_password_ok_button_{nullptr};
  HotspotButton* change_password_cancel_button_{nullptr};
  bool account_update_mode_{false};
};

/// 服务器选择场景：显示可用服务器列表供玩家选择
class ServerSelectScene final : public Scene {
 public:
  void enter(ClientContext& context) override {
    app_ = context.app;
    state_ = context.state;
    ui_.clear();
    server_buttons_.clear();

    auto* root = ui_.set_root<ui::UiNode>(RectI{0, 0, 800, 600});
    dialog_rect_ = centered_rect(get_frame(context, ArchiveId::prguse, kServerSelectDialogIndex),
                                 800, 600, 300, 360);

    server_close_button_ = add_sprite_button(root, context, ArchiveId::prguse,
                                             kLoginCloseButtonIndex, dialog_rect_.x + 244,
                                             dialog_rect_.y + 30, 24, 24);
    bind_audio_click(server_close_button_, context.audio, LegacyClickSound::stone, [this] {
      if (app_ != nullptr) {
        app_->request_close();
      }
    });

    const auto count = context.state->lobby.servers.size();
    const auto visible_count = std::min<std::size_t>(count, kMaxServerButtons);
    const auto server_top = 235 - static_cast<int>(42 * visible_count) / 2;
    for (std::size_t index = 0; index < visible_count; ++index) {
      auto* button =
          add_hotspot_button(root, RectI{dialog_rect_.x + 63,
                                         dialog_rect_.y + server_top +
                                             static_cast<int>(index) * 42,
                                         180, 34});
      bind_audio_click(button, context.audio, LegacyClickSound::stone,
                       [this, index] { select_server(index); });
      server_buttons_.push_back(button);
    }
    if (context.audio != nullptr) {
      context.audio->play_bgm(bmg_intro);
    }
  }

  void exit(ClientContext& /*context*/) override {
    app_ = nullptr;
    state_ = nullptr;
    server_buttons_.clear();
  }

  void update(ClientContext& context, float /*delta_seconds*/) override {
    if (context.state->lobby.servers.empty()) {
      context.state->lobby.selected_server_index = 0;
      return;
    }
    if (context.state->lobby.selected_server_index < 0) {
      context.state->lobby.selected_server_index = 0;
    } else if (context.state->lobby.selected_server_index >=
               static_cast<int>(context.state->lobby.servers.size())) {
      context.state->lobby.selected_server_index =
          static_cast<int>(context.state->lobby.servers.size() - 1);
    }
  }

  void render(ClientContext& context) override {
    draw_archive_sprite(context, ArchiveId::chr_sel, kLoginBackgroundIndex, 0, 0);
    draw_archive_sprite(context, ArchiveId::prguse, kServerSelectDialogIndex, dialog_rect_.x,
                        dialog_rect_.y);
    const auto visible_count =
        std::min<std::size_t>(context.state->lobby.servers.size(), kMaxServerButtons);
    for (std::size_t index = 0; index < visible_count; ++index) {
      const auto* server = &context.state->lobby.servers[index];
      const auto selected = static_cast<int>(index) == context.state->lobby.selected_server_index;
      context.renderer->draw_text(dialog_rect_.x + 83,
                                  dialog_rect_.y + server_top(visible_count) +
                                      static_cast<int>(index) * 42 + 9,
                                  widen(server->name),
                                  selected ? 0xFFFFFF66U : 0xFFF5F7FAU);
    }
  }

  ui::UiTree& ui_tree() override { return ui_; }

 private:
  void select_server(const std::size_t index) {
    if (app_ == nullptr || state_ == nullptr || index >= state_->lobby.servers.size()) {
      return;
    }
    state_->lobby.selected_server_index = static_cast<int>(index);
    state_->lobby.selected_server_name = state_->lobby.servers[index].name;
    app_->request_select_server(state_->lobby.servers[index].name);
  }

  static int server_top(const std::size_t visible_count) {
    return 235 - static_cast<int>(42 * visible_count) / 2;
  }

  static constexpr std::size_t kMaxServerButtons = 8;

  ClientApp* app_{nullptr};
  GameStateStore* state_{nullptr};
  ui::UiTree ui_{};
  RectI dialog_rect_{};
  HotspotButton* server_close_button_{nullptr};
  std::vector<HotspotButton*> server_buttons_{};
};

/// 角色选择场景：显示角色列表，支持创建/删除角色
/// 包含角色动画预览（待机/特效/冻结帧）
/// 创建角色对话框支持职业、性别、发型选择
class CharacterSelectScene final : public Scene {
 public:
  void enter(ClientContext& context) override {
    app_ = context.app;
    state_ = context.state;
    config_ = context.config;
    audio_ = context.audio;
    renderer_ = context.renderer;
    dialog_hint_.clear();
    create_dialog_active_ = false;
    create_slot_index_ = 0;
    create_job_ = config_ != nullptr ? static_cast<std::uint8_t>(config_->auto_play.job % 3) : 0;
    create_sex_ = config_ != nullptr ? static_cast<std::uint8_t>(config_->auto_play.sex % 2) : 0;
    create_hair_ = config_ != nullptr ? static_cast<std::uint8_t>(config_->auto_play.hair % 5) : 0;
    ui_.clear();
    create_edit_font();
    auto* root = ui_.set_root<ui::UiNode>(RectI{0, 0, 800, 600});

    select_left_button_ =
        add_sprite_button(root, context, ArchiveId::prguse, kSelectLeftButtonIndex, 133, 453);
    bind_audio_click(select_left_button_, context.audio, LegacyClickSound::normal,
                     [this] { select_slot(0); });

    select_right_button_ =
        add_sprite_button(root, context, ArchiveId::prguse, kSelectRightButtonIndex, 685, 454);
    bind_audio_click(select_right_button_, context.audio, LegacyClickSound::normal,
                     [this] { select_slot(1); });

    start_button_ =
        add_sprite_button(root, context, ArchiveId::prguse, kSelectStartButtonIndex, 385, 456);
    bind_audio_click(start_button_, context.audio, LegacyClickSound::stone, [this] {
      if (app_ != nullptr) {
        app_->request_selected_character_enter();
      }
    });

    new_button_ =
        add_sprite_button(root, context, ArchiveId::prguse, kSelectNewButtonIndex, 348, 486);
    bind_audio_click(new_button_, context.audio, LegacyClickSound::normal,
                     [this] { open_create_dialog(); });

    erase_button_ =
        add_sprite_button(root, context, ArchiveId::prguse, kSelectEraseButtonIndex, 347, 506);
    bind_audio_click(erase_button_, context.audio, LegacyClickSound::normal,
                     [this] { request_delete_selected_if_ready(); });

    create_overlay_ = root->emplace_child<ui::UiNode>(RectI{0, 0, 800, 600});
    create_overlay_->visible = false;
    create_name_edit_ = create_overlay_->emplace_child<ResourceTextEdit>(RectI{0, 0, 137, 20});
    create_name_edit_->visible = false;
    create_name_edit_->on_submit = [this] { confirm_create_dialog(); };

    create_job_buttons_[0] =
        add_sprite_button(create_overlay_, context, ArchiveId::prguse, kCreateWarriorButtonIndex, 0,
                          0);
    bind_audio_click(create_job_buttons_[0], context.audio, LegacyClickSound::stone,
                     [this] { create_job_ = 0; });
    create_job_buttons_[1] =
        add_sprite_button(create_overlay_, context, ArchiveId::prguse, kCreateWizardButtonIndex, 0,
                          0);
    bind_audio_click(create_job_buttons_[1], context.audio, LegacyClickSound::stone,
                     [this] { create_job_ = 1; });
    create_job_buttons_[2] =
        add_sprite_button(create_overlay_, context, ArchiveId::prguse, kCreateTaoistButtonIndex, 0,
                          0);
    bind_audio_click(create_job_buttons_[2], context.audio, LegacyClickSound::stone,
                     [this] { create_job_ = 2; });

    create_sex_buttons_[0] =
        add_sprite_button(create_overlay_, context, ArchiveId::prguse, kCreateMaleButtonIndex, 0,
                          0);
    bind_audio_click(create_sex_buttons_[0], context.audio, LegacyClickSound::stone,
                     [this] { create_sex_ = 0; });
    create_sex_buttons_[1] =
        add_sprite_button(create_overlay_, context, ArchiveId::prguse, kCreateFemaleButtonIndex, 0,
                          0);
    bind_audio_click(create_sex_buttons_[1], context.audio, LegacyClickSound::stone,
                     [this] { create_sex_ = 1; });

    create_prev_hair_button_ =
        add_sprite_button(create_overlay_, context, ArchiveId::prguse, kCreatePrevHairButtonIndex, 0,
                          0);
    bind_audio_click(create_prev_hair_button_, context.audio, LegacyClickSound::stone, [this] {
      create_hair_ = static_cast<std::uint8_t>((create_hair_ + 4) % 5);
    });
    create_next_hair_button_ =
        add_sprite_button(create_overlay_, context, ArchiveId::prguse, kCreateNextHairButtonIndex, 0,
                          0);
    bind_audio_click(create_next_hair_button_, context.audio, LegacyClickSound::stone, [this] {
      create_hair_ = static_cast<std::uint8_t>((create_hair_ + 1) % 5);
    });
    create_ok_button_ =
        add_sprite_button(create_overlay_, context, ArchiveId::prguse, kLoginSubmitButtonIndex, 0,
                          0);
    bind_audio_click(create_ok_button_, context.audio, LegacyClickSound::stone,
                     [this] { confirm_create_dialog(); });
    create_close_button_ =
        add_sprite_button(create_overlay_, context, ArchiveId::prguse, kLoginCloseButtonIndex, 0,
                          0);
    bind_audio_click(create_close_button_, context.audio, LegacyClickSound::stone,
                     [this] { close_create_dialog(); });
    create_dialog_template_ = sprite_rect(get_frame(context, ArchiveId::prguse, kCreateDialogIndex),
                                          0, 0, 310, 400);
    create_job_button_template_ =
        select_button_bounds(context, 0, 0, kCreateWarriorButtonIndex);
    create_sex_button_template_ =
        select_button_bounds(context, 0, 0, kCreateMaleButtonIndex);
    create_prev_hair_button_template_ =
        select_button_bounds(context, 0, 0, kCreatePrevHairButtonIndex);
    create_next_hair_button_template_ =
        select_button_bounds(context, 0, 0, kCreateNextHairButtonIndex);
    create_ok_button_template_ =
        select_button_bounds(context, 0, 0, kLoginSubmitButtonIndex);
    create_close_button_template_ =
        select_button_bounds(context, 0, 0, 64);
    if (app_ != nullptr && app_->window_handle() != nullptr && edit_font_ != nullptr) {
      create_name_edit_->attach_native(app_->window_handle(), edit_font_, 14, false, false, true);
      create_name_edit_->set_native_visible(false);
    }
    reset_character_visuals(*context.state, detail::monotonic_ms());
    if (context.audio != nullptr) {
      context.audio->play_bgm(bmg_select);
    }
  }

  void exit(ClientContext& context) override {
    if (context.audio != nullptr) {
      context.audio->silence();
    }
    if (create_name_edit_ != nullptr) {
      create_name_edit_->detach_native();
    }
    destroy_edit_font();
    app_ = nullptr;
    state_ = nullptr;
    config_ = nullptr;
    audio_ = nullptr;
    renderer_ = nullptr;
  }

  void update(ClientContext& context, float /*delta_seconds*/) override {
    if (create_name_edit_ != nullptr) {
      create_name_edit_->sync_from_native();
      create_name_edit_->sync_native_bounds(context.renderer);
      create_name_edit_->set_native_visible(create_dialog_active_ && !context.state->modal.visible);
    }
    normalize_selected_index(*context.state);
    const auto now_ms = detail::monotonic_ms();
    sync_character_visuals(*context.state, now_ms);
    character_visuals_.update(now_ms);
  }

  void render(ClientContext& context) override {
    draw_archive_sprite(context, ArchiveId::prguse, kSelectBackgroundIndex, 0, 0);

    const auto now_ms = detail::monotonic_ms();
    for (std::size_t index = 0; index < 2; ++index) {
      if (index < context.state->lobby.characters.size()) {
        draw_select_character(context, context.state->lobby.characters[index],
                              static_cast<int>(index),
                              character_visuals_.pose_for(static_cast<int>(index)));
        continue;
      }

      if (create_dialog_active_ && static_cast<int>(index) == create_slot_index_) {
        client_v1::CharacterSummary preview;
        preview.name = narrow(create_name_edit_ != nullptr ? create_name_edit_->value : std::wstring{});
        preview.level = 1;
        preview.job = create_job_;
        preview.sex = create_sex_;
        preview.hair = create_hair_;
        const auto preview_pose = CharacterSelectPose{
            CharacterSelectPoseKind::idle,
            static_cast<int>((now_ms / static_cast<std::uint64_t>(kCharacterSelectIdleFrameMs)) %
                             static_cast<std::uint64_t>(kCharacterSelectSelectedFrameCount)),
            0, false};
        draw_select_character(context, preview, static_cast<int>(index), preview_pose);
        continue;
      }

    }

    const auto server_name = selected_server_text(context.state->lobby);
    context.renderer->draw_text(400 - static_cast<int>(server_name.size()) * 4, 8, server_name,
                                0xFFF5F7FAU);
    if (create_dialog_active_) {
      render_create_dialog(context);
    }
  }

  ui::UiTree& ui_tree() override { return ui_; }

 private:
  void select_slot(const int index) {
    if (state_ == nullptr || index < 0 || index >= static_cast<int>(state_->lobby.characters.size())) {
      return;
    }
    const auto now_ms = detail::monotonic_ms();
    const auto selected_changed = state_->lobby.selected_index != index;
    const auto changed =
        character_visuals_.select_slot(index, visible_character_count(*state_), now_ms);
    if (selected_changed && changed && audio_ != nullptr) {
      audio_->play_sound(s_meltstone);
    }
    visual_selected_index_ = index;
    state_->lobby.selected_index = index;
  }

  void request_delete_selected_if_ready() {
    if (state_ == nullptr || app_ == nullptr) {
      return;
    }
    const auto selected = state_->lobby.selected_index;
    const auto count = visible_character_count(*state_);
    if (!character_visuals_.can_delete(selected, count)) {
      return;
    }
    if (selected < 0 || selected >= count ||
        state_->lobby.characters[static_cast<std::size_t>(selected)].name.empty()) {
      return;
    }
    app_->request_delete_selected_character();
  }

  static int visible_character_count(const GameStateStore& state) {
    return std::min(kCharacterSelectSlotCount,
                    static_cast<int>(state.lobby.characters.size()));
  }

  static void normalize_selected_index(GameStateStore& state) {
    const auto count = visible_character_count(state);
    if (count == 0) {
      state.lobby.selected_index = -1;
      return;
    }
    if (state.lobby.selected_index < 0 || state.lobby.selected_index >= count) {
      state.lobby.selected_index = 0;
    }
  }

  void reset_character_visuals(const GameStateStore& state, const std::uint64_t now_ms) {
    const auto count = visible_character_count(state);
    character_visuals_.reset(count, state.lobby.selected_index, now_ms);
    visual_character_count_ = count;
    visual_selected_index_ = state.lobby.selected_index;
    visual_slot_names_.fill({});
    for (int index = 0; index < count; ++index) {
      visual_slot_names_[static_cast<std::size_t>(index)] =
          state.lobby.characters[static_cast<std::size_t>(index)].name;
    }
  }

  void sync_character_visuals(const GameStateStore& state, const std::uint64_t now_ms) {
    const auto count = visible_character_count(state);
    auto changed = count != visual_character_count_;
    for (int index = 0; index < kCharacterSelectSlotCount; ++index) {
      const auto name = index < count ? state.lobby.characters[static_cast<std::size_t>(index)].name
                                      : std::string{};
      if (visual_slot_names_[static_cast<std::size_t>(index)] != name) {
        changed = true;
      }
    }
    if (changed) {
      reset_character_visuals(state, now_ms);
      return;
    }
    if (state.lobby.selected_index != visual_selected_index_) {
      (void)character_visuals_.select_slot(state.lobby.selected_index, count, now_ms);
      visual_selected_index_ = state.lobby.selected_index;
    }
  }

  void open_create_dialog() {
    if (state_ == nullptr || create_overlay_ == nullptr || create_name_edit_ == nullptr) {
      return;
    }

    if (state_->lobby.characters.size() >= 2) {
      dialog_hint_ = L"All character slots are already occupied.";
      return;
    }

    create_slot_index_ = 0;
    if (!state_->lobby.characters.empty() && state_->lobby.characters.front().name != "") {
      create_slot_index_ = 1;
    }
    create_popup_rect_ = RectI{create_slot_index_ == 0 ? 415 : 75, 15,
                               create_dialog_template_.w, create_dialog_template_.h};
    layout_create_dialog();
    create_dialog_active_ = true;
    dialog_hint_.clear();
    create_job_ = config_ != nullptr ? static_cast<std::uint8_t>(config_->auto_play.job % 3) : 0;
    create_sex_ = config_ != nullptr ? static_cast<std::uint8_t>(config_->auto_play.sex % 2) : 0;
    create_hair_ = config_ != nullptr ? static_cast<std::uint8_t>(config_->auto_play.hair % 5) : 0;
    create_name_edit_->value.clear();
    create_name_edit_->sync_to_native();
    create_overlay_->visible = true;
    create_name_edit_->visible = true;
    create_name_edit_->sync_native_bounds(renderer_);
    create_name_edit_->set_native_visible(true);
    ui_.focus(create_name_edit_);
  }

  void close_create_dialog() {
    create_dialog_active_ = false;
    dialog_hint_.clear();
    if (create_overlay_ != nullptr) {
      create_overlay_->visible = false;
    }
    if (create_name_edit_ != nullptr) {
      create_name_edit_->value.clear();
      create_name_edit_->sync_to_native();
      create_name_edit_->set_native_visible(false);
      create_name_edit_->visible = false;
    }
  }

  void confirm_create_dialog() {
    if (!create_dialog_active_ || app_ == nullptr || create_name_edit_ == nullptr) {
      return;
    }

    create_name_edit_->sync_from_native();
    const auto name = narrow(create_name_edit_->value);
    if (name.empty()) {
      dialog_hint_ = L"Please enter a character name.";
      ui_.focus(create_name_edit_);
      return;
    }

    dialog_hint_.clear();
    app_->request_create_character(name, create_job_, create_sex_, create_hair_);
    close_create_dialog();
  }

  void layout_create_dialog() {
    if (create_name_edit_ == nullptr || create_job_buttons_[0] == nullptr || create_sex_buttons_[0] == nullptr ||
        create_prev_hair_button_ == nullptr || create_next_hair_button_ == nullptr ||
        create_ok_button_ == nullptr || create_close_button_ == nullptr) {
      return;
    }

    const auto popup = create_popup_rect_;
    create_name_edit_->bounds = RectI{popup.x + 71, popup.y + 107, 137, 20};
    create_name_edit_->sync_native_bounds(renderer_);
    create_job_buttons_[0]->bounds = RectI{popup.x + 48, popup.y + 157,
                                           create_job_button_template_.w, create_job_button_template_.h};
    create_job_buttons_[1]->bounds = RectI{popup.x + 93, popup.y + 157,
                                           create_job_button_template_.w, create_job_button_template_.h};
    create_job_buttons_[2]->bounds = RectI{popup.x + 138, popup.y + 157,
                                           create_job_button_template_.w, create_job_button_template_.h};
    create_sex_buttons_[0]->bounds = RectI{popup.x + 93, popup.y + 231,
                                           create_sex_button_template_.w, create_sex_button_template_.h};
    create_sex_buttons_[1]->bounds = RectI{popup.x + 138, popup.y + 231,
                                           create_sex_button_template_.w, create_sex_button_template_.h};
    create_prev_hair_button_->bounds = RectI{popup.x + 76, popup.y + 308,
                                             create_prev_hair_button_template_.w,
                                             create_prev_hair_button_template_.h};
    create_next_hair_button_->bounds = RectI{popup.x + 170, popup.y + 308,
                                             create_next_hair_button_template_.w,
                                             create_next_hair_button_template_.h};
    create_ok_button_->bounds = RectI{popup.x + 102, popup.y + 359,
                                      create_ok_button_template_.w, create_ok_button_template_.h};
    create_close_button_->bounds = RectI{popup.x + 248, popup.y + 31,
                                         create_close_button_template_.w, create_close_button_template_.h};
  }

  void render_create_dialog(ClientContext& context) {
    draw_archive_sprite(context, ArchiveId::prguse, kCreateDialogIndex, create_popup_rect_.x,
                        create_popup_rect_.y);
    if (create_job_buttons_[create_job_] != nullptr) {
      const auto selected_index = create_job_ == 0   ? kCreateWarriorSelectedIndex
                                  : create_job_ == 1 ? kCreateWizardSelectedIndex
                                                     : kCreateTaoistSelectedIndex;
      const auto rect = create_job_buttons_[create_job_]->bounds;
      draw_archive_sprite(context, ArchiveId::prguse, selected_index, rect.x, rect.y);
    }
    if (create_sex_buttons_[create_sex_] != nullptr) {
      const auto rect = create_sex_buttons_[create_sex_]->bounds;
      draw_archive_sprite(context, ArchiveId::prguse,
                          create_sex_ == 0 ? kCreateMaleSelectedIndex : kCreateFemaleSelectedIndex,
                          rect.x, rect.y);
    }
  }

  void create_edit_font() {
    destroy_edit_font();
    if (app_ == nullptr || app_->window_handle() == nullptr) {
      return;
    }
    const auto dc = GetDC(app_->window_handle());
    const auto font_height = -MulDiv(10, GetDeviceCaps(dc, LOGPIXELSY), 72);
    ReleaseDC(app_->window_handle(), dc);
    edit_font_ = CreateFontW(font_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"MS Sans Serif");
  }

  void destroy_edit_font() {
    if (edit_font_ != nullptr) {
      DeleteObject(edit_font_);
      edit_font_ = nullptr;
    }
  }

  ClientApp* app_{nullptr};
  GameStateStore* state_{nullptr};
  ClientConfig* config_{nullptr};
  AudioService* audio_{nullptr};
  SoftwareRenderer* renderer_{nullptr};
  ui::UiTree ui_{};
  HotspotButton* select_left_button_{nullptr};
  HotspotButton* select_right_button_{nullptr};
  HotspotButton* start_button_{nullptr};
  HotspotButton* new_button_{nullptr};
  HotspotButton* erase_button_{nullptr};
  CharacterSelectVisualState character_visuals_{};
  std::array<std::string, kCharacterSelectSlotCount> visual_slot_names_{};
  int visual_character_count_{0};
  int visual_selected_index_{-1};
  ui::UiNode* create_overlay_{nullptr};
  ResourceTextEdit* create_name_edit_{nullptr};
  std::array<HotspotButton*, 3> create_job_buttons_{};
  std::array<HotspotButton*, 2> create_sex_buttons_{};
  HotspotButton* create_prev_hair_button_{nullptr};
  HotspotButton* create_next_hair_button_{nullptr};
  HotspotButton* create_ok_button_{nullptr};
  HotspotButton* create_close_button_{nullptr};
  RectI create_popup_rect_{};
  RectI create_dialog_template_{0, 0, 310, 400};
  RectI create_job_button_template_{0, 0, 40, 24};
  RectI create_sex_button_template_{0, 0, 40, 24};
  RectI create_prev_hair_button_template_{0, 0, 40, 24};
  RectI create_next_hair_button_template_{0, 0, 40, 24};
  RectI create_ok_button_template_{0, 0, 88, 28};
  RectI create_close_button_template_{0, 0, 88, 28};
  std::wstring dialog_hint_{};
  bool create_dialog_active_{false};
  int create_slot_index_{0};
  std::uint8_t create_job_{0};
  std::uint8_t create_sex_{0};
  std::uint8_t create_hair_{0};
  HFONT edit_font_{nullptr};
};

/// 加载场景：过渡场景，在资源加载完成后自动切换到下一场景
class LoadingScene final : public Scene {
 public:
  void enter(ClientContext& /*context*/) override {}
  void exit(ClientContext& /*context*/) override {}
  void update(ClientContext& /*context*/, float /*delta_seconds*/) override {}
  void render(ClientContext& /*context*/) override {}
  ui::UiTree& ui_tree() override { return ui_; }

 private:
  ui::UiTree ui_{};
};

/// 登录公告场景：显示服务端公告，玩家确认后方可进入游戏
class LoginNoticeScene final : public Scene {
 public:
  void enter(ClientContext& context) override {
    app_ = context.app;
    state_ = context.state;
    ui_.clear();
    message_rect_ =
        centered_rect(get_frame(context, ArchiveId::prguse, kMessageDialogIndex), 800, 600, 360, 180);
    auto* root = ui_.set_root<ui::UiNode>(RectI{0, 0, 800, 600});
    auto* ok_button =
        add_sprite_button(root, context, ArchiveId::prguse, kMessageOkButtonIndex,
                          message_rect_.x + (message_rect_.w - 88) / 2,
                          message_rect_.y + 126, 88, 28);
    bind_audio_click(ok_button, context.audio, LegacyClickSound::stone, [this] {
      if (app_ != nullptr) {
        app_->acknowledge_login_notice();
      }
    });
  }

  void exit(ClientContext& /*context*/) override {
    app_ = nullptr;
    state_ = nullptr;
  }

  void update(ClientContext& /*context*/, float /*delta_seconds*/) override {}

  void render(ClientContext& context) override {
    draw_archive_sprite(context, ArchiveId::chr_sel, kLoginBackgroundIndex, 0, 0);
    draw_archive_sprite(context, ArchiveId::prguse, kMessageDialogIndex, message_rect_.x,
                        message_rect_.y);
    const auto title = context.state->login_notice.title.empty()
                           ? std::wstring{}
                           : widen(context.state->login_notice.title);
    const auto text = context.state->login_notice.text.empty()
                          ? std::wstring{}
                          : widen(context.state->login_notice.text);
    if (!title.empty()) {
      context.renderer->draw_text(message_rect_.x + 34, message_rect_.y + 34, title,
                                  0xFFF5F7FAU);
    }
    if (!text.empty()) {
      context.renderer->draw_text(message_rect_.x + 34, message_rect_.y + 70, text,
                                  0xFFD7E0EAU);
    }
  }

  ui::UiTree& ui_tree() override { return ui_; }

 private:
  ClientApp* app_{nullptr};
  GameStateStore* state_{nullptr};
  ui::UiTree ui_{};
  RectI message_rect_{};
};

/// 世界场景：游戏主场景
/// 职责：地图渲染（瓦片/物件/角色分层）、输入处理（移动/攻击/施法）、
///       角色动画和特效管理、LegacyHud 交互
/// 渲染顺序：背景瓦片 → 小物件 → 地面特效 → 逐行大物件/掉落物/角色/飞行特效 → 覆盖特效
class WorldScene final : public Scene {
 public:
  void enter(ClientContext& context) override {
    if (context.audio != nullptr) {
      context.audio->silence();
    }
    legacy_hud_.initialize(context, ui_);
    sync_map(context);
    const auto now_ms = detail::monotonic_ms();
    animation_.reset(now_ms);
    animation_.sync_world(context.state->world, now_ms);
    audio_cues_.reset();
    main_theme_due_ = true;
    main_theme_elapsed_ms_ = 0;
  }
  void exit(ClientContext& context) override {
    if (context.audio != nullptr) {
      context.audio->silence();
    }
    map_.reset();
    loaded_map_id_.clear();
    animation_.reset();
    audio_cues_.reset();
    legacy_hud_.reset();
    ui_.clear();
    next_left_hold_ms_ = 0;
    next_right_hold_ms_ = 0;
    main_theme_due_ = true;
    main_theme_elapsed_ms_ = 0;
  }

  void update(ClientContext& context, float delta_seconds) override {
    sync_map(context);
    legacy_hud_.sync(context);
    auto& world = context.state->world;
    const auto now_ms = detail::monotonic_ms();
    animation_.update(world, now_ms);
    if (context.audio != nullptr) {
      update_main_theme(*context.audio, delta_seconds, now_ms);
      audio_cues_.update(world, animation_, map_.get(), *context.audio, now_ms);
      context.audio->flush_queued_sounds(now_ms);
    }
    const auto& input = *context.input;
    if (world.self_actor_id == 0) {
      return;
    }
    auto it = world.actors.find(world.self_actor_id);
    if (it == world.actors.end()) {
      return;
    }

    if (world.action_locked && now_ms - world.action_lock_started_ms > 10000U) {
      world.action_locked = false;
    }

    const auto legacy_input = make_legacy_input(context, it->second, now_ms);
    world.focus_actor_id = focused_actor_at(context, legacy_input);
    world.focus_ground_item_id = focused_ground_item_at(context, legacy_input.map_x,
                                                        legacy_input.map_y);
    clear_invalid_targets(world);
    if (legacy_trace_enabled() &&
        (input.left_pressed || input.right_pressed || input.left_released ||
         input.right_released || input.key_pressed[VK_LEFT] || input.key_pressed[VK_RIGHT] ||
         input.key_pressed[VK_UP] || input.key_pressed[VK_DOWN])) {
      std::ostringstream out;
      out << "world_input now=" << now_ms << " mouse=" << legacy_input.mouse_x << ','
          << legacy_input.mouse_y << " map=" << legacy_input.map_x << ',' << legacy_input.map_y
          << " left_pressed=" << input.left_pressed << " right_pressed=" << input.right_pressed
          << " left_released=" << input.left_released
          << " focus_actor=" << world.focus_actor_id
          << " focus_ground=" << world.focus_ground_item_id
          << " ui_consumed=" << context.ui_input.consumed
          << " text_focus=" << context.ui_input.text_focus
          << " dragging=" << context.ui_input.dragging
          << " moving_item=" << world.moving_item.active;
      legacy_trace(out.str());
    }

    if (legacy_hud_.process_pending_actions(context)) {
      return;
    }

    if (legacy_hud_.handle_shortcuts(context, ui_)) {
      return;
    }
    if (legacy_hud_.blocks_world_input()) {
      world.legacy_target_x = -1;
      world.legacy_target_y = -1;
      world.legacy_chr_action = LegacyChrAction::none;
      world.target_actor_id = 0;
      world.action_key = -1;
      return;
    }

    const auto input_guard =
        context.ui_input.consumed || context.ui_input.text_focus || context.ui_input.dragging;
    if (!input_guard && input.key_pressed['R']) {
      context.app->request_reselect_character();
      return;
    }
    if (!input_guard && input.key_pressed[VK_ESCAPE]) {
      world.legacy_target_x = -1;
      world.legacy_target_y = -1;
      world.legacy_chr_action = LegacyChrAction::none;
      world.target_actor_id = 0;
      world.action_key = -1;
      return;
    }

    if (!input_guard) {
      collect_keyboard_ops(context, legacy_input, it->second);
      collect_mouse_ops(context, legacy_input, it->second);
    }

    if (debug_arrow_move_enabled() && !input_guard) {
      if (handle_debug_arrow_move(context, it->second)) {
        return;
      }
    }

    if (process_pending_magic(context, legacy_input)) {
      return;
    }
    if (process_pending_attack(context, now_ms)) {
      return;
    }
    if (process_pending_pickup(context, now_ms)) {
      return;
    }
    if (process_pending_move(context, now_ms)) {
      return;
    }
  }

  /// 渲染世界场景（等距视角的层次绘制管线）
  ///
  /// 可视区域计算：
  ///   以玩家角色为中心，前后左右各取 kViewHalfWidth(9) 列和
  ///   kViewTopRows(9)/kViewBottomRows(8) 行，形成 19×18 的视口。
  ///   这个尺寸与经典 Delphi 客户端的可视范围一致。
  ///
  /// 渲染层级（从下到上，确保正确的遮挡关系）：
  ///   1. 地面砖块（tiles）—— 最底层，瓦片纹理
  ///   2. 小物件（small objects）—— 树桩、石头等低矮物体
  ///   3. 地面特效（ground effects）—— 火墙、毒云等
  ///   4. 逐行大物件/掉落物/角色/飞行特效 —— 复刻 PlayScn.pas 行内顺序
  ///   5. 叠加特效（overlay effects）—— 不受视角限制的辅助特效
  void render(ClientContext& context) override {
    // Delphi play/map surfaces are cleared with black before the visible map is copied in.
    context.renderer->fill_rect(
        RectI{0, 0, context.renderer->logical_width(), context.renderer->logical_height()},
        0xFF000000U);

    const auto self_it = context.state->world.actors.find(context.state->world.self_actor_id);
    if (self_it == context.state->world.actors.end()) {
      return;  // 无自身角色，地图无法渲染（视口定位需要玩家坐标）
    }

    if (map_ == nullptr) {
      return;  // 地图数据未加载（可能在 map_id 切换间隙）
    }

    // 每帧同步一次世界状态到动画管理器（更新角色动作和位置）
    animation_.sync_world(context.state->world, detail::monotonic_ms());

    const auto viewport = viewport_for_self(self_it->second);

    render_tiles(context, viewport);
    render_small_objects(context, viewport);
    if (context.assets != nullptr) {
      animation_.effects().render_ground(*context.assets, *context.renderer, viewport);
    }
    render_world_rows(context, viewport);
    render_map_debug_overlay(context, viewport);
    if (context.assets != nullptr) {
      animation_.effects().render_overlay(*context.assets, *context.renderer, viewport);
    }

  }

  ui::UiTree& ui_tree() override { return ui_; }

 private:
  /// 旧版输入帧：封装鼠标/键盘状态和地图坐标映射
  struct LegacyInputFrame {
    std::uint64_t tick{0};
    int mouse_x{0};
    int mouse_y{0};
    int map_x{0};
    int map_y{0};
    bool left_pressed{false};
    bool left_released{false};
    bool left_down{false};
    bool right_pressed{false};
    bool right_released{false};
    bool right_down{false};
    bool left_long_press{false};
    bool right_long_press{false};
    bool shift{false};
    bool ctrl{false};
    bool alt{false};
  };

  void update_main_theme(AudioService& audio, const float delta_seconds,
                         const std::uint64_t now_ms) {
    if (main_theme_due_) {
      audio.queue_sound(s_main_theme, now_ms);
      main_theme_due_ = false;
      main_theme_elapsed_ms_ = 0;
      return;
    }

    if (delta_seconds > 0.0F) {
      main_theme_elapsed_ms_ +=
          static_cast<std::uint64_t>(static_cast<double>(delta_seconds) * 1000.0);
    }
    if (main_theme_elapsed_ms_ >= kWorldMainThemeIntervalMs) {
      audio.queue_sound(s_main_theme, now_ms);
      main_theme_elapsed_ms_ = 0;
    }
  }

  static std::uint8_t direction_between(int sx, int sy, int dx, int dy,
                                        std::uint8_t fallback) {
    if (sx == dx && sy == dy) {
      return fallback;
    }
    return legacy::next_direction(sx, sy, dx, dy);
  }

  std::pair<int, int> step_toward(const ActorState& self, int x, int y, bool running) const {
    const auto width = map_ != nullptr ? map_->width : 0;
    const auto height = map_ != nullptr ? map_->height : 0;
    const auto target = running ? legacy::requested_run_target(width, height, self.x, self.y, x, y)
                                : legacy::requested_walk_target(width, height, self.x, self.y, x, y);
    if (!target.has_value()) {
      return {self.x, self.y};
    }
    return {target->x, target->y};
  }

  legacy::LegacyMapViewport viewport_for_self(const ActorState& self) const {
    const auto pose = animation_.pose_for(self.actor_id);
    if (pose.has_value()) {
      return legacy::make_legacy_map_viewport(pose->rx, pose->ry, pose->shift_x, pose->shift_y);
    }
    return legacy::make_legacy_map_viewport(self.x, self.y);
  }

  std::pair<int, int> screen_to_map_tile(ClientContext& context, const ActorState& self) const {
    const auto fallback_width = map_ != nullptr ? map_->width : 0;
    const auto fallback_height = map_ != nullptr ? map_->height : 0;
    const auto map_width =
        context.state->world.width > 0 ? context.state->world.width : fallback_width;
    const auto map_height =
        context.state->world.height > 0 ? context.state->world.height : fallback_height;
    return legacy::legacy_mouse_to_map_clamped(viewport_for_self(self), context.input->mouse_x,
                                               context.input->mouse_y, map_width, map_height);
  }

  std::uint64_t focused_actor_at(ClientContext& context, const LegacyInputFrame& input) const {
    std::uint64_t best_pixel_id = 0;
    int best_pixel_y = -1;
    for (const auto& [actor_id, actor] : context.state->world.actors) {
      if (actor_id == context.state->world.self_actor_id || actor.dead) {
        continue;
      }
      if (actor_pixel_hit(context, actor, input.mouse_x, input.mouse_y) && actor.y >= best_pixel_y) {
        best_pixel_id = actor_id;
        best_pixel_y = actor.y;
      }
    }
    if (best_pixel_id != 0) {
      return best_pixel_id;
    }

    std::uint64_t best_id = 0;
    int best_distance = 3;
    for (const auto& [actor_id, actor] : context.state->world.actors) {
      if (actor_id == context.state->world.self_actor_id || actor.dead) {
        continue;
      }
      const auto distance = std::max(std::abs(actor.x - input.map_x),
                                     std::abs(actor.y - input.map_y));
      if (distance < best_distance) {
        best_id = actor_id;
        best_distance = distance;
      }
    }
    return best_distance <= 1 ? best_id : 0;
  }

  std::uint64_t focused_ground_item_at(ClientContext& context, int x, int y) const {
    std::uint64_t best_id = 0;
    for (const auto& [item_id, item] : context.state->world.ground_items) {
      if (item.x == x && item.y == y && (best_id == 0 || item_id < best_id)) {
        best_id = item_id;
      }
    }
    return best_id;
  }

  bool actor_pixel_hit(ClientContext& context, const ActorState& actor, int mouse_x,
                       int mouse_y) const {
    const auto self_it = context.state->world.actors.find(context.state->world.self_actor_id);
    if (self_it == context.state->world.actors.end()) {
      return false;
    }
    if (context.assets == nullptr) {
      return false;
    }
    const auto viewport = viewport_for_self(self_it->second);
    const auto pose = animation_.pose_for(actor.actor_id);
    if (!pose.has_value()) {
      return false;
    }
    const auto base_x = legacy::legacy_actor_base_x(viewport, pose->rx, pose->shift_x);
    const auto base_y = legacy::legacy_actor_base_y(viewport, pose->ry, pose->shift_y);
    const auto body = context.assets->get_frame(pose->body_archive, pose->body_index);
    const auto hair = pose->hair_index >= 0
                          ? context.assets->get_frame(ArchiveId::hair, pose->hair_index)
                          : nullptr;
    const auto weapon = pose->weapon_index >= 0
                            ? context.assets->get_frame(ArchiveId::weapon, pose->weapon_index)
                            : nullptr;
    const auto hit_frame = [&](const std::shared_ptr<const SpriteFrame>& frame, int x, int y) {
      if (frame == nullptr || frame->empty()) {
        return false;
      }
      const auto local_x = mouse_x - x;
      const auto local_y = mouse_y - y;
      if (local_x < 0 || local_y < 0 || local_x >= frame->width || local_y >= frame->height) {
        return false;
      }
      const auto pixel = frame->pixels[static_cast<std::size_t>(local_y) *
                                           static_cast<std::size_t>(frame->width) +
                                       static_cast<std::size_t>(local_x)];
      return ((pixel >> 24U) & 0xFFU) > 16U;
    };

    if (pose->weapon_before_body && weapon != nullptr &&
        hit_frame(weapon, base_x + weapon->hotspot_x, base_y + weapon->hotspot_y)) {
      return true;
    }
    if (hit_frame(body, base_x + (body != nullptr ? body->hotspot_x : 8),
                  base_y + (body != nullptr ? body->hotspot_y : -56))) {
      return true;
    }
    if (hair != nullptr && hit_frame(hair, base_x + hair->hotspot_x, base_y + hair->hotspot_y)) {
      return true;
    }
    if (!pose->weapon_before_body && weapon != nullptr &&
        hit_frame(weapon, base_x + weapon->hotspot_x, base_y + weapon->hotspot_y)) {
      return true;
    }
    return body == nullptr && RectI{base_x + 8, base_y - 56, 32, 64}.contains(mouse_x, mouse_y);
  }

  static std::uint16_t magic_for_slot(const WorldViewState& world, int slot) {
    const auto key = static_cast<std::uint8_t>(slot + 1);
    for (const auto& magic : world.magics) {
      if (magic.key == key) {
        return magic.magic_id;
      }
    }
    return 0;
  }

  static const MagicShortcutState* magic_for_id(const WorldViewState& world,
                                                std::uint16_t magic_id) {
    for (const auto& magic : world.magics) {
      if (magic.magic_id == magic_id) {
        return &magic;
      }
    }
    return nullptr;
  }

  /// 从原始输入构造 LegacyInputFrame（添加长按检测和地图坐标映射）
  LegacyInputFrame make_legacy_input(ClientContext& context, const ActorState& self,
                                     std::uint64_t now_ms) {
    auto& world = context.state->world;
    const auto& input = *context.input;
    const auto tile = screen_to_map_tile(context, self);
    if (input.left_pressed || input.right_pressed) {
      world.mouse_down_ms = now_ms;
      world.run_ready_count = 0;
    }
    if (input.left_pressed) {
      next_left_hold_ms_ = now_ms + 300U;
    }
    if (input.right_pressed) {
      next_right_hold_ms_ = now_ms + 300U;
    }

    LegacyInputFrame frame;
    frame.tick = now_ms;
    frame.mouse_x = input.mouse_x;
    frame.mouse_y = input.mouse_y;
    frame.map_x = tile.first;
    frame.map_y = tile.second;
    frame.left_pressed = input.left_pressed;
    frame.left_released = input.left_released;
    frame.left_down = input.left_down;
    frame.right_pressed = input.right_pressed;
    frame.right_released = input.right_released;
    frame.right_down = input.right_down;
    frame.shift = input.key_down[VK_SHIFT];
    frame.ctrl = input.key_down[VK_CONTROL];
    frame.alt = input.key_down[VK_MENU];

    if (input.left_down && next_left_hold_ms_ != 0 && now_ms >= next_left_hold_ms_) {
      frame.left_long_press = true;
      next_left_hold_ms_ = now_ms + 50U;
    }
    if (input.right_down && next_right_hold_ms_ != 0 && now_ms >= next_right_hold_ms_) {
      frame.right_long_press = true;
      next_right_hold_ms_ = now_ms + 50U;
    }
    if (input.left_released) {
      next_left_hold_ms_ = 0;
    }
    if (input.right_released) {
      next_right_hold_ms_ = 0;
    }
    return frame;
  }

  void clear_invalid_targets(WorldViewState& world) const {
    if (world.focus_actor_id != 0) {
      const auto it = world.actors.find(world.focus_actor_id);
      if (it == world.actors.end() || it->second.dead) {
        world.focus_actor_id = 0;
      }
    }
    if (world.target_actor_id != 0) {
      const auto it = world.actors.find(world.target_actor_id);
      if (it == world.actors.end() || it->second.dead) {
        world.target_actor_id = 0;
      }
    }
    if (world.focus_ground_item_id != 0 &&
        world.ground_items.find(world.focus_ground_item_id) == world.ground_items.end()) {
      world.focus_ground_item_id = 0;
    }
    if (world.pending_pickup_item_id != 0 &&
        world.ground_items.find(world.pending_pickup_item_id) == world.ground_items.end()) {
      world.pending_pickup_item_id = 0;
    }
  }

  void collect_keyboard_ops(ClientContext& context, const LegacyInputFrame& input,
                            const ActorState& self) {
    auto& world = context.state->world;
    for (int index = 0; index < 8; ++index) {
      if (context.input->key_pressed[VK_F1 + index]) {
        if (magic_for_slot(world, index) != 0) {
          world.action_key = index;
          if (legacy_trace_enabled()) {
            std::ostringstream out;
            out << "action_key now=" << input.tick << " slot=" << index;
            legacy_trace(out.str());
          }
        }
      }
    }

    for (int index = 0; index < 6; ++index) {
      if (context.input->key_pressed['1' + index]) {
        if (self.dead || context.app == nullptr || world.pending_item_action.active ||
            !valid_bag_slot(index)) {
          continue;
        }
        const auto item = world.bag_items[static_cast<std::size_t>(index)];
        if (!item_usable_from_bag(item)) {
          continue;
        }
        client_v1::UseItemIntent intent;
        intent.item_slot = index;
        intent.item_make_index = item.make_index;
        intent.name = item.name;
        context.state->world.eating_item_slot = index;
        context.state->world.eating_item_make_index = item.make_index;
        context.state->world.eat_time_ms = input.tick;
        context.state->begin_pending_item_action(PendingItemActionKind::use,
                                                 MovingItemSource::bag, index, index, item,
                                                 input.tick);
        context.state->world.bag_items[static_cast<std::size_t>(index)] =
            client_v1::ItemState{};
        context.app->request_use_item(intent);
        return;
      }
    }
  }

  void collect_mouse_ops(ClientContext& context, const LegacyInputFrame& input,
                         const ActorState& self) {
    auto& world = context.state->world;
    const auto left_action = input.left_pressed || input.left_long_press;
    const auto right_action = input.right_pressed || input.right_long_press;

    if (input.left_released || input.right_released) {
      world.legacy_target_x = -1;
      world.legacy_target_y = -1;
      world.legacy_chr_action = LegacyChrAction::none;
    }

    if (left_action) {
      if (world.focus_actor_id != 0 && world.focus_actor_id != world.self_actor_id) {
        const auto target_it = world.actors.find(world.focus_actor_id);
        if (target_it != world.actors.end() &&
            target_it->second.actor_type == client_v1::ActorType::npc) {
          if (context.app != nullptr) {
            context.app->request_npc_click(world.focus_actor_id);
          }
          world.target_actor_id = 0;
          world.legacy_target_x = -1;
          world.legacy_target_y = -1;
          world.legacy_chr_action = LegacyChrAction::none;
          return;
        }
        world.target_actor_id = world.focus_actor_id;
        try_attack_target(context, world.target_actor_id, input.tick);
        return;
      }
      if (input.shift) {
        try_attack_ground(context, input.map_x, input.map_y, input.tick);
        return;
      }
      if (world.focus_ground_item_id != 0) {
        world.pending_pickup_item_id = world.focus_ground_item_id;
        world.target_actor_id = 0;
        return;
      }
      if (input.map_x == self.x && input.map_y == self.y) {
        try_pickup(context, self, input.tick);
        return;
      }
      if (world.last_attack_ms == 0 || elapsed_ms(input.tick, world.last_attack_ms) > 1000U) {
        set_pending_move(context, input.map_x, input.map_y,
                         input.ctrl ? LegacyChrAction::run : LegacyChrAction::walk);
      }
      return;
    }

    if (right_action) {
      const auto distance = std::max(std::abs(input.map_x - self.x), std::abs(input.map_y - self.y));
      if (distance <= 2) {
        try_turn(context, self, input.map_x, input.map_y, input.tick);
        return;
      }
      set_pending_move(context, input.map_x, input.map_y, LegacyChrAction::run);
      return;
    }
  }

  bool handle_debug_arrow_move(ClientContext& context, const ActorState& self) {
    auto target_x = self.x;
    auto target_y = self.y;
    bool moved = false;
    const auto step = context.input->key_down[VK_SHIFT] ? 2 : 1;
    if (context.input->key_pressed[VK_LEFT]) {
      target_x -= step;
      moved = true;
    }
    if (context.input->key_pressed[VK_RIGHT]) {
      target_x += step;
      moved = true;
    }
    if (context.input->key_pressed[VK_UP]) {
      target_y -= step;
      moved = true;
    }
    if (context.input->key_pressed[VK_DOWN]) {
      target_y += step;
      moved = true;
    }
    if (!moved) {
      return false;
    }
    set_pending_move(context, target_x, target_y,
                     step > 1 ? LegacyChrAction::run : LegacyChrAction::walk);
    return process_pending_move(context, detail::monotonic_ms());
  }

  bool can_walk(ClientContext& context, const ActorState& self, int x, int y) const {
    if (map_ != nullptr) {
      if (!map_->can_move(x, y)) {
        return false;
      }
    } else if (context.state->world.width > 0 && context.state->world.height > 0 &&
               !legacy::in_bounds(context.state->world.width, context.state->world.height, x, y)) {
      return false;
    }

    for (const auto& [actor_id, actor] : context.state->world.actors) {
      if (actor_id == self.actor_id || actor.dead) {
        continue;
      }
      if (actor.x == x && actor.y == y) {
        return false;
      }
    }
    return true;
  }

  bool can_send_move(ClientContext& context, const ActorState& self, int x, int y,
                     bool running) const {
    if (x == self.x && y == self.y) {
      return false;
    }

    const auto now = detail::monotonic_ms();
    if (context.state->world.latest_spell_ms != 0 &&
        now - context.state->world.latest_spell_ms < context.state->world.magic_pk_delay_ms) {
      return false;
    }

    if (running) {
      if (self.hp >= 0 && self.hp < 10) {
        return false;
      }
      if (context.state->world.latest_struck_ms != 0 &&
          now - context.state->world.latest_struck_ms < 3000U) {
        return false;
      }
      const auto dir = legacy::next_direction(self.x, self.y, x, y);
      const auto middle = legacy::step_target(map_ != nullptr ? map_->width : context.state->world.width,
                                              map_ != nullptr ? map_->height : context.state->world.height,
                                              self.x, self.y, dir, 1);
      if (!middle.has_value() || !can_walk(context, self, middle->x, middle->y)) {
        return false;
      }
    }

    return can_walk(context, self, x, y);
  }

  void set_pending_move(ClientContext& context, int x, int y, LegacyChrAction action) const {
    auto& world = context.state->world;
    world.legacy_target_x = x;
    world.legacy_target_y = y;
    world.legacy_chr_action = action;
    if (action != LegacyChrAction::none) {
      world.target_actor_id = 0;
    }
  }

  bool process_pending_move(ClientContext& context, std::uint64_t now_ms) const {
    auto& world = context.state->world;
    if (world.legacy_target_x < 0 || world.legacy_chr_action == LegacyChrAction::none) {
      return false;
    }
    auto self_it = world.actors.find(world.self_actor_id);
    if (self_it == world.actors.end()) {
      return false;
    }
    if (!can_next_action(world, self_it->second, now_ms)) {
      return false;
    }
    const auto running = world.legacy_chr_action == LegacyChrAction::run;
    const auto sent = send_move(context, self_it->second, world.legacy_target_x,
                                world.legacy_target_y, running);
    world.legacy_target_x = -1;
    world.legacy_target_y = -1;
    world.legacy_chr_action = LegacyChrAction::none;
    if (sent) {
      world.last_move_ms = now_ms;
      if (running) {
        ++world.run_ready_count;
      }
    }
    return sent;
  }

  bool send_move(ClientContext& context, const ActorState& self, int x, int y,
                 bool running) const {
    const auto [next_x, next_y] = step_toward(self, x, y, running);
    if (!can_send_move(context, self, next_x, next_y, running)) {
      if (running) {
        const auto [walk_x, walk_y] = step_toward(self, x, y, false);
        if (!can_send_move(context, self, walk_x, walk_y, false)) {
          return false;
        }
        client_v1::ActionIntent action;
        action.kind = client_v1::WorldActionKind::walk;
        action.x = walk_x;
        action.y = walk_y;
        action.dir = direction_between(self.x, self.y, walk_x, walk_y, self.dir);
        context.app->request_action(action);
        return true;
      }
      return false;
    }
    client_v1::ActionIntent action;
    action.kind = running ? client_v1::WorldActionKind::run : client_v1::WorldActionKind::walk;
    action.x = next_x;
    action.y = next_y;
    action.dir = direction_between(self.x, self.y, next_x, next_y, self.dir);
    context.app->request_action(action);
    return true;
  }

  bool try_turn(ClientContext& context, const ActorState& self, int x, int y,
                std::uint64_t now_ms) const {
    auto& world = context.state->world;
    if (!can_next_action(world, self, now_ms)) {
      return false;
    }
    client_v1::ActionIntent action;
    action.kind = client_v1::WorldActionKind::turn;
    action.x = self.x;
    action.y = self.y;
    action.dir = direction_between(self.x, self.y, x, y, self.dir);
    context.app->request_action(action);
    return true;
  }

  bool try_pickup(ClientContext& context, const ActorState& self, std::uint64_t now_ms) const {
    auto& world = context.state->world;
    if (world.last_pickup_ms != 0 && elapsed_ms(now_ms, world.last_pickup_ms) < 250U) {
      return false;
    }
    world.last_pickup_ms = now_ms;
    context.app->request_pickup(client_v1::PickupIntent{self.x, self.y});
    return true;
  }

  bool process_pending_pickup(ClientContext& context, std::uint64_t now_ms) const {
    auto& world = context.state->world;
    if (world.pending_pickup_item_id == 0) {
      return false;
    }
    const auto item_it = world.ground_items.find(world.pending_pickup_item_id);
    if (item_it == world.ground_items.end()) {
      world.pending_pickup_item_id = 0;
      return false;
    }
    auto self_it = world.actors.find(world.self_actor_id);
    if (self_it == world.actors.end()) {
      return false;
    }
    if (self_it->second.x == item_it->second.x && self_it->second.y == item_it->second.y) {
      const auto sent = try_pickup(context, self_it->second, now_ms);
      if (sent) {
        world.pending_pickup_item_id = 0;
      }
      return sent;
    }
    if (!can_next_action(world, self_it->second, now_ms)) {
      return false;
    }
    world.legacy_target_x = item_it->second.x;
    world.legacy_target_y = item_it->second.y;
    world.legacy_chr_action = LegacyChrAction::walk;
    return false;
  }

  bool try_attack_ground(ClientContext& context, int x, int y, std::uint64_t now_ms) const {
    auto& world = context.state->world;
    auto self_it = world.actors.find(world.self_actor_id);
    if (self_it == world.actors.end() || !can_next_action(world, self_it->second, now_ms) ||
        !can_next_hit(world, self_it->second, now_ms)) {
      return false;
    }
    const auto dir = direction_between(self_it->second.x, self_it->second.y, x, y,
                                       self_it->second.dir);
    send_attack(context, self_it->second.x, self_it->second.y, dir, 0, 0);
    world.latest_hit_ms = now_ms;
    world.last_attack_ms = now_ms;
    return true;
  }

  bool process_pending_attack(ClientContext& context, std::uint64_t now_ms) {
    auto& world = context.state->world;
    if (world.target_actor_id == 0) {
      return false;
    }
    return try_attack_target(context, world.target_actor_id, now_ms);
  }

  bool try_attack_target(ClientContext& context, std::uint64_t target_actor_id,
                         std::uint64_t now_ms) {
    auto& world = context.state->world;
    auto self_it = world.actors.find(world.self_actor_id);
    auto target_it = world.actors.find(target_actor_id);
    if (self_it == world.actors.end() || target_it == world.actors.end() || target_it->second.dead) {
      world.target_actor_id = 0;
      return false;
    }

    const auto& self = self_it->second;
    const auto& target = target_it->second;
    const auto distance = std::max(std::abs(target.x - self.x), std::abs(target.y - self.y));
    const auto dir = direction_between(self.x, self.y, target.x, target.y, self.dir);
    if (distance > 1) {
      chase_target(context, self, target, dir);
      return process_pending_move(context, now_ms);
    }

    if (!can_next_action(world, self, now_ms) || !can_next_hit(world, self, now_ms)) {
      world.target_actor_id = target_actor_id;
      return false;
    }

    send_attack(context, self.x, self.y, dir, target_actor_id, 0);
    world.latest_hit_ms = now_ms;
    world.last_attack_ms = now_ms;
    return true;
  }

  void chase_target(ClientContext& context, const ActorState& self, const ActorState& target,
                    std::uint8_t dir) const {
    const auto delta = legacy::direction_delta(dir);
    auto stand_x = target.x - delta.dx;
    auto stand_y = target.y - delta.dy;
    if (!legacy::in_bounds(map_ != nullptr ? map_->width : context.state->world.width,
                           map_ != nullptr ? map_->height : context.state->world.height,
                           stand_x, stand_y) ||
        !can_walk(context, self, stand_x, stand_y)) {
      stand_x = target.x;
      stand_y = target.y;
    }
    auto& world = context.state->world;
    world.legacy_target_x = stand_x;
    world.legacy_target_y = stand_y;
    world.legacy_chr_action = LegacyChrAction::walk;
  }

  bool process_pending_magic(ClientContext& context, const LegacyInputFrame& input) {
    auto& world = context.state->world;
    if (world.action_key < 0 || world.action_key >= 8) {
      return false;
    }
    const auto slot = world.action_key;
    const auto magic_id = magic_for_slot(world, slot);
    if (magic_id == 0) {
      world.action_key = -1;
      return false;
    }
    auto self_it = world.actors.find(world.self_actor_id);
    if (self_it == world.actors.end()) {
      return false;
    }
    const auto* magic = magic_for_id(world, magic_id);
    const auto delay = static_cast<std::uint64_t>(500 + (magic != nullptr ? magic->delay_ms : 0));
    if (world.latest_spell_ms != 0 && elapsed_ms(input.tick, world.latest_spell_ms) < delay) {
      return false;
    }
    if (self_it->second.mp == 0) {
      return false;
    }
    if (!can_next_action(world, self_it->second, input.tick)) {
      return false;
    }
    send_spell(context, self_it->second, input.map_x, input.map_y,
               world.focus_actor_id != 0 ? world.focus_actor_id : world.target_actor_id, magic_id);
    world.action_key = -1;
    return true;
  }

  static void send_attack(ClientContext& context, int x, int y, std::uint8_t dir,
                          std::uint64_t target_actor_id, std::uint16_t legacy_ident) {
    client_v1::ActionIntent action;
    action.kind = client_v1::WorldActionKind::attack;
    action.x = x;
    action.y = y;
    action.dir = dir;
    action.target_actor_id = target_actor_id;
    action.legacy_ident = legacy_ident;
    context.app->request_action(action);
  }

  static void send_spell(ClientContext& context, const ActorState& self, int x, int y,
                         std::uint64_t target_actor_id, std::uint16_t magic_id) {
    client_v1::SpellIntent spell;
    spell.x = x;
    spell.y = y;
    spell.dir = direction_between(self.x, self.y, x, y, self.dir);
    spell.target_actor_id = target_actor_id;
    spell.magic_id = magic_id;
    context.app->request_spell(spell);
  }

  void sync_map(ClientContext& context) {
    if (context.assets == nullptr) {
      return;
    }
    if (loaded_map_id_ == context.state->world.map_id && map_ != nullptr) {
      return;
    }
    loaded_map_id_ = context.state->world.map_id;
    map_ = context.assets->load_map(loaded_map_id_);
    audio_cues_.reset();
  }

  /// 渲染地图瓦片层：背景瓦片（偶数格）+ 中间层物件
  void render_tiles(ClientContext& context, const legacy::LegacyMapViewport& viewport) {
    for (int y = viewport.top - 1; y <= viewport.bottom + 1; ++y) {
      for (int x = viewport.left - 2; x <= viewport.right + 1; ++x) {
        const auto* cell = map_->cell(x, y);
        if (cell == nullptr) {
          continue;
        }

        const auto draw_x = legacy::legacy_tile_draw_x(viewport, x);
        const auto bk_index = legacy::legacy_ground_tile_frame_index(x, y, cell->bk_img);
        if (bk_index >= 0) {
          draw_sprite(*context.renderer, context.assets->get_frame(ArchiveId::tiles, bk_index),
                      draw_x, legacy::legacy_ground_back_y(viewport, y));
        }

        const auto mid_index = legacy::legacy_small_tile_frame_index(cell->mid_img);
        if (mid_index >= 0) {
          draw_sprite(*context.renderer, context.assets->get_frame(ArchiveId::sm_tiles, mid_index),
                      draw_x, legacy::legacy_ground_mid_y(viewport, y));
        }
      }
    }
  }

  void render_small_objects(ClientContext& context, const legacy::LegacyMapViewport& viewport) {
    for (int y = viewport.top; y <= viewport.bottom + legacy::kLegacyLongHeightRows; ++y) {
      for (int x = viewport.left - 2; x <= viewport.right + 2; ++x) {
        const auto* cell = map_->cell(x, y);
        if (cell == nullptr) {
          continue;
        }
        const auto object_index = animation_.map_object_frame(*cell);
        if (object_index < 0) {
          continue;
        }
        const auto frame =
            context.assets->get_frame(object_archive_for_area(cell->area), object_index);
        if (frame == nullptr || frame->width != 48 || frame->height != 32) {
          continue;
        }

        draw_sprite(*context.renderer, frame, legacy::legacy_tile_draw_x(viewport, x),
                    legacy::legacy_object_row_y(viewport, y));
      }
    }
  }

  struct RowActorDraw {
    std::uint64_t actor_id{0};
    const ActorState* actor{nullptr};
    ActorRenderPose pose{};
    int draw_row{0};
  };

  struct RowGroundItemDraw {
    std::uint64_t item_id{0};
    const client_v1::GroundItemState* item{nullptr};
  };

  std::vector<RowActorDraw> collect_row_actor_draws(
      ClientContext& context, const legacy::LegacyMapViewport& viewport) const {
    std::vector<RowActorDraw> actors;
    actors.reserve(context.state->world.actors.size());
    for (const auto& [actor_id, actor] : context.state->world.actors) {
      const auto pose = animation_.pose_for(actor_id);
      if (!pose.has_value()) {
        continue;
      }
      const auto draw_row = legacy::legacy_actor_draw_row(pose->ry, pose->down_draw_level);
      if (draw_row < viewport.top || draw_row > viewport.bottom) {
        continue;
      }
      actors.push_back(RowActorDraw{actor_id, &actor, *pose, draw_row});
    }
    std::stable_sort(actors.begin(), actors.end(), [](const RowActorDraw& left,
                                                      const RowActorDraw& right) {
      if (left.draw_row != right.draw_row) {
        return left.draw_row < right.draw_row;
      }
      if (left.pose.ry != right.pose.ry) {
        return left.pose.ry < right.pose.ry;
      }
      return left.actor_id < right.actor_id;
    });
    return actors;
  }

  void render_world_rows(ClientContext& context, const legacy::LegacyMapViewport& viewport) {
    const auto actors = collect_row_actor_draws(context, viewport);
    auto actor_it = actors.begin();
    for (int y = viewport.top; y <= viewport.bottom + legacy::kLegacyLongHeightRows; ++y) {
      render_large_objects_for_row(context, viewport, y);

      if (y <= viewport.bottom) {
        render_ground_items_for_row(context, viewport, y);
        while (actor_it != actors.end() && actor_it->draw_row < y) {
          ++actor_it;
        }
        for (auto it = actor_it; it != actors.end() && it->draw_row == y; ++it) {
          if (it->actor == nullptr) {
            continue;
          }
          render_actor(context, *it->actor, it->pose, viewport,
                       it->actor_id == context.state->world.self_actor_id);
          if (context.assets != nullptr) {
            animation_.effects().render_overlay_for_actor(it->actor_id, it->pose, *context.assets,
                                                          *context.renderer, viewport);
          }
        }
      }

      if (context.assets != nullptr) {
        animation_.effects().render_fly(*context.assets, *context.renderer, viewport, y);
      }
    }
  }

  void render_map_debug_overlay(ClientContext& context,
                                const legacy::LegacyMapViewport& viewport) const {
    if (!map_debug_overlay_enabled() || context.renderer == nullptr) {
      return;
    }

    constexpr auto kGridColor = 0xFF334155U;
    constexpr auto kCenterColor = 0xFF22C55EU;
    constexpr auto kMouseColor = 0xFFFACC15U;
    constexpr auto kTextColor = 0xFFE0F2FEU;
    auto& renderer = *context.renderer;

    for (int y = viewport.top; y <= viewport.bottom; ++y) {
      for (int x = viewport.left; x <= viewport.right; ++x) {
        renderer.stroke_rect(RectI{legacy::legacy_tile_draw_x(viewport, x),
                                   legacy::legacy_ground_mid_y(viewport, y),
                                   legacy::kLegacyUnitX, legacy::kLegacyUnitY},
                             kGridColor);
      }
    }

    renderer.stroke_rect(RectI{legacy::legacy_tile_draw_x(viewport, viewport.rx),
                               legacy::legacy_ground_mid_y(viewport, viewport.ry),
                               legacy::kLegacyUnitX, legacy::kLegacyUnitY},
                         kCenterColor);

    std::pair<int, int> mouse_tile{-1, -1};
    const auto self_it = context.state->world.actors.find(context.state->world.self_actor_id);
    if (context.input != nullptr && self_it != context.state->world.actors.end()) {
      mouse_tile = screen_to_map_tile(context, self_it->second);
      renderer.stroke_rect(RectI{legacy::legacy_tile_draw_x(viewport, mouse_tile.first),
                                 legacy::legacy_ground_mid_y(viewport, mouse_tile.second),
                                 legacy::kLegacyUnitX, legacy::kLegacyUnitY},
                           kMouseColor);
    }

    renderer.fill_rect(RectI{6, 6, 360, 58}, 0xCC000000U);
    renderer.draw_text(10, 10,
                       L"rx/ry=" + std::to_wstring(viewport.rx) + L"," +
                           std::to_wstring(viewport.ry) + L" shift=" +
                           std::to_wstring(viewport.shift_x) + L"," +
                           std::to_wstring(viewport.shift_y),
                       kTextColor);
    renderer.draw_text(10, 26,
                       L"view=" + std::to_wstring(viewport.left) + L"," +
                           std::to_wstring(viewport.top) + L".." +
                           std::to_wstring(viewport.right) + L"," +
                           std::to_wstring(viewport.bottom) + L" mainAni=" +
                           std::to_wstring(animation_.clock().main_ani_count()),
                       kTextColor);
    renderer.draw_text(10, 42,
                       L"mouse=" + std::to_wstring(mouse_tile.first) + L"," +
                           std::to_wstring(mouse_tile.second),
                       kTextColor);
  }

  void render_large_objects_for_row(ClientContext& context,
                                    const legacy::LegacyMapViewport& viewport, const int y) {
    for (int x = viewport.left - 2; x <= viewport.right + 2; ++x) {
      const auto* cell = map_->cell(x, y);
      if (cell == nullptr) {
        continue;
      }

      const auto object_index = animation_.map_object_frame(*cell);
      if (object_index >= 0) {
        const auto frame =
            context.assets->get_frame(object_archive_for_area(cell->area), object_index);
        if (frame != nullptr && (frame->width != 48 || frame->height != 32)) {
          const auto draw_x = legacy::legacy_tile_draw_x(viewport, x);
          const auto base_y = legacy::legacy_object_row_y(viewport, y);
          if (animation_.map_object_blend(*cell)) {
            draw_sprite_legacy_blend(*context.renderer, frame, draw_x + frame->hotspot_x - 2,
                                     base_y + frame->hotspot_y - 68);
          } else {
            draw_sprite(*context.renderer, frame, draw_x,
                        base_y + legacy::kLegacyUnitY - frame->height);
          }
        }
      }
    }
  }

  void render_ground_items_for_row(ClientContext& context,
                                   const legacy::LegacyMapViewport& viewport, const int row) {
    std::vector<RowGroundItemDraw> items;
    items.reserve(context.state->world.ground_items.size());
    for (const auto& [item_id, item] : context.state->world.ground_items) {
      if (item.y == row) {
        items.push_back(RowGroundItemDraw{item_id, &item});
      }
    }
    std::stable_sort(items.begin(), items.end(), [](const RowGroundItemDraw& left,
                                                    const RowGroundItemDraw& right) {
      if (left.item == nullptr || right.item == nullptr) {
        return left.item != nullptr;
      }
      if (left.item->x != right.item->x) {
        return left.item->x < right.item->x;
      }
      return left.item_id < right.item_id;
    });
    for (const auto& draw : items) {
      if (draw.item != nullptr) {
        render_ground_item(context, viewport, draw.item_id, *draw.item);
      }
    }
  }

  void render_ground_item(ClientContext& context, const legacy::LegacyMapViewport& viewport,
                          const std::uint64_t item_id,
                          const client_v1::GroundItemState& item) const {
    const auto focused = item_id == context.state->world.focus_ground_item_id ||
                         item_id == context.state->world.pending_pickup_item_id;
    const auto fallback_x = legacy::legacy_tile_draw_x(viewport, item.x) + 16;
    const auto fallback_y = legacy::legacy_ground_mid_y(viewport, item.y) - 8;
    const auto frame = item.looks >= 0 && context.assets != nullptr
                           ? context.assets->get_frame(ArchiveId::dn_items, item.looks)
                           : nullptr;
    if (frame != nullptr && !frame->empty()) {
      draw_sprite(*context.renderer, frame,
                  legacy::legacy_ground_item_draw_x(viewport, item.x, frame->width),
                  legacy::legacy_ground_item_draw_y(viewport, item.y, frame->height));
    } else {
      context.renderer->fill_rect(RectI{fallback_x - 2, fallback_y - 2, 12, 6},
                                  focused ? 0xCCFACC15U : 0xCC94A3B8U);
    }
    if (!item.name.empty()) {
      context.renderer->draw_text(fallback_x - 10, fallback_y - 18, widen(item.name),
                                  focused ? 0xFFFFF7ADU : 0xFFE5E7EBU);
    }
  }

  void render_actor(ClientContext& context, const ActorState& actor, const ActorRenderPose& pose,
                    const legacy::LegacyMapViewport& viewport, bool is_self) {
    if (!pose.visible) {
      return;
    }
    const auto base_x = legacy::legacy_actor_base_x(viewport, pose.rx, pose.shift_x);
    const auto base_y = legacy::legacy_actor_base_y(viewport, pose.ry, pose.shift_y);

    const auto body = context.assets->get_frame(pose.body_archive, pose.body_index);
    const auto hair = pose.hair_index >= 0
                          ? context.assets->get_frame(ArchiveId::hair, pose.hair_index)
                          : nullptr;
    const auto weapon = pose.weapon_index >= 0
                            ? context.assets->get_frame(ArchiveId::weapon, pose.weapon_index)
                            : nullptr;

    if (body == nullptr) {
      context.renderer->fill_rect(
          RectI{base_x + 18, base_y - 36, 12, 28},
          is_self ? 0xFF22C55EU : 0xFFF97316U);
      render_actor_saying(context, actor, base_x, base_y);
      return;
    }

    if (pose.weapon_before_body && weapon != nullptr) {
      draw_sprite(*context.renderer, weapon, base_x + weapon->hotspot_x,
                  base_y + weapon->hotspot_y, pose.alpha);
    }
    draw_sprite(*context.renderer, body, base_x + body->hotspot_x, base_y + body->hotspot_y,
                pose.alpha);
    if (hair != nullptr) {
      draw_sprite(*context.renderer, hair, base_x + hair->hotspot_x, base_y + hair->hotspot_y,
                  pose.alpha);
    }
    if (!pose.weapon_before_body && weapon != nullptr) {
      draw_sprite(*context.renderer, weapon, base_x + weapon->hotspot_x,
                  base_y + weapon->hotspot_y, pose.alpha);
    }
    if (actor.max_hp > 0 && actor.hp >= 0 && actor.hp < actor.max_hp) {
      const auto width = 34;
      const auto filled = std::clamp((actor.hp * width) / actor.max_hp, 0, width);
      context.renderer->fill_rect(RectI{base_x + 7, base_y - 54, width, 4}, 0xCC1F2937U);
      context.renderer->fill_rect(RectI{base_x + 7, base_y - 54, filled, 4}, 0xCCE11D48U);
    }
    render_actor_saying(context, actor, base_x, base_y);
  }

  void render_actor_saying(ClientContext& context, const ActorState& actor, const int base_x,
                           const int base_y) const {
    if (actor.saying.empty() || actor.saying_started_ms == 0) {
      return;
    }
    const auto now_ms = detail::monotonic_ms();
    if (elapsed_ms(now_ms, actor.saying_started_ms) >= 4000U) {
      return;
    }
    const auto text = widen(actor.saying);
    const auto width = context.renderer->measure_text_width(text);
    const auto x = base_x + 24 - width / 2;
    const auto y = base_y - 72;
    const auto back = legacy_color_to_argb(actor.saying_back_color);
    if ((back >> 24U) != 0U) {
      context.renderer->fill_rect(RectI{x - 2, y - 1, width + 4, 14}, back);
    }
    context.renderer->draw_text(x, y, text, legacy_color_to_argb(actor.saying_fore_color));
  }

  std::string loaded_map_id_{};
  std::shared_ptr<const MapDocument> map_{};
  AnimationManager animation_{};
  LegacyAudioCueTracker audio_cues_{};
  LegacyHud legacy_hud_{};
  ui::UiTree ui_{};
  std::uint64_t next_left_hold_ms_{0};
  std::uint64_t next_right_hold_ms_{0};
  bool main_theme_due_{true};
  std::uint64_t main_theme_elapsed_ms_{0};
};

}  // namespace

void SceneManager::initialize(ClientContext& context) { change_scene(SceneId::boot, context); }

/// 切换场景：退出当前场景 → 创建新场景 → 进入新场景
void SceneManager::change_scene(SceneId id, ClientContext& context) {
  if (current_scene_ != nullptr) {
    current_scene_->exit(context);
  }
  current_scene_ = make_scene(id);
  current_id_ = id;
  current_scene_->enter(context);
}

/// 更新当前场景：先更新 UI 输入，再更新场景逻辑
void SceneManager::update(ClientContext& context, float delta_seconds) {
  if (current_scene_ != nullptr) {
    current_scene_->ui_tree().set_asset_manager(context.assets);
    context.ui_input = current_scene_->ui_tree().update(*context.input);
    current_scene_->update(context, delta_seconds);
  }
}

/// 渲染当前场景：先绘制场景内容，再绘制 UI 层
void SceneManager::render(ClientContext& context) {
  if (current_scene_ != nullptr) {
    current_scene_->render(context);
    current_scene_->ui_tree().paint(*context.renderer);
  }
}

ui::UiTree* SceneManager::current_ui_tree() {
  return current_scene_ != nullptr ? &current_scene_->ui_tree() : nullptr;
}

/// 工厂方法：根据 SceneId 创建对应的场景实例
std::unique_ptr<Scene> SceneManager::make_scene(SceneId id) {
  switch (id) {
    case SceneId::boot:
      return std::make_unique<BootScene>();
    case SceneId::login:
      return std::make_unique<LoginScene>();
    case SceneId::server_select:
      return std::make_unique<ServerSelectScene>();
    case SceneId::character_select:
      return std::make_unique<CharacterSelectScene>();
    case SceneId::login_notice:
      return std::make_unique<LoginNoticeScene>();
    case SceneId::loading:
      return std::make_unique<LoadingScene>();
    case SceneId::world:
      return std::make_unique<WorldScene>();
  }
  return std::make_unique<BootScene>();
}

}  // namespace mir2::client

