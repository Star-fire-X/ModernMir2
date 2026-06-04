/**
 * @file legacy_trade_group_guild_ui.hpp
 * @brief 旧版交易/组队/行会界面布局 —— 交易窗口、组队面板、行会面板的坐标定义
 *
 * @details 定义了交易、组队、行会三个社交功能界面的完整布局参数。
 *          所有坐标基于 800x600 逻辑分辨率，与经典传奇客户端一致。
 *
 * 界面功能：
 * - 组队窗口：显示队员列表、允许组队开关、创建/添加/移除队员按钮
 * - 交易窗口：本地和远程两个面板（5x2 物品网格 + 金币 + 确认按钮）
 * - 行会窗口：成员列表、管理按钮（首页/列表/聊天/添加/移除/编辑公告/编辑等级等）
 * - 社交输入提示：通用名称输入对话框
 */

#pragma once

#include <cstdint>
#include <string_view>

#include "render/software_renderer.hpp"

namespace mir2::client::legacy_trade_group_guild_ui {

/**
 * @enum LegacyTradeGroupGuildUiTraceLabel
 * @brief 交易/组队/行会操作的追踪标签
 */
enum class LegacyTradeGroupGuildUiTraceLabel {
  group_window_created,              ///< 组队窗口创建
  trade_remote_window_created,       ///< 交易远程窗口创建
  trade_local_window_created,        ///< 交易本地窗口创建
  guild_window_created,              ///< 行会窗口创建
  social_prompt_created,             ///< 社交输入提示创建
  shortcut_open_group,               ///< 快捷键打开组队窗口
  show_group_window,                 ///< 显示组队窗口
  click_group_allow,                 ///< 点击允许组队
  send_group_mode,                   ///< 发送组队模式
  recv_group_state_fifo,             ///< 接收组队状态（FIFO）
  refresh_group_window,              ///< 刷新组队窗口
  click_group_create,                ///< 点击创建队伍
  show_group_name_prompt,            ///< 显示队伍名称输入提示
  send_create_group,                 ///< 发送创建队伍请求
  refresh_group_members,             ///< 刷新队伍成员
  click_group_add,                   ///< 点击添加队员
  send_add_group_member,             ///< 发送添加队员请求
  click_group_remove,                ///< 点击移除队员
  send_remove_group_member,          ///< 发送移除队员请求
  shortcut_open_trade,               ///< 快捷键打开交易窗口
  send_trade_try,                    ///< 发送交易请求
  recv_trade_state_fifo,             ///< 接收交易状态（FIFO）
  show_trade_remote_window,          ///< 显示远程交易窗口
  show_trade_local_window,           ///< 显示本地交易窗口
  close_trade_window,                ///< 关闭交易窗口
  send_trade_cancel,                 ///< 发送取消交易请求
  hide_trade_windows,                ///< 隐藏交易窗口
  drop_bag_item_on_trade_grid,       ///< 将背包物品放到交易网格
  send_trade_add_item,               ///< 发送添加交易物品请求
  refresh_trade_items,               ///< 刷新交易物品
  drag_trade_item_back_to_bag,       ///< 将交易物品拖回背包
  send_trade_remove_item,            ///< 发送移除交易物品请求
  click_trade_gold,                  ///< 点击交易金币
  show_trade_gold_prompt,            ///< 显示交易金币输入提示
  send_trade_gold,                   ///< 发送交易金币请求
  click_trade_accept,                ///< 点击确认交易
  send_trade_accept,                 ///< 发送交易确认请求
  refresh_trade_accept,              ///< 刷新交易确认状态
  shortcut_open_guild,               ///< 快捷键打开行会窗口
  send_guild_open,                   ///< 发送打开行会请求
  recv_guild_state_fifo,             ///< 接收行会状态（FIFO）
  show_guild_window,                 ///< 显示行会窗口
  click_guild_home,                  ///< 点击行会首页
  send_guild_home,                   ///< 发送行会首页请求
  click_guild_members,               ///< 点击行会成员列表
  send_guild_members,                ///< 发送行会成员列表请求
  refresh_guild_lines,               ///< 刷新行会文本行
  click_guild_add_member,            ///< 点击添加行会成员
  show_guild_name_prompt,            ///< 显示行会名称输入提示
  send_guild_add,                    ///< 发送添加行会成员请求
  refresh_guild_members,             ///< 刷新行会成员
  click_guild_remove_member,         ///< 点击移除行会成员
  send_guild_remove,                 ///< 发送移除行会成员请求
  click_guild_scroll_down,           ///< 点击行会列表下滚
  guild_top_line_plus_three,         ///< 行会顶部行 +3
  click_guild_scroll_up,             ///< 点击行会列表上滚
  guild_top_line_minus_three,        ///< 行会顶部行 -3
  click_guild_chat_toggle,           ///< 点击行会聊天切换
  show_guild_chat_lines_or_noop,     ///< 显示行会聊天行或空操作
  append_system_message              ///< 追加系统消息
};

/// 将追踪标签转换为可读字符串
[[nodiscard]] std::string_view legacy_trade_group_guild_ui_trace_label(
    LegacyTradeGroupGuildUiTraceLabel label);

/** @struct LegacyGroupLayout 组队窗口布局 */
struct LegacyGroupLayout {
  int resource_index{120};             ///< 窗口背景精灵索引
  RectI window{262, 179, 276, 242};   ///< 窗口矩形
  int close_resource_index{64};        ///< 关闭按钮精灵索引
  RectI close_button{260, 0, 16, 16}; ///< 关闭按钮区域
  int allow_resource_index{122};       ///< 允许组队按钮精灵索引
  RectI allow_button{20, 18, 20, 19}; ///< 允许组队按钮区域
  int create_resource_index{123};      ///< 创建队伍按钮精灵索引
  RectI create_button{22, 203, 72, 19};///< 创建队伍按钮区域
  int add_resource_index{124};         ///< 添加队员按钮精灵索引
  RectI add_button{97, 203, 72, 19};  ///< 添加队员按钮区域
  int remove_resource_index{125};      ///< 移除队员按钮精灵索引
  RectI remove_button{172, 203, 72, 19};///< 移除队员按钮区域
  int member_x{28};                    ///< 队员名 X 坐标
  int member_y{80};                    ///< 队员名起始 Y 坐标
  int member_second_column_x{128};     ///< 第二列队员名 X 坐标
  int member_row_height{16};           ///< 队员行高

  [[nodiscard]] RectI member_text_rect(int index) const;
};

/** @struct LegacyTradeLayout 交易窗口布局 */
struct LegacyTradeLayout {
  int local_resource_index{389};       ///< 本地窗口背景精灵索引
  int remote_resource_index{390};      ///< 远程窗口背景精灵索引
  RectI remote_window{464, 0, 220, 175};   ///< 远程窗口矩形
  RectI local_window{464, 160, 236, 175};  ///< 本地窗口矩形
  RectI grid{21, 56, 180, 66};        ///< 物品网格区域
  int grid_columns{5};                 ///< 网格列数
  int grid_rows{2};                    ///< 网格行数
  int cell_width{36};                  ///< 格子宽度
  int cell_height{33};                 ///< 格子高度
  int ok_resource_index{391};          ///< 确认按钮精灵索引
  RectI ok_button{155, 128, 44, 16};  ///< 确认按钮区域
  int close_resource_index{64};        ///< 关闭按钮精灵索引
  RectI close_button{220, 42, 16, 16};///< 关闭按钮区域
  int gold_resource_index{28};         ///< 金币按钮精灵索引
  RectI gold_button{11, 137, 44, 16}; ///< 金币按钮区域
  int gold_text_x{64};                 ///< 金币文本 X 坐标
  int gold_text_y{131};                ///< 金币文本 Y 坐标
  int name_center_x{59};               ///< 名称居中 X
  int name_center_width{106};          ///< 名称居中区域宽度
  int name_y{6};                       ///< 名称 Y 坐标

  [[nodiscard]] RectI cell_rect(int slot) const;
  [[nodiscard]] int slot_at(int x, int y) const;
};

/** @struct LegacyGuildLayout 行会窗口布局 */
struct LegacyGuildLayout {
  int resource_index{180};             ///< 窗口背景精灵索引
  RectI window{0, -3, 628, 453};     ///< 窗口矩形
  int close_resource_index{64};        ///< 关闭按钮精灵索引
  RectI close_button{584, 6, 16, 16}; ///< 关闭按钮区域
  int home_resource_index{198};        ///< 首页按钮精灵索引
  RectI home_button{13, 411, 64, 16}; ///< 首页按钮区域
  int list_resource_index{200};        ///< 列表按钮精灵索引
  RectI list_button{13, 429, 64, 16}; ///< 列表按钮区域
  int chat_resource_index{190};        ///< 聊天按钮精灵索引
  RectI chat_button{94, 429, 64, 16}; ///< 聊天按钮区域
  int add_resource_index{182};         ///< 添加按钮精灵索引
  RectI add_button{243, 411, 64, 16}; ///< 添加按钮区域
  int remove_resource_index{192};      ///< 移除按钮精灵索引
  RectI remove_button{243, 429, 64, 16};///< 移除按钮区域
  int edit_notice_resource_index{196}; ///< 编辑公告按钮精灵索引
  RectI edit_notice_button{325, 411, 64, 16};///< 编辑公告按钮区域
  int edit_grade_resource_index{194};  ///< 编辑等级按钮精灵索引
  RectI edit_grade_button{325, 429, 64, 16};///< 编辑等级按钮区域
  int ally_resource_index{184};        ///< 结盟按钮精灵索引
  RectI ally_button{407, 411, 64, 16};///< 结盟按钮区域
  int break_ally_resource_index{186};  ///< 解盟按钮精灵索引
  RectI break_ally_button{407, 429, 64, 16};///< 解盟按钮区域
  int war_resource_index{202};         ///< 宣战按钮精灵索引
  RectI war_button{529, 411, 64, 16}; ///< 宣战按钮区域
  int cancel_war_resource_index{188};  ///< 停战按钮精灵索引
  RectI cancel_war_button{529, 429, 64, 16};///< 停战按钮区域
  int up_resource_index{373};          ///< 上滚按钮精灵索引
  RectI up_button{595, 239, 16, 16};  ///< 上滚按钮区域
  int down_resource_index{372};        ///< 下滚按钮精灵索引
  RectI down_button{595, 291, 16, 16};///< 下滚按钮区域
  int title_x{320};                    ///< 标题 X 坐标
  int title_y{13};                     ///< 标题 Y 坐标
  int line_x{24};                      ///< 文本行 X 坐标
  int line_y{41};                      ///< 文本行起始 Y 坐标
  int line_height{14};                 ///< 行高
  int max_line_pixel_height{356};      ///< 最大可见高度
  int scroll_step{3};                  ///< 滚动步长（行数）

  [[nodiscard]] int max_visible_lines() const;
};

/** @struct LegacySocialPromptLayout 社交输入提示布局（通用名称输入） */
struct LegacySocialPromptLayout {
  RectI window{244, 244, 312, 112};   ///< 对话框窗口矩形
  RectI edit{28, 42, 196, 24};        ///< 输入框区域
  RectI ok_button{232, 38, 54, 24};   ///< 确认按钮区域
  RectI cancel_button{232, 68, 54, 24};///< 取消按钮区域
};

[[nodiscard]] LegacyGroupLayout legacy_group_layout();
[[nodiscard]] LegacyTradeLayout legacy_trade_layout();
[[nodiscard]] LegacyGuildLayout legacy_guild_layout();
[[nodiscard]] LegacySocialPromptLayout legacy_social_prompt_layout();

}  // namespace mir2::client::legacy_trade_group_guild_ui
