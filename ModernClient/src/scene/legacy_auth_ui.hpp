/**
 * @file legacy_auth_ui.hpp
 * @brief 旧版登录/认证界面布局 —— 登录、注册、选服、选角、创建角色界面的坐标定义
 *
 * @details 定义了客户端所有认证流程相关界面的布局参数，包括：
 *          - 登录界面：账号/密码输入框、登录/注册/改密按钮
 *          - 服务器选择界面：服务器列表按钮
 *          - 消息提示模态框：确定/是/否/取消按钮
 *          - 角色选择界面：左右箭头、开始/新建/删除按钮、角色信息文本位置
 *          - 创建角色界面：名称输入、职业选择（战/法/道）、性别选择、发型切换
 *
 * 所有布局基于 800x600 逻辑分辨率，使用精灵帧模板（spritesheet reference）
 * 计算居中位置。与经典传奇客户端界面布局一致。
 */

#pragma once

#include <cstddef>
#include <string_view>

#include "render/software_renderer.hpp"

namespace mir2::client::legacy_auth_ui {

/**
 * @enum LegacyAuthUiTraceLabel
 * @brief 认证流程操作的追踪标签
 */
enum class LegacyAuthUiTraceLabel {
  account_enter_focus_password,         ///< 账号输入完成，焦点移到密码框
  password_enter_send_login,            ///< 密码输入完成，发送登录请求
  send_login,                           ///< 发送登录请求
  recv_login_failure,                   ///< 接收登录失败
  show_login_error_modal,               ///< 显示登录错误模态框
  modal_ok,                             ///< 模态框点击确定
  focus_login_password,                 ///< 焦点移回密码框
  recv_login_success,                   ///< 接收登录成功
  recv_server_list,                     ///< 接收服务器列表
  show_server_select,                   ///< 显示服务器选择界面
  click_server_select,                  ///< 点击选择服务器
  send_select_server,                   ///< 发送选择服务器请求
  recv_select_server_ok,                ///< 接收选服成功
  connect_character_gateway,            ///< 连接角色网关
  send_query_character,                 ///< 发送查询角色列表
  recv_query_character,                 ///< 接收角色列表
  show_character_select,                ///< 显示角色选择界面
  select_character_slot,                ///< 选择角色槽位
  click_start_character,                ///< 点击开始游戏
  send_select_character,                ///< 发送选择角色请求
  recv_start_play,                      ///< 接收开始游戏确认
  connect_game_gateway,                 ///< 连接游戏网关
  show_login_notice_or_loading,         ///< 显示登录公告或加载画面
  login_notice_ok,                      ///< 登录公告确认
  waiting_world_snapshot,               ///< 等待世界快照
  open_create_character_dialog,         ///< 打开创建角色对话框
  focus_create_character_name,          ///< 焦点移到创建角色名称框
  send_new_character,                   ///< 发送创建角色请求
  recv_new_character_success,           ///< 接收创建角色成功
  refresh_character_slots,              ///< 刷新角色槽位
  click_delete_character,               ///< 点击删除角色
  confirm_delete_character,             ///< 确认删除角色
  send_delete_character,                ///< 发送删除角色请求
  recv_delete_character_success         ///< 接收删除角色成功
};

/// 将追踪标签转换为可读字符串
[[nodiscard]] std::string_view legacy_auth_ui_trace_label(LegacyAuthUiTraceLabel label);

/**
 * @struct LegacyLoginLayout
 * @brief 登录界面布局
 */
struct LegacyLoginLayout {
  RectI dialog{};                  ///< 登录对话框矩形
  RectI account_edit{};            ///< 账号输入框区域
  RectI password_edit{};           ///< 密码输入框区域
  RectI change_password_button{};  ///< 修改密码按钮区域
  RectI create_account_button{};   ///< 注册账号按钮区域
  RectI login_button{};            ///< 登录按钮区域
  RectI close_button{};            ///< 关闭按钮区域
};

/**
 * @struct LegacyServerSelectLayout
 * @brief 服务器选择界面布局
 */
struct LegacyServerSelectLayout {
  RectI dialog{};                  ///< 服务器选择对话框矩形
  RectI close_button{};            ///< 关闭按钮区域
  int row_top{0};                  ///< 第一行顶部 Y 坐标
  int row_gap{42};                 ///< 行间距（像素）
  int dialog_sprite_index{256};    ///< 对话框精灵帧索引
  bool dialog_uses_prguse2{false}; ///< 对话框是否使用 Prguse2.wil

  [[nodiscard]] RectI server_button(std::size_t index) const;
};

/**
 * @struct LegacyMessageModalLayout
 * @brief 消息提示模态框布局（支持 OK/Yes/No/Cancel 按钮组合）
 */
struct LegacyMessageModalLayout {
  RectI dialog{};          ///< 模态对话框矩形
  RectI title_origin{};    ///< 标题文本起始位置
  RectI text_origin{};     ///< 正文文本起始位置
  RectI ok_button{};       ///< 确定按钮区域
  RectI yes_button{};      ///< 是按钮区域
  RectI no_button{};       ///< 否按钮区域
  RectI cancel_button{};   ///< 取消按钮区域
};

/**
 * @struct LegacyCharacterSelectLayout
 * @brief 角色选择界面布局
 */
struct LegacyCharacterSelectLayout {
  RectI left_button{};       ///< 左箭头按钮区域
  RectI right_button{};      ///< 右箭头按钮区域
  RectI start_button{};      ///< 开始游戏按钮区域
  RectI new_button{};        ///< 新建角色按钮区域
  RectI erase_button{};      ///< 删除角色按钮区域
  RectI left_name_text{};    ///< 左侧角色名文本位置
  RectI left_level_text{};   ///< 左侧角色等级文本位置
  RectI left_job_text{};     ///< 左侧角色职业文本位置
  RectI right_name_text{};   ///< 右侧角色名文本位置
  RectI right_level_text{};  ///< 右侧角色等级文本位置
  RectI right_job_text{};    ///< 右侧角色职业文本位置
  RectI server_name_text{};  ///< 服务器名文本位置
};

/**
 * @struct LegacyCreateCharacterLayout
 * @brief 创建角色界面布局
 */
struct LegacyCreateCharacterLayout {
  RectI dialog{};              ///< 创建角色对话框矩形
  RectI name_edit{};           ///< 角色名输入框区域
  RectI warrior_button{};      ///< 战士职业按钮区域
  RectI wizard_button{};       ///< 法师职业按钮区域
  RectI taoist_button{};       ///< 道士职业按钮区域
  RectI male_button{};         ///< 男性按钮区域
  RectI female_button{};       ///< 女性按钮区域
  RectI prev_hair_button{};    ///< 上一个发型按钮区域
  RectI next_hair_button{};    ///< 下一个发型按钮区域
  RectI ok_button{};           ///< 确认按钮区域
  RectI close_button{};        ///< 取消按钮区域
};

/// 计算居中矩形（根据精灵模板尺寸）
[[nodiscard]] RectI legacy_centered_rect(RectI sprite_template);
[[nodiscard]] LegacyLoginLayout legacy_login_layout(RectI dialog_template);
[[nodiscard]] LegacyServerSelectLayout legacy_server_select_layout(RectI dialog_template,
                                                                   std::size_t visible_count);
[[nodiscard]] LegacyMessageModalLayout legacy_message_modal_layout(RectI dialog_template);
[[nodiscard]] LegacyCharacterSelectLayout legacy_character_select_layout();
[[nodiscard]] LegacyCreateCharacterLayout legacy_create_character_layout(
    RectI dialog_template, RectI job_button_template, RectI sex_button_template,
    RectI prev_hair_button_template, RectI next_hair_button_template, RectI ok_button_template,
    RectI close_button_template);

}  // namespace mir2::client::legacy_auth_ui
