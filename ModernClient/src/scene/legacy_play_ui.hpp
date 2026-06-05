/**
 * @file legacy_play_ui.hpp
 * @brief 旧版游戏 HUD 布局定义 —— 底部状态栏、快捷栏、聊天板、系统消息的坐标常量
 *
 * @details 定义了游戏中所有 HUD 元素的布局参数，包括：
 *          - 底部面板（Bottom Board）：HP/MP/经验/负重/金币显示
 *          - 快捷栏（Quick Belt）：6 格物品快捷使用栏
 *          - 功能按钮：状态、背包、魔法、选项、小地图、交易、行会、组队等
 *          - 聊天板（Chat Board）：底部聊天消息显示区域
 *          - 聊天输入框（Chat Edit）：聊天文本输入区域
 *          - 系统消息（System Message）：屏幕顶部的临时系统提示
 *
 * 所有坐标均基于 800x600 的逻辑分辨率，与经典传奇客户端一致。
 *
 * @note 布局常量通过 legacy_hud_layout() 和 legacy_chat_layout() 函数计算，
 *       允许根据底部面板高度动态调整按钮位置
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "render/software_renderer.hpp"

namespace mir2::client::legacy_play_ui {

/**
 * @enum LegacyPlayUiTraceLabel
 * @brief 游戏 HUD 操作的追踪标签 —— 用于测试和调试的 trace 事件标识
 */
enum class LegacyPlayUiTraceLabel {
  world_scene_enter,                  ///< 进入世界场景
  legacy_hud_root_created,            ///< HUD 根节点创建完成
  bottom_board_show,                  ///< 底部面板显示
  chat_board_show,                    ///< 聊天板显示
  chat_edit_created,                  ///< 聊天输入框创建
  legacy_shortcut_fallback,           ///< 快捷键回退处理
  shortcut_toggle_bag,                ///< 快捷键切换背包（F9）
  shortcut_toggle_state,              ///< 快捷键切换状态窗口（F10）
  shortcut_open_magic_page,           ///< 快捷键打开魔法页面（F11）
  shortcut_open_minimap,              ///< 快捷键打开小地图
  chat_enter_or_space_open,           ///< 回车/空格打开聊天输入
  chat_native_edit_focus,             ///< 聊天原生编辑框获得焦点
  chat_enter_submit,                  ///< 回车提交聊天内容
  queue_chat_send,                    ///< 排队发送聊天消息
  send_chat,                          ///< 实际发送聊天消息
  chat_prefix_open,                   ///< 前缀命令打开（!、@、/ 等）
  chat_slash_uses_whisper_or_literal, ///< '/' 前缀判断为私聊或原样发送
  chat_escape_cancel,                 ///< ESC 取消聊天输入
  chat_close_hide_edit,               ///< 关闭并隐藏聊天编辑框
  recv_chat_line_fifo,                ///< 接收聊天行（FIFO 队列）
  append_chat_board_line,             ///< 追加聊天行到聊天板
  recv_actor_say_fifo,                ///< 接收角色说话（FIFO）
  recv_sys_message_fifo,              ///< 接收系统消息（FIFO）
  append_top_sys_message,             ///< 追加顶部系统消息
  draw_top_system_messages,           ///< 绘制顶部系统消息
  expire_top_system_messages_after_3000ms,  ///< 3 秒后过期系统消息
  paint_bottom_board,                 ///< 绘制底部面板
  paint_hp_mp,                        ///< 绘制 HP/MP 值
  paint_exp_weight_gold,              ///< 绘制经验/负重/金币
  paint_quick_belt,                   ///< 绘制快捷栏
  paint_chat_board                    ///< 绘制聊天板
};

/// 将追踪标签转换为可读字符串（用于日志输出）
[[nodiscard]] std::string_view legacy_play_ui_trace_label(LegacyPlayUiTraceLabel label);

/**
 * @struct LegacyHudLayout
 * @brief 游戏 HUD 布局 —— 所有 HUD 元素的屏幕坐标矩形
 *
 * @details 基于 800x600 逻辑分辨率。所有坐标通过 legacy_hud_layout()
 *          根据底部面板高度动态计算。
 */
struct LegacyHudLayout {
  RectI bottom_board{};              ///< 底部面板矩形
  std::array<RectI, 6> quick_belt{}; ///< 6 格快捷栏矩形
  RectI state_button{};              ///< 状态按钮（F10）
  RectI bag_button{};                ///< 背包按钮（F9）
  RectI magic_button{};              ///< 魔法按钮（F11）
  RectI option_button{};             ///< 选项按钮
  RectI minimap_button{};            ///< 小地图按钮
  RectI trade_button{};              ///< 交易按钮
  RectI guild_button{};              ///< 行会按钮
  RectI group_button{};              ///< 组队按钮
  RectI plus_button{};               ///< 扩展按钮
  RectI logout_button{};             ///< 登出按钮
  RectI exit_button{};               ///< 退出按钮
};

/**
 * @struct LegacyChatLayout
 * @brief 聊天系统布局参数
 */
struct LegacyChatLayout {
  RectI board{};           ///< 聊天板显示区域
  RectI edit{};            ///< 聊天输入框区域
  int visible_lines{9};    ///< 聊天板可见行数
  int line_height{12};     ///< 每行高度（像素）
  int max_length{70};      ///< 每行最大字符数
};

/**
 * @struct LegacySystemMessageLayout
 * @brief 顶部系统消息布局参数
 */
struct LegacySystemMessageLayout {
  RectI bounds{};                  ///< 系统消息显示区域
  int line_height{16};             ///< 每行高度（像素）
  std::size_t max_messages{10};    ///< 最大同时显示消息数
  std::uint64_t expire_ms{3000};   ///< 消息过期时间（毫秒）
  std::uint32_t color{0xFF00FF00U};///< 消息文字颜色（默认绿色）
};

/**
 * @brief 计算 HUD 布局
 *
 * @details 根据底部面板的高度计算所有 HUD 元素的屏幕坐标。
 *          按钮位置相对于底部面板居中排列。
 *
 * @param bottom_height 底部面板高度（像素）
 * @return 完整的 HUD 布局参数
 */
[[nodiscard]] LegacyHudLayout legacy_hud_layout(int bottom_height);

/**
 * @brief 获取聊天布局参数
 *
 * @return 聊天系统的布局参数（基于 800x600 分辨率）
 */
[[nodiscard]] LegacyChatLayout legacy_chat_layout();

/**
 * @brief 获取系统消息布局参数
 *
 * @return 系统消息的布局参数
 */
[[nodiscard]] LegacySystemMessageLayout legacy_system_message_layout();

}  // namespace mir2::client::legacy_play_ui
