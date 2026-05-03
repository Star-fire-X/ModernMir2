// ============================================================
// Mir2 现代客户端 — UI 树节点系统声明
// 职责：定义基于树结构的 UI 框架，包含按钮、编辑框、网格、
//       窗口、工具提示、拖放精灵覆盖层等 UI 控件
//
// 架构说明：
//   经典传奇（Mir2）的 UI 系统基于 WIL 精灵帧构建，每个 UI 元素
//   都是一个精灵帧（如按钮、窗口背景、状态栏等）。
//
//   本实现使用树状节点（UiNode）结构：
//   - UiTree 是根容器，持有所有节点的所有权
//   - 每个 UiNode 可以有子节点（如窗口中包含按钮）
//   - 事件分发采用冒泡机制：从目标节点向上传播
//   - 渲染采用深度优先遍历（先父后子）
//
//   输入处理优先级：
//   1. 活动菜单（active_menu_）— 菜单打开时优先处理
//   2. 模态节点（modal_）— 模态对话框阻塞下层
//   3. 常规命中测试（从后向前遍历子节点）
//
//   与 Delphi 客户端的对应：
//   - Delphi 使用 TWILButton、TWILForm 等 VCL 控件
//   - 本实现使用纯 C++ 精灵节点模拟相同的外观和行为
//   - 精灵帧通过 WIL/WIX 归档加载，与 Delphi 一致
// ============================================================
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "assets/asset_manager.hpp"
#include "platform/win32_window.hpp"
#include "render/software_renderer.hpp"

namespace mir2::client::ui {

/// 锚点枚举：决定节点在父容器中的定位方式
enum class Anchor {
  top_left,  ///< 左上角定位（默认）
  center     ///< 居中定位（bounds 的偏移量相对于父级中心）
};

class UiTree;

/// 旧版精灵引用：ArchiveId + 帧索引
/// 用于从 WIL 归档加载 UI 精灵（如按钮、窗口背景）
struct LegacySpriteRef {
  ArchiveId archive{ArchiveId::prguse};  ///< 归档类型（默认 prguse.wil）
  int index{-1};                          ///< 帧索引（-1 表示无效）

  [[nodiscard]] bool valid() const { return index >= 0; }
};

/// UI 输入查询结果：每帧由 UiTree::update() 返回
struct UiInputResult {
  bool consumed{false};     ///< 输入已被 UI 消费（场景不应再处理）
  bool text_focus{false};   ///< 当前有编辑框获得焦点
  bool dragging{false};     ///< 正在拖拽中
};

/// 鼠标按键枚举
enum class UiMouseButton {
  left,   ///< 左键
  right   ///< 右键
};

/// 按钮样式枚举
enum class ButtonStyle {
  base,   ///< 普通按钮（点击触发）
  radio,  ///< 单选按钮（同组互斥）
  lock    ///< 锁定式按钮（点击切换选中状态）
};

/// UI 节点基类：所有 UI 控件的基类
///
/// 节点树的关键设计：
///   bounds      — 相对于父节点的矩形（x, y, w, h）
///   anchor      — 影响 resolved_bounds() 的计算方式
///   resolved_bounds() — 递归计算节点的屏幕坐标空间位置
///
/// 输入事件流：
///   UiTree::update() → 命中测试(hit_test) → 事件分发(on_mouse_*)
///   → 如果子节点未消费，父节点处理
///
/// 可见性控制：
///   set_visible() 会同时更新 UiTree 中的焦点/捕获/悬停引用，
///   避免出现指向不可见节点的悬空指针。
class UiNode {
 public:
  explicit UiNode(RectI bounds);
  virtual ~UiNode() = default;

  /// 模板方法：创建并添加子节点
  /// 返回原始指针以便调用方设置属性
  template <typename T, typename... Args>
  T* emplace_child(Args&&... args) {
    auto child = std::make_unique<T>(std::forward<Args>(args)...);
    auto* raw = child.get();
    child->parent_ = this;
    children_.push_back(std::move(child));
    return raw;
  }

  /// 更新逻辑：递归遍历可见子节点调用 update
  virtual void update(UiTree& tree, const InputState& input);
  /// 渲染：递归遍历可见子节点调用 paint
  virtual void paint(SoftwareRenderer& renderer);
  /// 命中测试（无精灵检测的重载版本）
  [[nodiscard]] UiNode* hit_test(int x, int y);
  /// 命中测试：从后向前遍历子节点，支持精灵像素级检测
  [[nodiscard]] virtual UiNode* hit_test(int x, int y, AssetManager* assets);
  /// 计算节点的实际屏幕坐标（考虑锚点和父级偏移）
  [[nodiscard]] virtual RectI resolved_bounds() const;
  /// 判断 node 是否为当前节点的后代
  [[nodiscard]] bool contains_descendant(const UiNode* node) const;
  /// 从当前节点到根全部可见
  [[nodiscard]] bool is_visible_in_tree() const;
  /// 精灵像素级碰撞检测（检查本地坐标处的像素是否不透明）
  [[nodiscard]] bool pixel_hit(int local_x, int local_y, AssetManager* assets) const;
  /// 实际区域包含检测（先矩形再可选精灵像素检测）
  [[nodiscard]] bool real_area_contains(int x, int y, AssetManager* assets) const;
  /// 设置可见性并自动清理 UiTree 中的引用
  void set_visible(UiTree& tree, bool is_visible);
  /// 焦点获得回调
  virtual void on_focus_gained() {}
  /// 焦点丢失回调
  virtual void on_focus_lost() {}
  /// 鼠标进入回调
  virtual void on_mouse_enter(UiTree& tree) { (void)tree; }
  /// 鼠标离开回调
  virtual void on_mouse_leave(UiTree& tree) { (void)tree; }
  /// 鼠标移动事件处理
  virtual bool on_mouse_move(UiTree& tree, const InputState& input);
  /// 鼠标按下事件处理
  virtual bool on_mouse_down(UiTree& tree, const InputState& input, UiMouseButton button);
  /// 鼠标释放事件处理
  virtual bool on_mouse_up(UiTree& tree, const InputState& input, UiMouseButton button);
  /// 键盘按键事件处理
  virtual bool on_key_down(int virtual_key) {
    (void)virtual_key;
    return false;
  }
  /// 文本输入事件处理
  virtual bool on_text_input(const std::wstring& text) { return false; }
  /// 退格键事件处理
  virtual bool on_backspace() { return false; }
  /// 回车键事件处理
  virtual bool on_enter() { return false; }
  /// 是否接受文本输入（编辑框返回 true）
  [[nodiscard]] virtual bool accepts_text_input() const { return false; }

  RectI bounds{};                           ///< 相对父级的边界矩形
  Anchor anchor{Anchor::top_left};           ///< 锚点定位方式
  bool visible{true};                        ///< 是否可见
  bool enabled{true};                        ///< 是否启用
  bool background{false};                    ///< 是否为背景节点（点击不消费输入）
  bool focusable{false};                     ///< 是否可获得焦点
  bool key_preview{false};                   ///< 是否预览按键（未使用保留）
  bool parent_notify{false};                 ///< 父节点通知标志（未使用保留）
  bool real_hit_test_enabled{false};         ///< 是否启用精灵像素级命中检测
  LegacySpriteRef face{};                    ///< 精灵引用（用于自动加载帧）
  std::shared_ptr<const SpriteFrame> hit_frame{};  ///< 预加载的命中检测帧
  std::wstring text{};                       ///< 节点显示的文本
  UiNode* parent() const { return parent_; }
  std::vector<std::unique_ptr<UiNode>>& children() { return children_; }

 protected:
  UiNode* parent_{nullptr};                         ///< 父节点指针
  std::vector<std::unique_ptr<UiNode>> children_{};  ///< 子节点列表
};

/// 面板节点：带填充色和边框色的矩形面板
class Panel : public UiNode {
 public:
  explicit Panel(RectI bounds);
  void paint(SoftwareRenderer& renderer) override;

  std::uint32_t fill_color{0xFF22252BU};     ///< 填充色（深蓝灰）
  std::uint32_t border_color{0xFF4A5568U};   ///< 边框色（灰蓝）
};

/// 标签节点：显示纯文本
class Label : public UiNode {
 public:
  explicit Label(RectI bounds);
  void paint(SoftwareRenderer& renderer) override;

  std::uint32_t color{0xFFF5F7FAU};  ///< 文字颜色（近白色）
};

/// 按钮节点：支持悬停、按下、选中三种状态的矩形按钮
class Button : public UiNode {
 public:
  explicit Button(RectI bounds);
  void paint(SoftwareRenderer& renderer) override;
  void on_mouse_enter(UiTree& tree) override;
  void on_mouse_leave(UiTree& tree) override;
  bool on_mouse_move(UiTree& tree, const InputState& input) override;
  bool on_mouse_down(UiTree& tree, const InputState& input, UiMouseButton button) override;
  bool on_mouse_up(UiTree& tree, const InputState& input, UiMouseButton button) override;

  std::function<void()> on_click{};  ///< 点击回调
  ButtonStyle style{ButtonStyle::base};  ///< 按钮样式
  bool hovered{false};    ///< 鼠标悬停状态
  bool pressed{false};    ///< 鼠标按下状态
  bool selected{false};   ///< 选中状态（lock/radio 样式使用）
};

/// 精灵按钮节点：使用 WIL 精灵帧作为外观的按钮
class SpriteButton : public Button {
 public:
  SpriteButton(RectI bounds, std::shared_ptr<const SpriteFrame> frame,
               std::shared_ptr<const SpriteFrame> pressed_frame = nullptr);
  void paint(SoftwareRenderer& renderer) override;

  std::shared_ptr<const SpriteFrame> frame{};           ///< 正常状态精灵
  std::shared_ptr<const SpriteFrame> pressed_frame{};   ///< 按下状态精灵（可选）
};

/// 文本编辑框节点：支持文本输入、密码模式、占位符
class TextEdit : public UiNode {
 public:
  explicit TextEdit(RectI bounds);
  void paint(SoftwareRenderer& renderer) override;
  bool on_mouse_down(UiTree& tree, const InputState& input, UiMouseButton button) override;
  bool on_text_input(const std::wstring& text) override;
  bool on_backspace() override;
  bool on_enter() override;
  [[nodiscard]] bool accepts_text_input() const override { return true; }

  /// 获取显示文本（密码模式下返回 ****）
  [[nodiscard]] std::wstring display_text() const;

  std::function<void()> on_submit{};  ///< 回车提交回调
  std::wstring value{};               ///< 当前输入内容
  std::wstring placeholder{};         ///< 占位文本
  bool password_mode{false};          ///< 是否密码模式（显示为 *）
};

/// 列表框节点：垂直列表项选择
class ListBox : public UiNode {
 public:
  explicit ListBox(RectI bounds);
  void paint(SoftwareRenderer& renderer) override;
  bool on_mouse_down(UiTree& tree, const InputState& input, UiMouseButton button) override;

  std::vector<std::wstring> items{};                    ///< 列表项文本
  int selected_index{-1};                               ///< 当前选中项索引
  std::function<void(int)> on_selection_changed{};      ///< 选中项变化回调
};

/// 图片节点：显示精灵帧，无精灵时显示回退色
class Image : public UiNode {
 public:
  explicit Image(RectI bounds);
  void paint(SoftwareRenderer& renderer) override;

  std::uint32_t fallback_fill_color{0x00000000U};    ///< 无精灵时的填充色（默认透明）
  std::uint32_t fallback_border_color{0x00000000U};  ///< 无精灵时的边框色（默认透明）
};

/// 网格节点：二维单元格布局，支持悬停、单击、双击
/// 通过 on_cell_paint 回调自定义每个单元格的渲染
class Grid : public UiNode {
 public:
  explicit Grid(RectI bounds);
  void paint(SoftwareRenderer& renderer) override;
  bool on_mouse_move(UiTree& tree, const InputState& input) override;
  bool on_mouse_down(UiTree& tree, const InputState& input, UiMouseButton button) override;
  bool on_mouse_up(UiTree& tree, const InputState& input, UiMouseButton button) override;

  /// 根据屏幕坐标计算所在单元格
  [[nodiscard]] std::optional<std::pair<int, int>> cell_at(int screen_x, int screen_y) const;
  /// 清除选中状态
  void clear_select();

  int col_count{0};    ///< 列数
  int row_count{0};    ///< 行数
  int col_width{0};    ///< 列宽（像素）
  int row_height{0};   ///< 行高（像素）
  int view_top_line{0};  ///< 顶部可见行（预留滚动支持）
  int selected_col{-1};  ///< 选中列索引
  int selected_row{-1};  ///< 选中行索引
  int hover_col{-1};     ///< 悬停列索引
  int hover_row{-1};     ///< 悬停行索引
  int down_col{-1};      ///< 按下时的列索引
  int down_row{-1};      ///< 按下时的行索引
  std::uint64_t double_click_interval_ms{300};  ///< 双击判定间隔（毫秒）
  std::function<void(Grid&, int, int)> on_cell_hover{};         ///< 单元格悬停回调
  std::function<void(Grid&, int, int)> on_cell_select{};        ///< 单元格单击选中回调
  std::function<void(Grid&, int, int)> on_cell_double_click{};  ///< 单元格双击回调
  std::function<void(Grid&, int, int, const RectI&, bool, SoftwareRenderer&)> on_cell_paint{};  ///< 单元格自定义渲染回调

 private:
  std::uint64_t last_click_ms_{0};  ///< 上次点击时间戳（用于双击检测）
  int last_click_col_{-1};          ///< 上次点击列
  int last_click_row_{-1};          ///< 上次点击行
};

/// 滚动条节点（占位，当前未实现具体功能）
class ScrollBar : public UiNode {
 public:
  explicit ScrollBar(RectI bounds) : UiNode(bounds) {}
};

/// 窗口节点：可拖拽浮动窗口，支持精灵背景、模态显示
class Window : public UiNode {
 public:
  explicit Window(RectI bounds);
  void paint(SoftwareRenderer& renderer) override;
  bool on_mouse_move(UiTree& tree, const InputState& input) override;
  bool on_mouse_down(UiTree& tree, const InputState& input, UiMouseButton button) override;
  bool on_mouse_up(UiTree& tree, const InputState& input, UiMouseButton button) override;

  void show(UiTree& tree);           ///< 显示窗口
  void hide(UiTree& tree);           ///< 隐藏窗口
  void show_modal(UiTree& tree);     ///< 以模态方式显示

  bool floating{false};              ///< 是否可浮动拖拽
  bool dragging{false};              ///< 是否正在拖拽
  int drag_origin_x{0};              ///< 拖拽起始 X
  int drag_origin_y{0};              ///< 拖拽起始 Y
  LegacySpriteRef background_sprite{};  ///< 背景精灵引用
  std::shared_ptr<const SpriteFrame> background_frame{};  ///< 预加载的背景帧
  std::uint32_t fallback_fill_color{0x660B1220U};    ///< 无精灵时的回退填充色（半透明深色）
  std::uint32_t fallback_border_color{0xFF64748BU};  ///< 无精灵时的回退边框色
};

/// 模态对话框节点：全屏半透明遮罩 + 面板
class ModalDialog : public Panel {
 public:
  explicit ModalDialog(RectI bounds) : Panel(bounds) {}
};

/// 工具提示节点：鼠标悬停时显示的文本提示框
class Tooltip : public UiNode {
 public:
  explicit Tooltip(RectI bounds);
  void paint(SoftwareRenderer& renderer) override;
  void show_at(int x, int y, std::wstring text, std::uint32_t color);
  void hide();

  std::uint32_t color{0xFFF8FAFCU};  ///< 文字颜色
  LegacySpriteRef background_sprite{ArchiveId::prguse, 394};  ///< 背景精灵（默认 prguse.wil 索引 394）
  std::shared_ptr<const SpriteFrame> background_frame{};      ///< 预加载的背景帧

 private:
  void layout_with_renderer(SoftwareRenderer& renderer);

  int anchor_x_{0};
  int anchor_y_{0};
  bool layout_dirty_{false};
};

/// 拖放精灵覆盖层：拖拽操作时显示跟随鼠标的精灵
class DragSpriteOverlay : public UiNode {
 public:
  explicit DragSpriteOverlay(RectI bounds);
  void paint(SoftwareRenderer& renderer) override;
  void set_sprite(std::shared_ptr<const SpriteFrame> frame);
  void clear();
  void set_position(int x, int y);

  std::shared_ptr<const SpriteFrame> frame{};  ///< 当前显示的拖拽精灵
};

/// UI 树：管理所有 UI 节点的根容器
///
/// 核心职责：
///   - 输入事件分发：鼠标移动/按下/释放 + 键盘按键/文本输入/退格/回车
///   - 焦点管理：只有 focusable=true 的节点可获得焦点
///   - 模态管理：模态节点接收所有事件，阻塞下层节点
///   - 鼠标捕获：拖拽操作中捕获鼠标移动事件
///   - 自动清理：节点不可见/移出树时自动清理引用
///
/// 输入分发流程：
///   1. 收集当前帧的按键事件（键盘缓冲区）
///   2. 分发鼠标事件到目标节点（先捕获节点，再模态节点，再任意命中）
///   3. 更新悬停状态（触发 on_mouse_enter / on_mouse_leave）
///   4. 分发键盘事件到焦点节点
class UiTree {
 public:
  UiTree();

  /// 设置根节点，清除现有树
  template <typename T, typename... Args>
  T* set_root(Args&&... args) {
    clear();
    root_ = std::make_unique<T>(std::forward<Args>(args)...);
    root_->background = true;
    return static_cast<T*>(root_.get());
  }

  /// 更新 UI 树：处理输入事件并返回消费结果
  UiInputResult update(const InputState& input);
  /// 渲染 UI 树
  void paint(SoftwareRenderer& renderer);
  /// 清除整个 UI 树
  void clear();
  /// 设置资源管理器（用于精灵加载）
  void set_asset_manager(AssetManager* assets) { assets_ = assets; }
  /// 设置焦点到指定节点
  void focus(UiNode* node);
  /// 捕获鼠标输入到指定节点
  void set_capture(UiNode* node);
  /// 释放指定节点的鼠标捕获
  void release_capture(UiNode* node);
  /// 将节点设为模态（阻塞下层节点输入）
  void show_modal(UiNode* node);
  /// 关闭模态
  void close_modal(UiNode* node);
  /// 将节点移到兄弟列表末尾（渲染在最上层）
  void bring_to_front(UiNode* node);
  /// 如果焦点节点是 node 的后代则清除焦点
  void clear_focus_if_descendant(UiNode* node);
  /// 清理 node 后代在树中的所有引用（焦点/捕获/悬停/模态）
  void clear_references_if_descendant(UiNode* node);

  [[nodiscard]] UiNode* root() const { return root_.get(); }
  [[nodiscard]] UiNode* focused() const { return focused_; }
  [[nodiscard]] UiNode* captured() const { return captured_; }
  [[nodiscard]] UiNode* hovered() const { return hovered_; }
  [[nodiscard]] UiNode* modal() const { return modal_; }
  [[nodiscard]] AssetManager* asset_manager() const { return assets_; }

 private:
  /// 优先级命中测试：先检测活动菜单和模态节点
  [[nodiscard]] UiNode* priority_hit_test(int x, int y) const;
  /// 验证节点是否有效（属于树中且可见）
  [[nodiscard]] bool is_valid_target(UiNode* node) const;
  /// 设置悬停节点（触发 enter/leave 事件）
  void set_hovered(UiNode* node);
  /// 清理所有失效节点引用
  void clear_stale_references();
  /// 选择单选按钮（取消同级其他 radio 按钮的选择状态）
  void select_radio_button(Button* button);
  /// 分发鼠标移动事件
  bool dispatch_mouse_move(UiNode* target, const InputState& input);
  /// 分发鼠标按下事件（自动设置焦点）
  bool dispatch_mouse_down(UiNode* target, const InputState& input, UiMouseButton button);
  /// 分发鼠标释放事件
  bool dispatch_mouse_up(UiNode* target, const InputState& input, UiMouseButton button);
  /// 分发键盘事件（按键 + 文本输入 + 退格/回车）
  bool dispatch_keyboard(const InputState& input, UiInputResult& result);

  std::unique_ptr<UiNode> root_{};  ///< 根节点
  AssetManager* assets_{nullptr};   ///< 资源管理器指针
  UiNode* focused_{nullptr};        ///< 当前焦点节点
  UiNode* captured_{nullptr};       ///< 当前捕获节点
  UiNode* hovered_{nullptr};        ///< 当前悬停节点
  UiNode* mouse_down_{nullptr};     ///< 鼠标按下时的目标节点
  UiNode* modal_{nullptr};          ///< 当前模态节点
  UiNode* active_menu_{nullptr};    ///< 当前活动菜单节点（预留）

  friend class Button;  ///< Button 需要访问 select_radio_button
};

}  // namespace mir2::client::ui
