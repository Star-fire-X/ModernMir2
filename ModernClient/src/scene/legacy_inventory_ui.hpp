/**
 * @file legacy_inventory_ui.hpp
 * @brief 旧版背包/装备界面布局 —— 背包窗口、装备栏窗口、物品提示的坐标和精灵索引
 *
 * @details 定义了旧版背包界面（46 格）、装备栏界面（13 槽位）和物品提示
 *          的完整布局参数。所有坐标基于 800x600 逻辑分辨率，与经典传奇
 *          客户端的界面布局一致。
 *
 * 界面功能：
 * - 背包窗口：8 列 x 5 行 = 40 个可见格子（共 46 格，前 6 格为快捷栏专用）
 * - 装备栏窗口：9 个可见槽位（武器/衣服/头盔/项链/戒指/手镯/腰带/鞋子/勋章）
 * - 物品提示：鼠标悬停时显示的物品名称和属性文本
 * - 拖动物品叠加层：拖拽物品时跟随鼠标的精灵覆盖层
 *
 * @note legacy_inventory_ui_trace_label 枚举用于跟踪所有背包/装备操作事件，
 *       支持 Delphi 客户端的 trace 回放对比验证
 */

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "render/software_renderer.hpp"
#include "shared/protocol/client_v1/protocol.hpp"

namespace mir2::client::legacy_inventory_ui {

/**
 * @enum LegacyInventoryUiTraceLabel
 * @brief 背包/装备界面操作的追踪标签
 */
enum class LegacyInventoryUiTraceLabel {
  legacy_inventory_windows_created,     ///< 旧版背包窗口创建完成
  item_bag_window_created,              ///< 物品背包窗口创建
  item_grid_created,                    ///< 物品网格创建
  state_window_created,                 ///< 状态窗口创建
  equipment_slots_created,              ///< 装备槽位创建
  item_hint_created,                    ///< 物品提示创建
  moving_item_overlay_created,          ///< 拖动物品覆盖层创建
  show_item_bag,                        ///< 显示物品背包
  arrange_item_bag_origin,              ///< 排列物品背包到原始位置
  hide_item_bag,                        ///< 隐藏物品背包
  show_state_window,                    ///< 显示状态窗口
  hide_state_window,                    ///< 隐藏状态窗口
  mouse_down_bag_cell,                  ///< 鼠标按下背包格子
  start_item_moving_from_bag,           ///< 从背包开始拖动物品
  clear_source_bag_slot,                ///< 清除源背包槽位
  draw_moving_item_overlay,             ///< 绘制拖动物品覆盖层
  cancel_moving_item,                   ///< 取消拖动物品
  restore_item_to_source_slot,          ///< 恢复物品到源槽位
  double_click_bag_cell,                ///< 双击背包格子（使用物品）
  begin_pending_use_item,               ///< 开始等待使用物品确认
  send_use_item,                        ///< 发送使用物品请求
  recv_use_item_result_fifo,            ///< 接收使用物品结果（FIFO）
  clear_or_restore_pending_item,        ///< 清除或恢复待处理的物品
  mouse_down_equipment_slot,            ///< 鼠标按下装备槽位
  start_item_moving_from_equipment,     ///< 从装备栏开始拖动物品
  clear_source_equipment_slot,          ///< 清除源装备槽位
  drop_equipment_item_on_bag_cell,      ///< 将装备物品放到背包格子
  send_takeoff_item,                    ///< 发送脱下装备请求
  server_equipment_message_fifo,        ///< 接收服务端装备消息（FIFO）
  refresh_bag_or_equipment,             ///< 刷新背包或装备显示
  drop_bag_item_on_equipment_slot,      ///< 将背包物品放到装备槽位
  validate_equipment_slot,              ///< 验证装备槽位合法性
  begin_pending_equip_item,             ///< 开始等待装备确认
  send_takeon_item,                     ///< 发送装备物品请求
  hover_bag_or_equipment_item,          ///< 鼠标悬停背包/装备物品
  format_item_hint_backslash_lines,     ///< 格式化物品提示（反斜杠分行）
  show_hint_layer,                      ///< 显示提示层
  hide_hint_when_item_missing_or_moving,///< 物品缺失或移动时隐藏提示
  recv_inventory_message_fifo,          ///< 接收物品消息（FIFO）
  refresh_gold_or_attributes,           ///< 刷新金币或属性显示
  append_system_message                 ///< 追加系统消息
};

/// 将追踪标签转换为可读字符串
[[nodiscard]] std::string_view legacy_inventory_ui_trace_label(
    LegacyInventoryUiTraceLabel label);

/**
 * @struct LegacyBagLayout
 * @brief 背包窗口布局参数
 *
 * @details 背包窗口显示 40 个可见格子（8 列 x 5 行），对应槽位 6-45。
 *          前 6 个槽位（0-5）为快捷栏专用，不在背包窗口中显示。
 *          包含关闭按钮、修理按钮、金币按钮的坐标和精灵索引。
 */
struct LegacyBagLayout {
  int resource_index{3};           ///< 背包窗口背景精灵索引（Prguse.wil）
  RectI window{0, 0, 329, 227};   ///< 背包窗口矩形
  RectI grid{20, 13, 286, 162};   ///< 物品网格区域
  int columns{8};                  ///< 网格列数
  int rows{5};                     ///< 网格行数
  int cell_width{36};              ///< 格子宽度（像素）
  int cell_height{32};             ///< 格子高度（像素）
  int first_visible_slot{6};       ///< 第一个可见槽位（0-5 为快捷栏）
  int last_visible_slot{45};       ///< 最后一个可见槽位（共 46 格，索引 0-45）
  int close_resource_index{371};   ///< 关闭按钮精灵索引
  RectI close_button{309, 203, 14, 20};  ///< 关闭按钮区域
  int repair_resource_index{26};   ///< 修理按钮精灵索引
  RectI repair_button{254, 183, 48, 22};///< 修理按钮区域
  int gold_resource_index{29};     ///< 金币按钮精灵索引
  RectI gold_button{10, 190, 30, 20};   ///< 金币按钮区域
  int icon_count_offset_x{22};     ///< 物品数量图标 X 偏移
  int icon_count_offset_y{20};     ///< 物品数量图标 Y 偏移

  /// 根据行列计算对应的槽位索引
  [[nodiscard]] int slot_for_cell(int col, int row) const {
    return first_visible_slot + col + row * columns;
  }
};

/**
 * @struct LegacyEquipmentLayout
 * @brief 装备栏窗口布局参数
 *
 * @details 装备栏窗口显示 9 个可见槽位，对应传奇的 13 个装备槽位中
 *          在装备界面上显示的部分（部分槽位如毒符位在经典客户端中不显示）。
 */
struct LegacyEquipmentLayout {
  int resource_index{370};         ///< 装备窗口背景精灵索引
  RectI window{};                  ///< 装备窗口矩形
  int close_resource_index{371};   ///< 关闭按钮精灵索引
  RectI close_button{8, 39, 14, 20};     ///< 关闭按钮区域
  int prev_resource_index{373};    ///< 上一页按钮精灵索引
  RectI prev_button{7, 128, 22, 24};     ///< 上一页按钮区域
  int next_resource_index{372};    ///< 下一页按钮精灵索引
  RectI next_button{7, 187, 22, 24};     ///< 下一页按钮区域
  int magic_page_up_resource_index{398};  ///< 魔法页上翻按钮精灵索引
  RectI magic_page_up_button{213, 113, 22, 24};   ///< 魔法页上翻区域
  int magic_page_down_resource_index{396};///< 魔法页下翻按钮精灵索引
  RectI magic_page_down_button{213, 143, 22, 24}; ///< 魔法页下翻区域
  std::array<RectI, 9> visible_slots{};  ///< 9 个可见装备槽位坐标
};

/**
 * @struct LegacyItemHintLayout
 * @brief 物品提示布局参数
 *
 * @details 物品提示使用反斜杠（\\）作为逻辑分隔符，渲染时转换为换行符。
 *          提示框位置相对于鼠标偏移 12x16 像素。
 */
struct LegacyItemHintLayout {
  int mouse_offset_x{12};          ///< 提示框相对于鼠标的 X 偏移
  int mouse_offset_y{16};          ///< 提示框相对于鼠标的 Y 偏移
  wchar_t logical_separator{L'\\'};///< 逻辑分隔符（数据存储格式）
  wchar_t render_separator{L'\n'}; ///< 渲染分隔符（显示格式）
};

[[nodiscard]] LegacyBagLayout legacy_bag_layout();
[[nodiscard]] LegacyEquipmentLayout legacy_equipment_layout(int frame_width);
[[nodiscard]] LegacyItemHintLayout legacy_item_hint_layout();

/// 计算拖动物品覆盖层的屏幕矩形
[[nodiscard]] RectI legacy_moving_item_overlay_rect(int mouse_x, int mouse_y, int width,
                                                    int height);
/// 生成物品提示文本（名称、属性、持久度等）
[[nodiscard]] std::wstring legacy_item_hint_text(const client_v1::ItemState& item);
/// 将逻辑提示文本转换为渲染文本（分隔符替换）
[[nodiscard]] std::wstring legacy_hint_to_render_text(std::wstring text);
/// 根据物品品质 std_mode 获取提示颜色
[[nodiscard]] std::uint32_t legacy_item_hint_color(std::uint8_t std_mode);

}  // namespace mir2::client::legacy_inventory_ui
