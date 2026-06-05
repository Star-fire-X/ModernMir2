/**
 * @file legacy_magic_npc_ui.hpp
 * @brief 旧版魔法/NPC/商店界面布局 —— 魔法页面、快捷键设置、NPC 对话、商店买卖的坐标定义
 *
 * @details 定义了魔法技能页面、魔法快捷键绑定对话框、NPC 对话窗口、
 *          商店菜单窗口、出售对话框的完整布局参数。所有坐标基于
 *          800x600 逻辑分辨率，与经典传奇客户端一致。
 *
 * 界面功能：
 * - 魔法页面：显示 5 行已习得魔法（图标 + 名称 + 等级 + 经验 + 快捷键）
 * - 魔法快捷键绑定：F1-F8 快捷键设置对话框
 * - NPC 对话：显示 NPC 文本和可点击的链接列表
 * - 商店菜单：5 行物品列表 + 购买按钮 + 翻页
 * - 出售对话框：物品图标 + 确认/取消按钮
 */

#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "render/software_renderer.hpp"

namespace mir2::client::legacy_magic_npc_ui {

/**
 * @enum LegacyMagicNpcUiTraceLabel
 * @brief 魔法/NPC/商店操作的追踪标签
 */
enum class LegacyMagicNpcUiTraceLabel {
  magic_page_controls_created,          ///< 魔法页面控件创建
  magic_key_dialog_created,             ///< 魔法快捷键对话框创建
  magic_key_buttons_created,            ///< 魔法快捷键按钮创建
  shortcut_open_magic_page,             ///< 快捷键打开魔法页面
  show_state_window_magic_page,         ///< 显示状态窗口的魔法页面
  magic_page_controls_visible,          ///< 魔法页面控件可见
  draw_magic_page_rows,                 ///< 绘制魔法页面行
  click_magic_row,                      ///< 点击魔法行
  show_magic_key_modal,                 ///< 显示魔法快捷键模态框
  select_magic_key,                     ///< 选择快捷键
  send_clear_conflicting_magic_key,     ///< 发送清除冲突快捷键请求
  send_assign_magic_key,                ///< 发送分配快捷键请求
  hide_magic_key_modal,                 ///< 隐藏魔法快捷键模态框
  recv_magic_list_fifo,                 ///< 接收魔法列表（FIFO）
  refresh_magic_page,                   ///< 刷新魔法页面
  click_npc_from_scene_when_ui_not_consumed, ///< UI 未消费时从场景点击 NPC
  send_npc_click_request,               ///< 发送点击 NPC 请求
  recv_npc_dialog_fifo,                 ///< 接收 NPC 对话（FIFO）
  show_merchant_dialog,                 ///< 显示商人对话
  parse_npc_links,                      ///< 解析 NPC 链接文本
  click_npc_link,                       ///< 点击 NPC 链接
  send_npc_dialog_select,               ///< 发送 NPC 对话选择
  wait_server_refresh,                  ///< 等待服务端刷新
  close_npc_dialog_local,               ///< 本地关闭 NPC 对话
  hide_merchant_dialog,                 ///< 隐藏商人对话
  restore_item_bag_origin,              ///< 恢复物品背包到原位
  recv_npc_close_fifo,                  ///< 接收 NPC 关闭消息（FIFO）
  recv_merchant_goods_fifo,             ///< 接收商店物品列表（FIFO）
  show_shop_menu,                       ///< 显示商店菜单
  move_item_bag_for_shop,               ///< 为商店显示移动背包
  select_shop_row,                      ///< 选择商店物品行
  send_merchant_buy,                    ///< 发送购买请求
  recv_inventory_or_gold_fifo,          ///< 接收物品/金币更新（FIFO）
  refresh_shop_or_bag,                  ///< 刷新商店或背包
  open_sell_or_repair_selecting,        ///< 打开出售/修理选择状态
  select_bag_item_for_price,            ///< 选择背包物品询价
  send_price_request,                   ///< 发送询价请求
  recv_price_result_fifo,               ///< 接收价格结果（FIFO）
  show_sell_dialog,                     ///< 显示出售对话框
  confirm_sell_or_repair,               ///< 确认出售或修理
  send_sell_or_repair,                  ///< 发送出售/修理请求
  refresh_bag_or_gold_fifo,             ///< 刷新背包/金币（FIFO）
  recv_storage_list_fifo,               ///< 接收仓库列表（FIFO）
  show_storage_menu,                    ///< 显示仓库菜单
  select_storage_row,                   ///< 选择仓库物品行
  send_storage_withdraw,                ///< 发送仓库取出请求
  open_storage_deposit_selecting,       ///< 打开仓库存入选择状态
  select_bag_item_for_storage,          ///< 选择背包物品存入仓库
  send_storage_deposit,                 ///< 发送仓库存入请求
  refresh_storage_or_bag                ///< 刷新仓库或背包
};

/// 将追踪标签转换为可读字符串
[[nodiscard]] std::string_view legacy_magic_npc_ui_trace_label(
    LegacyMagicNpcUiTraceLabel label);

/**
 * @struct LegacyMagicPageLayout
 * @brief 魔法页面布局参数（状态窗口中的魔法标签页）
 *
 * @details 显示 5 行已习得魔法，每行包含：图标、名称、等级图标、经验图标、快捷键图标。
 */
struct LegacyMagicPageLayout {
  int resource_index{383};             ///< 魔法页面背景精灵索引
  RectI background{38, 52, 177, 197}; ///< 背景区域
  RectI row_hit_area{33, 55, 166, 185};///< 行点击区域
  int row_count{5};                    ///< 可见行数
  int row_height{37};                  ///< 行高（像素）
  int icon_x{46};                      ///< 图标 X 坐标
  int first_row_y{60};                 ///< 第一行 Y 坐标
  int icon_w{31};                      ///< 图标宽度
  int icon_h{33};                      ///< 图标高度
  int name_x{84};                      ///< 名称文本 X 坐标
  int level_icon_resource_index{112};  ///< 等级图标精灵索引基数
  int level_icon_x{84};                ///< 等级图标 X 坐标
  int exp_icon_resource_index{111};    ///< 经验图标精灵索引基数
  int exp_icon_x{110};                 ///< 经验图标 X 坐标
  int key_icon_x{183};                 ///< 快捷键图标 X 坐标
  int key_icon_first_resource_index{248};///< 快捷键图标起始精灵索引

  [[nodiscard]] RectI row_hit_rect(int row) const;
  [[nodiscard]] RectI icon_rect(int row) const;
  [[nodiscard]] int key_icon_resource_index(std::uint8_t key) const;
};

/**
 * @struct LegacyMagicKeyLayout
 * @brief 魔法快捷键绑定对话框布局
 *
 * @details 允许为选中的魔法分配 F1-F8 快捷键。包含 8 个快捷键按钮和确认/取消按钮。
 */
struct LegacyMagicKeyLayout {
  int resource_index{229};             ///< 对话框背景精灵索引
  RectI window{289, 234, 222, 132};   ///< 对话框窗口矩形
  int none_resource_index{230};        ///< "无"按钮精灵索引
  RectI none_button{15, 42, 32, 22};  ///< "无"按钮区域
  int key_first_resource_index{230};   ///< 快捷键按钮起始精灵索引
  RectI ok_button{78, 103, 70, 24};   ///< 确认按钮区域
  int ok_resource_index{62};           ///< 确认按钮精灵索引

  [[nodiscard]] RectI key_button(int key) const;
  [[nodiscard]] int key_resource_index(int key) const;
};

/**
 * @struct LegacyNpcDialogLayout
 * @brief NPC 对话窗口布局
 */
struct LegacyNpcDialogLayout {
  int resource_index{384};             ///< 对话窗口背景精灵索引
  RectI window{0, 0, 420, 180};      ///< 对话窗口矩形
  int close_resource_index{64};        ///< 关闭按钮精灵索引
  RectI close_button{399, 1, 16, 16}; ///< 关闭按钮区域
  int text_x{30};                      ///< 文本起始 X
  int text_y{20};                      ///< 文本起始 Y
  int line_height{16};                 ///< 行高
  int link_hit_height{14};             ///< 链接点击区域高度
};

/**
 * @struct LegacyMerchantMenuLayout
 * @brief 商店菜单布局（购买/仓库界面）
 */
struct LegacyMerchantMenuLayout {
  int resource_index{385};                  ///< 菜单背景精灵索引
  RectI normal_window{138, 163, 320, 210}; ///< 普通窗口矩形
  RectI shop_window{0, 176, 320, 210};    ///< 商店模式窗口矩形
  RectI row_hit_area{14, 32, 265, 5 * 28};///< 行点击区域
  int row_count{5};                         ///< 可见行数
  int row_height{28};                       ///< 行高
  int row_draw_x{27};                       ///< 行绘制 X
  int row_draw_y{28};                       ///< 行绘制 Y 偏移
  int prev_resource_index{387};             ///< 上一页按钮精灵索引
  RectI prev_button{43, 175, 40, 24};      ///< 上一页按钮区域
  int next_resource_index{388};             ///< 下一页按钮精灵索引
  RectI next_button{90, 175, 40, 24};      ///< 下一页按钮区域
  int buy_resource_index{386};              ///< 购买按钮精灵索引
  RectI buy_button{215, 171, 50, 28};      ///< 购买按钮区域
  RectI storage_take_button{174, 171, 50, 28};   ///< 仓库取出按钮区域
  RectI storage_deposit_button{224, 171, 50, 28};///< 仓库存入按钮区域
  int close_resource_index{64};             ///< 关闭按钮精灵索引
  RectI close_button{291, 0, 16, 16};      ///< 关闭按钮区域

  [[nodiscard]] RectI row_button(int row) const;
};

/**
 * @struct LegacySellDialogLayout
 * @brief 出售对话框布局
 */
struct LegacySellDialogLayout {
  int resource_index{392};                  ///< 对话框背景精灵索引
  RectI normal_window{328, 163, 145, 174}; ///< 普通窗口矩形
  RectI shop_sell_window{260, 176, 145, 174};///< 商店出售模式窗口矩形
  RectI spot{27, 67, 61, 52};              ///< 物品图标区域
  int ok_resource_index{393};               ///< 确认按钮精灵索引
  RectI ok_button{85, 150, 60, 24};        ///< 确认按钮区域
  int close_resource_index{64};             ///< 关闭按钮精灵索引
  RectI close_button{115, 0, 16, 16};      ///< 关闭按钮区域
};

[[nodiscard]] LegacyMagicPageLayout legacy_magic_page_layout();
[[nodiscard]] LegacyMagicKeyLayout legacy_magic_key_layout();
[[nodiscard]] LegacyNpcDialogLayout legacy_npc_dialog_layout();
[[nodiscard]] LegacyMerchantMenuLayout legacy_merchant_menu_layout();
[[nodiscard]] LegacySellDialogLayout legacy_sell_dialog_layout();

}  // namespace mir2::client::legacy_magic_npc_ui
