/**
 * @file canonical_login_error.hpp
 * @brief 登录错误码的规范化定义
 *
 * @details 本文件定义了统一的登录错误枚举类型 CanonicalLoginErrorKind，
 * 涵盖从身份验证、账号管理到角色选择等各个阶段的错误场景。
 * 同时提供了将规范化错误码映射回不同协议版本（Legacy/Client v1）
 * 错误响应格式的映射机制，确保服务器内部使用统一的错误表示，
 * 而对外返回客户端各自协议兼容的错误格式。
 */

#pragma once

#include <cstdint>
#include <string_view>

namespace mir2 {

/**
 * @enum CanonicalLoginErrorKind
 * @brief 规范化的登录错误类型枚举
 *
 * @details 覆盖登录全流程中所有可能出现的错误情况，
 * 包括：协议错误、认证失败、账号操作失败、密码修改失败、
 * 服务器选择错误、角色操作错误以及进入游戏阶段错误等。
 * 这是服务器内部统一的错误表示，与具体客户端协议无关。
 */
enum class CanonicalLoginErrorKind {
  unsupported_login_message,          ///< 不支持的登录协议消息
  unsupported_game_message,           ///< 不支持的游戏中协议消息
  protocol_version_mismatch,          ///< 协议版本不匹配
  missing_client_hello,               ///< 缺少客户端握手消息
  not_authenticated,                  ///< 未通过身份认证
  account_mismatch,                   ///< 账号不匹配
  login_empty_credentials,            ///< 登录凭证为空
  login_account_missing,              ///< 账号不存在
  login_bad_password,                 ///< 密码错误
  login_locked,                       ///< 账号被锁定
  login_banned,                       ///< 账号被封禁
  login_duplicate,                    ///< 账号重复登录
  create_account_failed,              ///< 创建账号失败
  update_account_failed,              ///< 更新账号信息失败
  update_account_missing,             ///< 要更新的账号不存在
  change_password_failed,             ///< 修改密码失败
  change_password_bad_password,       ///< 原密码错误（修改密码时）
  change_password_locked,             ///< 账号被锁定（修改密码时）
  select_server_rejected,             ///< 选择服务器被拒绝
  server_not_found,                   ///< 服务器未找到
  query_characters_rejected,          ///< 查询角色列表被拒绝
  create_character_rejected,          ///< 创建角色被拒绝
  invalid_character_name,             ///< 无效的角色名称
  character_slots_full,               ///< 角色槽位已满
  create_character_duplicate,         ///< 角色名重复
  create_character_failed,            ///< 创建角色失败
  delete_character_rejected,          ///< 删除角色被拒绝
  delete_character_failed,            ///< 删除角色失败
  select_character_rejected,          ///< 选择角色被拒绝
  character_not_found,                ///< 角色不存在
  invalid_enter_world_token,          ///< 无效的进入游戏令牌
  already_entered_world               ///< 已进入游戏世界
};

/**
 * @struct CanonicalLegacyLoginErrorResponse
 * @brief Legacy 协议登录错误响应格式
 *
 * @details 对应于传统 Legacy 协议的服务器消息格式，
 * 通过 ident/recog/param/tag/series 五个字段
 * 向旧版客户端传递错误信息。
 * present 字段指示是否需要发送此响应。
 */
struct CanonicalLegacyLoginErrorResponse {
  bool present{false};     ///< 是否需要发送此 Legacy 错误响应
  std::uint16_t ident{0};  ///< 消息标识符（如 kSmPasswdFail）
  std::int32_t recog{0};   ///< 识别码/错误码值
  std::uint16_t param{0};  ///< 参数
  std::uint16_t tag{0};    ///< 标签
  std::uint16_t series{0}; ///< 序列号
};

/**
 * @struct CanonicalClientV1LoginErrorResponse
 * @brief Client v1 协议登录错误响应格式
 *
 * @details 对应于新版 Client v1 协议的错误响应格式，
 * 包含断开连接标志、错误码和文本描述。
 * disconnect 为 true 时表示需要断开客户端连接。
 */
struct CanonicalClientV1LoginErrorResponse {
  bool disconnect{false};  ///< 是否需要断开客户端连接
  std::int32_t code{0};    ///< 错误码
  std::string_view text{}; ///< 错误描述文本
};

/**
 * @struct CanonicalLoginErrorMapping
 * @brief 规范化登录错误的协议映射
 *
 * @details 将一个 CanonicalLoginErrorKind 同时映射到 Legacy 和 Client v1
 * 两种协议格式的错误响应。上层逻辑只需要产生统一的错误枚举，
 * 由下层框架根据客户端协议类型选择对应的响应格式发送。
 */
struct CanonicalLoginErrorMapping {
  CanonicalLoginErrorKind kind{CanonicalLoginErrorKind::not_authenticated};  ///< 错误类型
  CanonicalLegacyLoginErrorResponse legacy{};    ///< 对应的 Legacy 协议错误响应
  CanonicalClientV1LoginErrorResponse client_v1{};  ///< 对应的 Client v1 协议错误响应
};

/**
 * @brief 获取规范化登录错误的协议映射表
 *
 * @details 根据规范化的错误枚举值，查找其对应的 Legacy 和 Client v1
 * 协议格式的错误响应数据。用于根据实际客户端类型选择正确格式的
 * 错误包发送给客户端。
 *
 * @param kind 规范化登录错误类型
 * @return CanonicalLoginErrorMapping 包含两种协议格式的映射结果
 */
[[nodiscard]] CanonicalLoginErrorMapping canonical_login_error_mapping(
    CanonicalLoginErrorKind kind);

/**
 * @brief 从 Legacy 登录结果码转换为规范化错误枚举
 *
 * @details 将 Legacy 服务器返回的登录结果码（如 -1/-2/-3/-4/-5）
 * 映射为统一的 CanonicalLoginErrorKind 枚举值。
 *
 * @param result_code Legacy 登录结果码
 * @return CanonicalLoginErrorKind 对应的规范化错误类型
 */
[[nodiscard]] CanonicalLoginErrorKind canonical_login_error_from_login_result_code(
    std::int32_t result_code);

/**
 * @brief 从 Legacy 修改密码结果码转换为规范化错误枚举
 *
 * @details 将 Legacy 服务器返回的修改密码结果码（如 -1/-2）
 * 映射为统一的 CanonicalLoginErrorKind 枚举值。
 *
 * @param result_code Legacy 修改密码结果码
 * @return CanonicalLoginErrorKind 对应的规范化错误类型
 */
[[nodiscard]] CanonicalLoginErrorKind canonical_login_error_from_change_password_result_code(
    std::int32_t result_code);

/**
 * @brief 获取规范化登录错误的英文名称
 *
 * @details 返回 CanonicalLoginErrorKind 枚举值对应的可读英文名称，
 * 用于日志记录、调试和错误展示。
 *
 * @param kind 规范化登录错误类型
 * @return std::string_view 错误名称的字符串视图
 */
[[nodiscard]] std::string_view canonical_login_error_name(CanonicalLoginErrorKind kind);

}  // namespace mir2
