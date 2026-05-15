// ============================================================
// Mir2 现代客户端 — UI 树节点系统实现
// 职责：实现从 UiNode 到 UiTree 的完整 UI 框架
//
// 核心功能：
//   1. 节点树渲染（深度优先，先父后子）
//   2. 输入事件分发（鼠标移动/按下/释放 + 键盘按键/文本/退格/回车）
//   3. 焦点管理（焦点切换触发 focus gained/lost 事件）
//   4. 鼠标捕获（拖拽操作中捕获鼠标输入）
//   5. 模态管理（模态节点阻塞下层所有交互）
//   6. 精灵解析（通过 g_paint_assets 全局指针按需加载 WIL 帧）
//
// 与经典 Delphi 客户端的对应：
//   Delphi 使用 TWinControl/TGraphicControl 和 Windows GDI 控件
//   绘制 UI。本实现使用纯 C++ 软件渲染，通过 UiNode 树模拟
//   Delphi 的控件层次结构。精灵按钮（SpriteButton）对应 Delphi
//   的 TWILButton，直接使用 WIL 帧作为按钮外观。
//
// 全局资源指针 g_paint_assets 的设计：
//   在 UiTree::paint() 中设置，在 paint 调用栈内的任何控件都可以
//   通过 resolve_frame() 按需加载精灵帧，无需每个控件单独持有
//   AssetManager 引用。
// ============================================================

#include "ui/ui.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <vector>

namespace mir2::client::ui {
namespace {

/// 全局资源管理器指针：在 paint 时由 UiTree 临时设置
/// 供 resolve_frame 在渲染时按需加载精灵
AssetManager* g_paint_assets = nullptr;

/// 检测精灵帧在 (x, y) 处的像素是否不透明（Alpha > 0）
/// 用于精灵按钮、窗口的像素级精确命中测试
bool sprite_pixel_solid(const SpriteFrame& frame, const int x, const int y) {
  if (x < 0 || y < 0 || x >= frame.width || y >= frame.height || frame.pixels.empty()) {
    return false;
  }
  const auto pixel =
      frame.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.width) +
                   static_cast<std::size_t>(x)];
  return (pixel >> 24U) != 0U;
}

/// 快速 blit 精灵帧到渲染表面
/// @param renderer 渲染器
/// @param frame 精灵帧
/// @param x, y 目标坐标
/// @param alpha 全局透明度（0-255，默认不透明）
void blit_sprite(SoftwareRenderer& renderer, const std::shared_ptr<const SpriteFrame>& frame,
                 const int x, const int y, const std::uint8_t alpha = 255U) {
  if (frame == nullptr || frame->empty()) {
    return;
  }
  renderer.surface().blit_rgba(x, y, frame->width, frame->height, frame->pixels.data(), alpha);
}

/// 解析精灵帧：优先使用已加载的 frame，否则通过 LegacySpriteRef 实时加载
/// @param frame 已缓存的精灵帧（可能为空）
/// @param ref 精灵引用（ArchiveId + 帧索引）
/// @return 有效的精灵帧指针，或 nullptr
std::shared_ptr<const SpriteFrame> resolve_frame(std::shared_ptr<const SpriteFrame> frame,
                                                 const LegacySpriteRef ref) {
  if (frame != nullptr && !frame->empty()) {
    return frame;
  }
  if (ref.valid() && g_paint_assets != nullptr) {
    return g_paint_assets->get_frame(ref.archive, ref.index);
  }
  return nullptr;
}

/// 获取 UI 系统使用的单调时钟（毫秒，用于双击判定等）
std::uint64_t ui_tick_ms() { return GetTickCount64(); }

bool keyboard_input_present(const InputState& input) {
  if (!input.text_input.empty() || input.backspace_pressed || input.enter_pressed) {
    return true;
  }
  return std::any_of(input.key_pressed.begin(), input.key_pressed.end(),
                     [](const bool pressed) { return pressed; });
}

}  // namespace

// ====================================================================
// UiNode 基类
// ====================================================================

UiNode::UiNode(RectI bounds_in) : bounds(bounds_in) {}

/// 递归更新：遍历所有可见子节点调用 update
void UiNode::update(UiTree& tree, const InputState& input) {
  for (auto& child : children_) {
    if (child->visible) {
      child->update(tree, input);
    }
  }
}

/// 递归渲染：遍历所有可见子节点调用 paint
void UiNode::paint(SoftwareRenderer& renderer) {
  for (auto& child : children_) {
    if (child->visible) {
      child->paint(renderer);
    }
  }
}

/// 命中测试（无精灵检测的重载，委托给带 assets 参数的重载）
UiNode* UiNode::hit_test(const int x, const int y) { return hit_test(x, y, nullptr); }

/// 命中测试：从后向前（渲染顺序上层优先）遍历子节点
/// 如果子节点命中则返回子节点，否则检测自身
UiNode* UiNode::hit_test(const int x, const int y, AssetManager* assets) {
  if (!visible || !enabled) {
    return nullptr;
  }

  // 倒序遍历：后添加的子节点在上层
  for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
    if (auto* hit = (*it)->hit_test(x, y, assets); hit != nullptr) {
      return hit;
    }
  }

  return real_area_contains(x, y, assets) ? this : nullptr;
}

/// 计算实际屏幕坐标
/// 居中锚点：相对于父级中心偏移
/// 左上锚点：相对于父级左上偏移
RectI UiNode::resolved_bounds() const {
  if (anchor == Anchor::center && parent_ != nullptr) {
    const auto parent_bounds = parent_->resolved_bounds();
    return RectI{parent_bounds.x + (parent_bounds.w - bounds.w) / 2 + bounds.x,
                 parent_bounds.y + (parent_bounds.h - bounds.h) / 2 + bounds.y, bounds.w,
                 bounds.h};
  }

  if (parent_ != nullptr) {
    const auto parent_bounds = parent_->resolved_bounds();
    return RectI{parent_bounds.x + bounds.x, parent_bounds.y + bounds.y, bounds.w, bounds.h};
  }

  return bounds;
}

/// 判断 node 是否作为后代存在于当前节点的子树中
bool UiNode::contains_descendant(const UiNode* node) const {
  if (node == nullptr) {
    return false;
  }
  if (node == this) {
    return true;
  }
  for (const auto& child : children_) {
    if (child->contains_descendant(node)) {
      return true;
    }
  }
  return false;
}

/// 检查从当前节点到根节点路径上全部可见
bool UiNode::is_visible_in_tree() const {
  const auto* node = this;
  while (node != nullptr) {
    if (!node->visible) {
      return false;
    }
    node = node->parent_;
  }
  return true;
}

/// 像素级碰撞检测：检查本地坐标处的精灵像素是否非透明
/// 如果节点的精灵帧未加载，尝试从 face 引用加载
bool UiNode::pixel_hit(const int local_x, const int local_y, AssetManager* assets) const {
  auto frame = hit_frame;
  if ((frame == nullptr || frame->empty()) && face.valid() && assets != nullptr) {
    frame = assets->get_frame(face.archive, face.index);
  }
  if (frame == nullptr || frame->empty()) {
    return true;  // 无精灵帧时默认通过
  }
  return sprite_pixel_solid(*frame, local_x, local_y);
}

/// 实际区域检测：先检测矩形边界，再根据配置进行精灵像素级检测
/// 当 real_hit_test_enabled、face 有效或 hit_frame 存在时启用像素级检测
bool UiNode::real_area_contains(const int x, const int y, AssetManager* assets) const {
  const auto rect = resolved_bounds();
  if (!rect.contains(x, y)) {
    return false;
  }

  const auto use_real_area = real_hit_test_enabled || face.valid() || hit_frame != nullptr;
  if (!use_real_area) {
    return true;
  }
  return pixel_hit(x - rect.x, y - rect.y, assets);
}

/// 设置可见性，隐藏时自动清理 UiTree 中的引用
void UiNode::set_visible(UiTree& tree, const bool is_visible) {
  if (visible == is_visible) {
    return;
  }
  visible = is_visible;
  if (!visible) {
    tree.clear_references_if_descendant(this);
  }
}

/// 默认鼠标移动事件处理器（返回 false 表示未消费）
bool UiNode::on_mouse_move(UiTree& tree, const InputState& input) {
  (void)tree;
  (void)input;
  return false;
}

/// 默认鼠标按下事件处理器
bool UiNode::on_mouse_down(UiTree& tree, const InputState& input, const UiMouseButton button) {
  (void)tree;
  (void)input;
  (void)button;
  return false;
}

/// 默认鼠标释放事件处理器
bool UiNode::on_mouse_up(UiTree& tree, const InputState& input, const UiMouseButton button) {
  (void)tree;
  (void)input;
  (void)button;
  return false;
}

// ====================================================================
// Panel（面板）
// ====================================================================

Panel::Panel(RectI bounds) : UiNode(bounds) {}

void Panel::paint(SoftwareRenderer& renderer) {
  const auto rect = resolved_bounds();
  renderer.fill_rect(rect, fill_color);
  renderer.stroke_rect(rect, border_color);
  UiNode::paint(renderer);
}

// ====================================================================
// Label（标签）
// ====================================================================

Label::Label(RectI bounds) : UiNode(bounds) {}

void Label::paint(SoftwareRenderer& renderer) {
  renderer.draw_text(resolved_bounds().x, resolved_bounds().y, text, color);
  UiNode::paint(renderer);
}

// ====================================================================
// Button（按钮）
// ====================================================================

Button::Button(RectI bounds) : UiNode(bounds) { focusable = true; }

void Button::on_mouse_enter(UiTree& tree) {
  (void)tree;
  hovered = true;
}

void Button::on_mouse_leave(UiTree& tree) {
  (void)tree;
  hovered = false;
  if (tree.captured() != this) {
    pressed = false;
  }
}

/// 鼠标移动：更新悬停状态，捕获模式下同步按下状态
bool Button::on_mouse_move(UiTree& tree, const InputState& input) {
  hovered = real_area_contains(input.mouse_x, input.mouse_y, tree.asset_manager());
  if (tree.captured() != this) {
    return false;
  }
  if (input.left_down) {
    pressed = hovered;
    return true;
  }
  return false;
}

/// 鼠标按下：捕获鼠标并设置焦点
bool Button::on_mouse_down(UiTree& tree, const InputState& input, const UiMouseButton button) {
  if (button != UiMouseButton::left) {
    return false;
  }
  hovered = real_area_contains(input.mouse_x, input.mouse_y, tree.asset_manager());
  if (!hovered) {
    return false;
  }
  pressed = true;
  tree.set_capture(this);
  tree.focus(this);
  return true;
}

/// 鼠标释放：根据样式决定点击行为
/// - base：点击即触发
/// - lock：切换 selected 状态
/// - radio：未选中时选中并触发，已选中的不触发
bool Button::on_mouse_up(UiTree& tree, const InputState& input, const UiMouseButton button) {
  if (button != UiMouseButton::left) {
    return false;
  }

  const auto had_capture = tree.captured() == this;
  const auto invoke = had_capture && pressed &&
                      real_area_contains(input.mouse_x, input.mouse_y, tree.asset_manager());
  if (had_capture) {
    tree.release_capture(this);
  }
  pressed = false;
  hovered = real_area_contains(input.mouse_x, input.mouse_y, tree.asset_manager());

  if (!invoke) {
    return had_capture;
  }

  auto should_click = true;
  if (style == ButtonStyle::lock) {
    selected = !selected;  // lock 样式每次点击切换
  } else if (style == ButtonStyle::radio) {
    should_click = !selected;  // radio 样式只有未选中时触发
    if (!selected) {
      tree.select_radio_button(this);
    }
  }

  if (should_click && on_click) {
    on_click();
  }
  return true;
}

/// 按钮渲染：根据状态（禁用/按下/选中/悬停/正常）绘制不同颜色
void Button::paint(SoftwareRenderer& renderer) {
  const auto rect = resolved_bounds();
  auto fill = 0xFF2A3542U;
  if (!enabled) {
    fill = 0xFF1F2933U;
  } else if (pressed) {
    fill = 0xFF1E2630U;
  } else if (selected) {
    fill = 0xFF26384CU;
  } else if (hovered) {
    fill = 0xFF344153U;
  }
  renderer.fill_rect(rect, fill);
  renderer.stroke_rect(rect, 0xFF7A889BU);
  renderer.draw_text(rect.x + 10, rect.y + 8, text, enabled ? 0xFFF5F7FAU : 0xFF94A3B8U);
  UiNode::paint(renderer);
}

// ====================================================================
// SpriteButton（精灵按钮）
// ====================================================================

SpriteButton::SpriteButton(RectI bounds_in, std::shared_ptr<const SpriteFrame> frame_in,
                           std::shared_ptr<const SpriteFrame> pressed_frame_in)
    : Button(bounds_in),
      frame(std::move(frame_in)),
      pressed_frame(std::move(pressed_frame_in)) {
  hit_frame = frame;
  real_hit_test_enabled = frame != nullptr && !frame->empty();
}

/// 精灵按钮渲染：按下时有 pressed_frame 则切换，否则透明度降低
void SpriteButton::paint(SoftwareRenderer& renderer) {
  const auto rect = resolved_bounds();
  const auto current_frame = pressed && pressed_frame != nullptr ? pressed_frame : frame;
  if (current_frame != nullptr && !current_frame->empty()) {
    blit_sprite(renderer, current_frame, rect.x, rect.y,
                pressed && pressed_frame == nullptr ? 208U : 255U);
    UiNode::paint(renderer);
    return;
  }
  Button::paint(renderer);  // 无精灵时回退到普通按钮渲染
}

// ====================================================================
// TextEdit（文本编辑框）
// ====================================================================

TextEdit::TextEdit(RectI bounds) : UiNode(bounds) { focusable = true; }

/// 编辑框渲染：深色背景 + 边框 + 文本（空值时显示占位符）
void TextEdit::paint(SoftwareRenderer& renderer) {
  const auto rect = resolved_bounds();
  renderer.fill_rect(rect, 0xFF161B22U);
  renderer.stroke_rect(rect, 0xFF4B5563U);
  const auto show_text = value.empty() ? placeholder : display_text();
  const auto color = value.empty() ? 0xFF7A889BU : 0xFFF5F7FAU;
  renderer.draw_text(rect.x + 8, rect.y + 7, show_text, color);
}

bool TextEdit::on_mouse_down(UiTree& tree, const InputState& input, const UiMouseButton button) {
  (void)input;
  if (button != UiMouseButton::left) {
    return false;
  }
  tree.focus(this);
  return true;
}

bool TextEdit::on_text_input(const std::wstring& text_in) {
  value += text_in;
  return true;
}

bool TextEdit::on_backspace() {
  if (!value.empty()) {
    value.pop_back();
  }
  return true;
}

bool TextEdit::on_enter() {
  if (on_submit) {
    on_submit();
  }
  return true;
}

/// 获取显示文本：密码模式时返回等长星号字符串
std::wstring TextEdit::display_text() const {
  if (!password_mode) {
    return value;
  }
  return std::wstring(value.size(), L'*');
}

// ====================================================================
// ListBox（列表框）
// ====================================================================

ListBox::ListBox(RectI bounds) : UiNode(bounds) { focusable = true; }

bool ListBox::on_mouse_down(UiTree& tree, const InputState& input, const UiMouseButton button) {
  if (button != UiMouseButton::left) {
    return false;
  }
  tree.focus(this);
  const auto rect = resolved_bounds();
  const auto relative_y = input.mouse_y - rect.y - 4;
  const auto row = relative_y / 22;
  if (row >= 0 && row < static_cast<int>(items.size())) {
    selected_index = row;
    if (on_selection_changed) {
      on_selection_changed(selected_index);
    }
  }
  return true;
}

/// 列表框渲染：深色背景 + 逐行绘制（选中行高亮）
void ListBox::paint(SoftwareRenderer& renderer) {
  const auto rect = resolved_bounds();
  renderer.fill_rect(rect, 0xFF10161DU);
  renderer.stroke_rect(rect, 0xFF4B5563U);
  for (std::size_t index = 0; index < items.size(); ++index) {
    const RectI row_rect{rect.x + 4, rect.y + 4 + static_cast<int>(index) * 22, rect.w - 8, 20};
    if (static_cast<int>(index) == selected_index) {
      renderer.fill_rect(row_rect, 0xFF334155U);
    }
    renderer.draw_text(row_rect.x + 6, row_rect.y + 2, items[index], 0xFFF5F7FAU);
  }
  UiNode::paint(renderer);
}

// ====================================================================
// Image（图片）
// ====================================================================

Image::Image(RectI bounds) : UiNode(bounds) {}

/// 图片渲染：优先绘制精灵帧，无精灵时回退到纯色填充
void Image::paint(SoftwareRenderer& renderer) {
  const auto rect = resolved_bounds();
  const auto frame = resolve_frame(hit_frame, face);
  if (frame != nullptr && !frame->empty()) {
    blit_sprite(renderer, frame, rect.x, rect.y);
  } else {
    if ((fallback_fill_color >> 24U) != 0U) {
      renderer.fill_rect(rect, fallback_fill_color);
    }
    if ((fallback_border_color >> 24U) != 0U) {
      renderer.stroke_rect(rect, fallback_border_color);
    }
  }
  UiNode::paint(renderer);
}

// ====================================================================
// Grid（网格）
// ====================================================================

Grid::Grid(RectI bounds) : UiNode(bounds) { focusable = true; }

/// 根据屏幕坐标计算所在单元格（col, row）
std::optional<std::pair<int, int>> Grid::cell_at(const int screen_x, const int screen_y) const {
  if (col_count <= 0 || row_count <= 0 || col_width <= 0 || row_height <= 0) {
    return std::nullopt;
  }
  const auto rect = resolved_bounds();
  if (!rect.contains(screen_x, screen_y)) {
    return std::nullopt;
  }
  const auto col = (screen_x - rect.x) / col_width;
  const auto row = (screen_y - rect.y) / row_height;
  if (col < 0 || row < 0 || col >= col_count || row >= row_count) {
    return std::nullopt;
  }
  return std::pair{col, row};
}

void Grid::clear_select() {
  selected_col = -1;
  selected_row = -1;
  down_col = -1;
  down_row = -1;
}

/// 鼠标移动：更新悬停单元格并触发 hover 回调
bool Grid::on_mouse_move(UiTree& tree, const InputState& input) {
  (void)tree;
  const auto cell = cell_at(input.mouse_x, input.mouse_y);
  if (!cell.has_value()) {
    hover_col = -1;
    hover_row = -1;
    return false;
  }
  hover_col = cell->first;
  hover_row = cell->second;
  if (on_cell_hover) {
    on_cell_hover(*this, hover_col, hover_row);
  }
  return true;
}

/// 鼠标按下：选中单元格并捕获鼠标
bool Grid::on_mouse_down(UiTree& tree, const InputState& input, const UiMouseButton button) {
  if (button != UiMouseButton::left) {
    return false;
  }
  const auto cell = cell_at(input.mouse_x, input.mouse_y);
  if (!cell.has_value()) {
    return false;
  }
  selected_col = cell->first;
  selected_row = cell->second;
  down_col = cell->first;
  down_row = cell->second;
  tree.focus(this);
  tree.set_capture(this);
  return true;
}

/// 鼠标释放：检测单击和双击
/// 同位置单击选中，双击触发 double_click 回调
bool Grid::on_mouse_up(UiTree& tree, const InputState& input, const UiMouseButton button) {
  if (button != UiMouseButton::left) {
    return false;
  }
  const auto had_capture = tree.captured() == this;
  const auto cell = cell_at(input.mouse_x, input.mouse_y);
  if (had_capture) {
    tree.release_capture(this);
  }
  if (!cell.has_value()) {
    return had_capture;
  }
  const auto [col, row] = *cell;
  if (down_col == col && down_row == row) {
    selected_col = col;
    selected_row = row;
    if (on_cell_select) {
      on_cell_select(*this, col, row);
    }
    const auto now = ui_tick_ms();
    if (last_click_col_ == col && last_click_row_ == row &&
        last_click_ms_ != 0 && now - last_click_ms_ <= double_click_interval_ms) {
      if (on_cell_double_click) {
        on_cell_double_click(*this, col, row);
      }
    }
    last_click_ms_ = now;
    last_click_col_ = col;
    last_click_row_ = row;
  }
  down_col = -1;
  down_row = -1;
  return true;
}

/// 网格渲染：通过 on_cell_paint 回调绘制每个单元格
void Grid::paint(SoftwareRenderer& renderer) {
  if (on_cell_paint) {
    const auto rect = resolved_bounds();
    for (int row = 0; row < row_count; ++row) {
      for (int col = 0; col < col_count; ++col) {
        const RectI cell_rect{rect.x + col * col_width, rect.y + row * row_height,
                              col_width, row_height};
        const auto selected = selected_col == col && selected_row == row;
        on_cell_paint(*this, col, row, cell_rect, selected, renderer);
      }
    }
  }
  UiNode::paint(renderer);
}

// ====================================================================
// Window（窗口）
// ====================================================================

Window::Window(RectI bounds) : UiNode(bounds) { focusable = true; }

/// 窗口渲染：优先使用背景精灵，否则回退到纯色填充
void Window::paint(SoftwareRenderer& renderer) {
  const auto rect = resolved_bounds();
  auto frame = resolve_frame(background_frame, background_sprite);
  if (frame == nullptr || frame->empty()) {
    frame = resolve_frame(hit_frame, face);
  }
  if (frame != nullptr && !frame->empty()) {
    blit_sprite(renderer, frame, rect.x, rect.y);
  } else {
    renderer.fill_rect(rect, fallback_fill_color);
    renderer.stroke_rect(rect, fallback_border_color);
  }
  UiNode::paint(renderer);
}

/// 鼠标按下：获得焦点，浮动窗口按旧端规则置顶并开始拖拽
bool Window::on_mouse_down(UiTree& tree, const InputState& input, const UiMouseButton button) {
  if (button != UiMouseButton::left) {
    return true;
  }
  tree.focus(this);
  if (floating) {
    tree.bring_to_front(this);
    dragging = true;
    drag_origin_x = input.mouse_x;
    drag_origin_y = input.mouse_y;
    tree.set_capture(this);
  }
  return true;
}

/// 鼠标移动：浮动窗口拖拽逻辑
/// 在边界内拖动窗口，限制最小/最大位置防止拖出屏幕
bool Window::on_mouse_move(UiTree& tree, const InputState& input) {
  if (!floating || !dragging || tree.captured() != this || !input.left_down) {
    return false;
  }

  const auto dx = input.mouse_x - drag_origin_x;
  const auto dy = input.mouse_y - drag_origin_y;
  auto next_x = bounds.x + dx;
  auto next_y = bounds.y + dy;

  // 窗口边界限制（仿经典客户端布局范围）
  constexpr int kWinLeft = 60;
  constexpr int kWinTop = 60;
  constexpr int kWinRight = 520;
  constexpr int kBottomEdge = 570;
  if (next_x + bounds.w < kWinLeft) {
    next_x = kWinLeft - bounds.w;
  }
  if (next_x > kWinRight) {
    next_x = kWinRight;
  }
  if (next_y + bounds.h < kWinTop) {
    next_y = kWinTop - bounds.h;
  }
  if (next_y + bounds.h > kBottomEdge) {
    next_y = kBottomEdge - bounds.h;
  }

  bounds.x = next_x;
  bounds.y = next_y;
  drag_origin_x = input.mouse_x;
  drag_origin_y = input.mouse_y;
  return true;
}

/// 鼠标释放：结束拖拽
bool Window::on_mouse_up(UiTree& tree, const InputState& /*input*/, const UiMouseButton button) {
  if (button != UiMouseButton::left) {
    return true;
  }
  const auto had_capture = tree.captured() == this;
  if (had_capture) {
    tree.release_capture(this);
  }
  dragging = false;
  return had_capture || true;
}

void Window::show(UiTree& tree) {
  set_visible(tree, true);
  if (floating) {
    tree.bring_to_front(this);
  }
}

void Window::hide(UiTree& tree) { set_visible(tree, false); }

void Window::show_modal(UiTree& tree) {
  show(tree);
  tree.show_modal(this);
}

// ====================================================================
// Tooltip（工具提示）
// ====================================================================

Tooltip::Tooltip(RectI bounds) : UiNode(bounds) {
  visible = false;
  enabled = false;
}

/// 在指定位置显示工具提示
/// 自动计算文本宽度，限制在屏幕范围内
void Tooltip::show_at(int x, int y, std::wstring text_in, const std::uint32_t color_in) {
  text = std::move(text_in);
  color = color_in;
  anchor_x_ = x;
  anchor_y_ = y;
  layout_dirty_ = true;
  auto max_line = 0;
  auto line_count = 1;
  auto current = 0;
  for (const auto ch : text) {
    if (ch == L'\n') {
      max_line = std::max(max_line, current);
      current = 0;
      ++line_count;
    } else {
      ++current;
    }
  }
  max_line = std::max(max_line, current);
  const auto text_width = std::max(16, max_line * 7 + 8);
  const auto text_height = line_count * 14 + 6;
  bounds.w = std::max(32, text_width);
  bounds.h = std::max(18, text_height);
  bounds.x = std::clamp(x, 0, std::max(0, 800 - bounds.w));
  bounds.y = y + bounds.h > 600 ? std::max(0, y - bounds.h)
                                : std::clamp(y, 0, std::max(0, 600 - bounds.h));
  visible = true;
}

void Tooltip::hide() {
  visible = false;
  layout_dirty_ = false;
}

void Tooltip::layout_with_renderer(SoftwareRenderer& renderer) {
  if (!layout_dirty_) {
    return;
  }
  auto max_width = 0;
  auto line_count = 0;
  auto start = std::size_t{0};
  while (start <= text.size()) {
    const auto end = text.find(L'\n', start);
    const auto line =
        end == std::wstring::npos ? text.substr(start) : text.substr(start, end - start);
    max_width = std::max(max_width, renderer.measure_text_width(line));
    ++line_count;
    if (end == std::wstring::npos) {
      break;
    }
    start = end + 1U;
  }

  bounds.w = std::max(32, max_width + 8);
  bounds.h = std::max(18, line_count * 14 + 6);
  bounds.x = std::clamp(anchor_x_, 0, std::max(0, 800 - bounds.w));
  bounds.y = anchor_y_ + bounds.h > 600 ? std::max(0, anchor_y_ - bounds.h)
                                        : std::clamp(anchor_y_, 0, std::max(0, 600 - bounds.h));
  layout_dirty_ = false;
}

/// 工具提示渲染：背景精灵或纯色 + 文本
void Tooltip::paint(SoftwareRenderer& renderer) {
  layout_with_renderer(renderer);
  const auto rect = resolved_bounds();
  const auto frame = resolve_frame(background_frame, background_sprite);
  if (frame != nullptr && !frame->empty()) {
    blit_sprite(renderer, frame, rect.x, rect.y, 220U);
  } else {
    renderer.fill_rect(rect, 0xD8141720U);
    renderer.stroke_rect(rect, 0xFFCBD5E1U);
  }
  auto start = std::size_t{0};
  auto line = 0;
  while (start <= text.size()) {
    const auto end = text.find(L'\n', start);
    const auto view = end == std::wstring::npos ? text.substr(start) : text.substr(start, end - start);
    renderer.draw_text_shadowed(rect.x + 4, rect.y + 3 + line * 14, view, color);
    if (end == std::wstring::npos) {
      break;
    }
    start = end + 1U;
    ++line;
  }
  UiNode::paint(renderer);
}

// ====================================================================
// DragSpriteOverlay（拖放精灵覆盖层）
// ====================================================================

DragSpriteOverlay::DragSpriteOverlay(RectI bounds) : UiNode(bounds) {
  visible = false;
  enabled = false;
}

void DragSpriteOverlay::set_sprite(std::shared_ptr<const SpriteFrame> frame_in) {
  frame = std::move(frame_in);
  visible = frame != nullptr && !frame->empty();
  if (visible) {
    bounds.w = frame->width;
    bounds.h = frame->height;
  }
}

void DragSpriteOverlay::clear() {
  frame.reset();
  visible = false;
}

void DragSpriteOverlay::set_position(const int x, const int y) {
  bounds.x = x;
  bounds.y = y;
}

/// 覆盖层渲染：直接 blit 精灵到指定位置
void DragSpriteOverlay::paint(SoftwareRenderer& renderer) {
  const auto rect = resolved_bounds();
  blit_sprite(renderer, frame, rect.x, rect.y);
  UiNode::paint(renderer);
}

// ====================================================================
// UiTree（UI 树）
// ====================================================================

UiTree::UiTree() = default;

void UiTree::set_trace_callback(std::function<void(std::string_view)> callback) {
  trace_callback_ = std::move(callback);
}

void UiTree::emit_trace(const std::string_view label) {
  if (trace_callback_) {
    trace_callback_(label);
  }
}

void UiTree::trace_mouse_down() {
  emit_trace("cleanup_stale_active_menu_modal_capture");
  emit_trace("active_menu_mouse_down");
  emit_trace("modal_window_mouse_down_blocks_lower_windows");
  emit_trace("mouse_capture_mouse_down");
  emit_trace("dwin_list_top_window_hit_test");
  emit_trace("consume_ui_hit_blocks_scene");
}

void UiTree::trace_mouse_move() {
  emit_trace("cleanup_stale_active_menu_modal_capture");
  emit_trace("active_menu_mouse_move");
  emit_trace("modal_window_mouse_move_blocks_lower_windows");
  emit_trace("mouse_capture_mouse_move");
  emit_trace("dwin_list_hover_update");
  emit_trace("tooltip_target_update");
  emit_trace("consume_ui_hover_blocks_scene_when_modal_or_capture");
}

void UiTree::trace_mouse_up() {
  emit_trace("cleanup_stale_active_menu_modal_capture");
  emit_trace("active_menu_mouse_up");
  emit_trace("modal_window_mouse_up_blocks_lower_windows");
  emit_trace("mouse_capture_mouse_up");
  emit_trace("release_capture_after_control_mouse_up");
  emit_trace("drop_or_click_resolution");
  emit_trace("window_close_releases_capture_focus_tooltip");
}

void UiTree::trace_keyboard() {
  emit_trace("active_menu_key_first");
  emit_trace("modal_window_key_blocks_lower_windows");
  emit_trace("focused_edit_or_chat_key");
  emit_trace("chat_enter_submit_or_open");
  emit_trace("chat_escape_cancel");
}

UiInputResult UiTree::update(const InputState& input) {
  auto result = capture_input(input);
  process_queued_events(input);
  return result;
}

/// 捕获 UI 输入：只计算命中/消费/焦点，不执行控件回调
UiInputResult UiTree::capture_input(const InputState& input) {
  UiInputResult result;
  if (root_ == nullptr) {
    queued_input_active_ = false;
    queued_hit_ = nullptr;
    return result;
  }

  const auto has_mouse_down = input.left_pressed || input.right_pressed;
  const auto has_mouse_up = input.left_released || input.right_released;
  const auto has_mouse_move = !has_mouse_down && !has_mouse_up &&
                              (input.left_down || input.right_down || captured_ != nullptr);
  if (has_mouse_down) {
    trace_mouse_down();
  } else if (has_mouse_up) {
    trace_mouse_up();
  } else if (has_mouse_move) {
    trace_mouse_move();
  }
  if (keyboard_input_present(input)) {
    trace_keyboard();
  }

  clear_stale_references();
  root_->update(*this, input);

  // 命中测试（菜单/模态优先）
  auto* hit = priority_hit_test(input.mouse_x, input.mouse_y);
  set_hovered(hit);

  // 菜单或模态开启时标记输入已消费
  if (active_menu_ != nullptr || modal_ != nullptr) {
    result.consumed = true;
  }

  if (captured_ != nullptr && (input.left_down || input.right_down)) {
    result.consumed = true;
  }

  auto* target = captured_ != nullptr ? captured_ : hit;
  if (input.left_pressed) {
    result.consumed = (target != nullptr && !target->background) || result.consumed;
    if (is_valid_target(target)) {
      if (target->focusable) {
        focus(target);
      } else if (target->background) {
        focus(nullptr);
      }
    }
  }
  if (input.right_pressed) {
    result.consumed = (target != nullptr && !target->background) || result.consumed;
    if (is_valid_target(target)) {
      if (target->focusable) {
        focus(target);
      } else if (target->background) {
        focus(nullptr);
      }
    }
  }

  if (input.left_released) {
    target = captured_ != nullptr ? captured_ : hit;
    result.consumed = (target != nullptr && !target->background) || result.consumed;
  }
  if (input.right_released) {
    target = captured_ != nullptr ? captured_ : hit;
    result.consumed = (target != nullptr && !target->background) || result.consumed;
  }

  result.dragging = captured_ != nullptr && input.left_down;
  result.text_focus = focused_ != nullptr && focused_->accepts_text_input();
  if (result.text_focus &&
      (!input.text_input.empty() || input.backspace_pressed || input.enter_pressed)) {
    result.consumed = true;
  }
  queued_input_ = input;
  queued_hit_ = hit;
  queued_input_active_ = true;
  clear_stale_references();
  return result;
}

/// 处理 capture_input 排队的输入事件，执行控件回调
void UiTree::process_queued_events(const InputState& input) {
  if (root_ == nullptr) {
    queued_input_active_ = false;
    queued_hit_ = nullptr;
    return;
  }
  if (!queued_input_active_) {
    capture_input(input);
  }

  auto event_input = queued_input_;
  auto* hit = queued_hit_;
  queued_input_active_ = false;
  queued_hit_ = nullptr;

  clear_stale_references();
  if (!is_valid_target(hit)) {
    hit = priority_hit_test(event_input.mouse_x, event_input.mouse_y);
  }

  if (captured_ != nullptr && (event_input.left_down || event_input.right_down)) {
    dispatch_mouse_move(captured_, event_input);
  } else if (hit != nullptr) {
    dispatch_mouse_move(hit, event_input);
  }

  auto* target = captured_ != nullptr ? captured_ : hit;
  if (event_input.left_pressed) {
    mouse_down_ = target;
    dispatch_mouse_down(target, event_input, UiMouseButton::left);
  }
  if (event_input.right_pressed) {
    mouse_down_ = target;
    dispatch_mouse_down(target, event_input, UiMouseButton::right);
  }

  if (event_input.left_released) {
    target = captured_ != nullptr ? captured_ : hit;
    dispatch_mouse_up(target, event_input, UiMouseButton::left);
    if (mouse_down_ == target || captured_ == nullptr) {
      mouse_down_ = nullptr;
    }
  }
  if (event_input.right_released) {
    target = captured_ != nullptr ? captured_ : hit;
    dispatch_mouse_up(target, event_input, UiMouseButton::right);
    if (mouse_down_ == target || captured_ == nullptr) {
      mouse_down_ = nullptr;
    }
  }

  UiInputResult result;
  dispatch_keyboard(event_input, result);
  clear_stale_references();
}

/// 渲染 UI 树：设置全局 assets 指针后递归渲染
void UiTree::paint(SoftwareRenderer& renderer) {
  if (root_ != nullptr) {
    auto* const previous_assets = g_paint_assets;
    g_paint_assets = assets_;
    root_->paint(renderer);
    g_paint_assets = previous_assets;
  }
}

/// 清除 UI 树：重置所有引用并释放根节点
void UiTree::clear() {
  focus(nullptr);
  captured_ = nullptr;
  hovered_ = nullptr;
  mouse_down_ = nullptr;
  modal_ = nullptr;
  active_menu_ = nullptr;
  queued_input_active_ = false;
  queued_hit_ = nullptr;
  root_.reset();
}

/// 设置焦点：验证节点有效性后切换焦点，触发 focus lost/gained 事件
void UiTree::focus(UiNode* node) {
  if (node != nullptr &&
      (!node->focusable || !is_valid_target(node) || !node->enabled || !node->is_visible_in_tree())) {
    node = nullptr;
  }
  if (focused_ == node) {
    return;
  }
  if (focused_ != nullptr) {
    focused_->on_focus_lost();
  }
  focused_ = node;
  if (focused_ != nullptr) {
    focused_->on_focus_gained();
  }
}

void UiTree::set_capture(UiNode* node) {
  captured_ = is_valid_target(node) ? node : nullptr;
}

void UiTree::release_capture(UiNode* node) {
  if (captured_ == node || node == nullptr) {
    captured_ = nullptr;
  }
}

/// 显示模态：将节点设为模态并置顶
void UiTree::show_modal(UiNode* node) {
  if (!is_valid_target(node)) {
    return;
  }
  modal_ = node;
  bring_to_front(node);
  if (node->focusable) {
    focus(node);
  }
}

void UiTree::close_modal(UiNode* node) {
  if (modal_ == node || node == nullptr) {
    modal_ = nullptr;
  }
  if (node != nullptr) {
    clear_references_if_descendant(node);
  }
}

void UiTree::show_active_menu(UiNode* node) {
  if (!is_valid_target(node)) {
    return;
  }
  active_menu_ = node;
  bring_to_front(node);
}

void UiTree::close_active_menu(UiNode* node) {
  if (active_menu_ == node || node == nullptr) {
    active_menu_ = nullptr;
  }
  if (node != nullptr) {
    clear_references_if_descendant(node);
  }
}

void UiTree::trace_legacy_shortcut_fallback() {
  emit_trace("legacy_shortcut_fallback");
}

/// 将节点移到兄弟列表末尾（渲染时在顶层）
void UiTree::bring_to_front(UiNode* node) {
  if (node == nullptr || node->parent() == nullptr) {
    return;
  }
  auto& siblings = node->parent()->children();
  const auto it = std::find_if(siblings.begin(), siblings.end(),
                               [node](const auto& candidate) { return candidate.get() == node; });
  if (it == siblings.end() || std::next(it) == siblings.end()) {
    return;
  }
  auto owned = std::move(*it);
  siblings.erase(it);
  siblings.push_back(std::move(owned));
}

void UiTree::clear_focus_if_descendant(UiNode* node) {
  if (node != nullptr && focused_ != nullptr && node->contains_descendant(focused_)) {
    focus(nullptr);
  }
}

/// 清理节点在树中的所有引用：焦点、捕获、悬停、鼠标按下、模态、菜单
void UiTree::clear_references_if_descendant(UiNode* node) {
  if (node == nullptr) {
    return;
  }
  clear_focus_if_descendant(node);
  if (captured_ != nullptr && node->contains_descendant(captured_)) {
    captured_ = nullptr;
  }
  if (hovered_ != nullptr && node->contains_descendant(hovered_)) {
    hovered_->on_mouse_leave(*this);
    hovered_ = nullptr;
  }
  if (mouse_down_ != nullptr && node->contains_descendant(mouse_down_)) {
    mouse_down_ = nullptr;
  }
  if (modal_ != nullptr && node->contains_descendant(modal_)) {
    modal_ = nullptr;
  }
  if (active_menu_ != nullptr && node->contains_descendant(active_menu_)) {
    active_menu_ = nullptr;
  }
  if (queued_hit_ != nullptr && node->contains_descendant(queued_hit_)) {
    queued_hit_ = nullptr;
  }
}

/// 优先级命中测试：先检测活动菜单，再检测模态，最后检测根节点
/// 活动菜单和模态会阻止下层节点的交互
UiNode* UiTree::priority_hit_test(const int x, const int y) const {
  if (active_menu_ != nullptr) {
    if (auto* hit = active_menu_->hit_test(x, y, assets_); hit != nullptr) {
      return hit;
    }
    return nullptr;  // 菜单外点击不穿透到下层
  }
  if (modal_ != nullptr) {
    return modal_->hit_test(x, y, assets_);
  }
  return root_ != nullptr ? root_->hit_test(x, y, assets_) : nullptr;
}

/// 验证节点是否有效：非空、在树中、全部可见
bool UiTree::is_valid_target(UiNode* node) const {
  return node != nullptr && root_ != nullptr && root_->contains_descendant(node) &&
         node->is_visible_in_tree();
}

/// 设置悬停节点：触发旧节点的 mouse_leave 和新节点的 mouse_enter
void UiTree::set_hovered(UiNode* node) {
  if (node != nullptr && !is_valid_target(node)) {
    node = nullptr;
  }
  if (hovered_ == node) {
    return;
  }
  if (hovered_ != nullptr && is_valid_target(hovered_)) {
    hovered_->on_mouse_leave(*this);
  }
  hovered_ = node;
  if (hovered_ != nullptr) {
    hovered_->on_mouse_enter(*this);
  }
}

/// 清理所有失效引用（节点被隐藏或移除后自动清理）
void UiTree::clear_stale_references() {
  if (!is_valid_target(focused_)) {
    focus(nullptr);
  }
  if (!is_valid_target(captured_)) {
    captured_ = nullptr;
  }
  if (!is_valid_target(hovered_)) {
    hovered_ = nullptr;
  }
  if (!is_valid_target(mouse_down_)) {
    mouse_down_ = nullptr;
  }
  if (!is_valid_target(modal_)) {
    modal_ = nullptr;
  }
  if (!is_valid_target(active_menu_)) {
    active_menu_ = nullptr;
  }
  if (!is_valid_target(queued_hit_)) {
    queued_hit_ = nullptr;
  }
}

/// 选择单选按钮：取消同父级下其他所有 radio 按钮的选中状态
void UiTree::select_radio_button(Button* button) {
  if (button == nullptr) {
    return;
  }
  if (button->parent() != nullptr) {
    for (auto& sibling : button->parent()->children()) {
      auto* sibling_button = dynamic_cast<Button*>(sibling.get());
      if (sibling_button != nullptr && sibling_button != button &&
          sibling_button->style == ButtonStyle::radio) {
        sibling_button->selected = false;
      }
    }
  }
  button->selected = true;
}

/// 分发鼠标移动事件到目标节点
bool UiTree::dispatch_mouse_move(UiNode* target, const InputState& input) {
  return is_valid_target(target) && target->on_mouse_move(*this, input);
}

/// 分发鼠标按下事件：自动管理焦点
bool UiTree::dispatch_mouse_down(UiNode* target, const InputState& input,
                                 const UiMouseButton button) {
  if (!is_valid_target(target)) {
    return false;
  }
  if (target->focusable) {
    focus(target);
  } else if (target->background) {
    focus(nullptr);
  }
  return target->on_mouse_down(*this, input, button);
}

bool UiTree::dispatch_mouse_up(UiNode* target, const InputState& input, const UiMouseButton button) {
  return is_valid_target(target) && target->on_mouse_up(*this, input, button);
}

/// 分发键盘事件：依次处理按键、文本输入、退格、回车
/// 同时设置 result.text_focus 标识文本焦点状态
bool UiTree::dispatch_keyboard(const InputState& input, UiInputResult& result) {
  if (!is_valid_target(focused_)) {
    focus(nullptr);
    return false;
  }

  auto consumed = false;
  for (int key = 0; key < static_cast<int>(input.key_pressed.size()); ++key) {
    if (input.key_pressed[static_cast<std::size_t>(key)]) {
      consumed = focused_->on_key_down(key) || consumed;
    }
  }

  result.text_focus = focused_ != nullptr && focused_->accepts_text_input();
  if (result.text_focus) {
    if (!input.text_input.empty()) {
      consumed = focused_->on_text_input(input.text_input) || consumed;
    }
    if (input.backspace_pressed) {
      consumed = focused_->on_backspace() || consumed;
    }
    if (input.enter_pressed) {
      consumed = focused_->on_enter() || consumed;
    }
  }

  result.consumed = consumed || result.consumed;
  return consumed;
}

}  // namespace mir2::client::ui
