/**
 * @file canonical_login_error.cpp
 * @brief 登录错误码规范化的实现
 *
 * @details 本文件实现了 CanonicalLoginErrorKind 与各协议版本错误响应格式之间的映射。
 * 核心是 canonical_login_error_mapping() 函数，它为每种错误类型定义了：
 * 1. Legacy 协议格式的错误响应（ident/recog/param/tag/series）
 * 2. Client v1 协议格式的错误响应（code/text + 是否断开连接）
 *
 * 此外还提供了从 Legacy 登录结果码（如 -1/-2）到规范化错误枚举的转换函数，
 * 以及错误名称的字符串表示函数。
 *
 * @note 对于安全敏感的错误（如 select_server_rejected），映射表中会要求
 *       Client v1 协议断开客户端连接，以防止未认证的访问。
 * @see canonical_login_error.hpp
 */

#include "protocol/canonical_login_error.hpp"

#include "protocol/legacy_types.hpp"

namespace mir2 {

namespace {

/**
 * @brief 构造一个空的 Legacy 错误响应
 *
 * @details present 设置为 false，表示不需要发送 Legacy 协议的错误响应。
 * 通常用于纯 Client v1 场景或不应响应的错误情况。
 *
 * @return CanonicalLegacyLoginErrorResponse 空响应（present == false）
 */
constexpr CanonicalLegacyLoginErrorResponse no_legacy() { return {}; }

/**
 * @brief 构造一个完整的 Legacy 错误响应
 *
 * @details 创建一个 present 为 true 的 Legacy 错误响应，
 * 包含消息标识符、识别码以及可选的参数/标签/序列号。
 *
 * @param ident 消息标识符（如 kSmPasswdFail, kSmNewChrFail 等）
 * @param recog 识别码/错误码值
 * @param param 参数（可选，默认 0）
 * @param tag 标签（可选，默认 0）
 * @param series 序列号（可选，默认 0）
 * @return CanonicalLegacyLoginErrorResponse 构造完成的 Legacy 错误响应
 */
constexpr CanonicalLegacyLoginErrorResponse legacy(std::uint16_t ident, std::int32_t recog,
                                                   std::uint16_t param = 0,
                                                   std::uint16_t tag = 0,
                                                   std::uint16_t series = 0) {
  return CanonicalLegacyLoginErrorResponse{true, ident, recog, param, tag, series};
}

/**
 * @brief 构造一个不断开连接的 Client v1 错误响应
 *
 * @details 创建一个 disconnect 为 false 的 Client v1 错误响应，
 * 客户端收到后可继续操作而不被断开。
 *
 * @param code 错误码
 * @param text 错误描述文本
 * @return CanonicalClientV1LoginErrorResponse 构造完成的 Client v1 错误响应
 */
constexpr CanonicalClientV1LoginErrorResponse result(std::int32_t code,
                                                     std::string_view text) {
  return CanonicalClientV1LoginErrorResponse{false, code, text};
}

/**
 * @brief 构造一个断开连接的 Client v1 错误响应
 *
 * @details 创建一个 disconnect 为 true 的 Client v1 错误响应，
 * 表示此错误发生后需要将客户端连接断开。
 * 通常用于安全违规、协议错误或严重认证失败。
 *
 * @param code HTTP 风格错误码
 * @param text 错误描述文本
 * @return CanonicalClientV1LoginErrorResponse 构造完成的 Client v1 错误响应
 */
constexpr CanonicalClientV1LoginErrorResponse disconnect(std::int32_t code,
                                                         std::string_view text) {
  return CanonicalClientV1LoginErrorResponse{true, code, text};
}

}  // namespace

CanonicalLoginErrorMapping canonical_login_error_mapping(CanonicalLoginErrorKind kind) {
  switch (kind) {
    // ─── 协议/连接层错误 ────────────────────────────────────
    // 这些错误通常是协议不兼容或状态异常，Client v1 需要断开连接，
    // Legacy 协议无法直接表示这类错误。
    case CanonicalLoginErrorKind::unsupported_login_message:
      return {kind, no_legacy(), disconnect(400, "unsupported_login_message")};
    case CanonicalLoginErrorKind::unsupported_game_message:
      return {kind, no_legacy(), disconnect(400, "unsupported_game_message")};
    case CanonicalLoginErrorKind::protocol_version_mismatch:
      return {kind, no_legacy(), disconnect(426, "protocol_version_mismatch")};
    case CanonicalLoginErrorKind::missing_client_hello:
      return {kind, no_legacy(), disconnect(400, "missing_client_hello")};
    case CanonicalLoginErrorKind::not_authenticated:
      return {kind, no_legacy(), disconnect(401, "not_authenticated")};
    case CanonicalLoginErrorKind::account_mismatch:
      return {kind, no_legacy(), disconnect(403, "account_mismatch")};

    // ─── 登录认证错误 ────────────────────────────────────────
    // 使用 Legacy 的 kSmPasswdFail 消息返回给旧客户端，
    // 同时 Client v1 端返回统一的 login_failed 文本。
    case CanonicalLoginErrorKind::login_empty_credentials:
    case CanonicalLoginErrorKind::login_account_missing:
      return {kind, legacy(kSmPasswdFail, -4), result(-4, "login_failed")};
    case CanonicalLoginErrorKind::login_bad_password:
      return {kind, legacy(kSmPasswdFail, -1), result(-1, "login_failed")};
    case CanonicalLoginErrorKind::login_locked:
      return {kind, legacy(kSmPasswdFail, -2), result(-2, "login_failed")};
    case CanonicalLoginErrorKind::login_banned:
      return {kind, legacy(kSmPasswdFail, -5), result(-5, "login_failed")};
    case CanonicalLoginErrorKind::login_duplicate:
      return {kind, legacy(kSmPasswdFail, -3), result(-3, "login_failed")};

    // ─── 账号管理 ──────────────────────────────────────────
    case CanonicalLoginErrorKind::create_account_failed:
      return {kind, legacy(kSmNewIdFail, 0), result(0, "create_account_failed")};
    case CanonicalLoginErrorKind::update_account_failed:
      return {kind, legacy(kSmUpdateIdFail, -1), result(-1, "update_account_failed")};
    case CanonicalLoginErrorKind::update_account_missing:
      return {kind, legacy(kSmUpdateIdFail, -1), result(-4, "account_not_found")};

    // ─── 密码修改 ──────────────────────────────────────────
    case CanonicalLoginErrorKind::change_password_failed:
      return {kind, legacy(kSmChgPasswdFail, 0), result(0, "change_password_failed")};
    case CanonicalLoginErrorKind::change_password_bad_password:
      return {kind, legacy(kSmChgPasswdFail, -1), result(-1, "change_password_failed")};
    case CanonicalLoginErrorKind::change_password_locked:
      return {kind, legacy(kSmChgPasswdFail, -2), result(-2, "change_password_failed")};

    // ─── 服务器选择 ────────────────────────────────────────
    // 选择服务器被拒绝视为安全违规，Client v1 断开连接。
    case CanonicalLoginErrorKind::select_server_rejected:
      return {kind, legacy(kSmPasswdFail, -4), disconnect(401, "not_authenticated")};
    case CanonicalLoginErrorKind::server_not_found:
      return {kind, no_legacy(), result(0, "server_not_found")};

    // ─── 角色管理 ──────────────────────────────────────────
    // 被拒绝的操作（查询/创建/删除）在 Client v1 中断开认证连接。
    case CanonicalLoginErrorKind::query_characters_rejected:
      return {kind, legacy(kSmQueryChrFail, 0, 0, 0, 1),
              disconnect(401, "not_authenticated")};
    case CanonicalLoginErrorKind::create_character_rejected:
      return {kind, legacy(kSmNewChrFail, 0), disconnect(401, "not_authenticated")};
    case CanonicalLoginErrorKind::invalid_character_name:
      return {kind, legacy(kSmNewChrFail, 0), result(0, "invalid_character_name")};
    case CanonicalLoginErrorKind::character_slots_full:
      return {kind, legacy(kSmNewChrFail, 3), result(3, "character_slots_full")};
    case CanonicalLoginErrorKind::create_character_duplicate:
      return {kind, legacy(kSmNewChrFail, 2), result(2, "create_character_failed")};
    case CanonicalLoginErrorKind::create_character_failed:
      return {kind, legacy(kSmNewChrFail, 4), result(4, "create_character_failed")};
    case CanonicalLoginErrorKind::delete_character_rejected:
      return {kind, legacy(kSmDelChrFail, 0), disconnect(401, "not_authenticated")};
    case CanonicalLoginErrorKind::delete_character_failed:
      return {kind, legacy(kSmDelChrFail, 0), result(0, "delete_character_failed")};

    // ─── 角色选择/进入游戏 ────────────────────────────────
    case CanonicalLoginErrorKind::select_character_rejected:
      return {kind, legacy(kSmStartFail, 0), disconnect(401, "not_authenticated")};
    case CanonicalLoginErrorKind::character_not_found:
      return {kind, legacy(kSmStartFail, 0), disconnect(404, "character_not_found")};
    // 进入世界的令牌无效，Client v1 断开，Legacy 无对应响应。
    case CanonicalLoginErrorKind::invalid_enter_world_token:
      return {kind, no_legacy(), disconnect(401, "invalid_enter_world_token")};
    case CanonicalLoginErrorKind::already_entered_world:
      return {kind, no_legacy(), disconnect(409, "already_entered_world")};
  }
  // 默认返回未认证错误
  return {CanonicalLoginErrorKind::not_authenticated, no_legacy(),
          disconnect(401, "not_authenticated")};
}

CanonicalLoginErrorKind canonical_login_error_from_login_result_code(std::int32_t result_code) {
  switch (result_code) {
    case -1:
      return CanonicalLoginErrorKind::login_bad_password;
    case -2:
      return CanonicalLoginErrorKind::login_locked;
    case -3:
      return CanonicalLoginErrorKind::login_duplicate;
    case -5:
      return CanonicalLoginErrorKind::login_banned;
    case -4:
    case 0:
    default:
      return CanonicalLoginErrorKind::login_account_missing;
  }
}

CanonicalLoginErrorKind canonical_login_error_from_change_password_result_code(
    std::int32_t result_code) {
  switch (result_code) {
    case -1:
      return CanonicalLoginErrorKind::change_password_bad_password;
    case -2:
      return CanonicalLoginErrorKind::change_password_locked;
    case 0:
    default:
      return CanonicalLoginErrorKind::change_password_failed;
  }
}

std::string_view canonical_login_error_name(CanonicalLoginErrorKind kind) {
  switch (kind) {
    case CanonicalLoginErrorKind::unsupported_login_message:
      return "unsupported_login_message";
    case CanonicalLoginErrorKind::unsupported_game_message:
      return "unsupported_game_message";
    case CanonicalLoginErrorKind::protocol_version_mismatch:
      return "protocol_version_mismatch";
    case CanonicalLoginErrorKind::missing_client_hello:
      return "missing_client_hello";
    case CanonicalLoginErrorKind::not_authenticated:
      return "not_authenticated";
    case CanonicalLoginErrorKind::account_mismatch:
      return "account_mismatch";
    case CanonicalLoginErrorKind::login_empty_credentials:
      return "login_empty_credentials";
    case CanonicalLoginErrorKind::login_account_missing:
      return "login_account_missing";
    case CanonicalLoginErrorKind::login_bad_password:
      return "login_bad_password";
    case CanonicalLoginErrorKind::login_locked:
      return "login_locked";
    case CanonicalLoginErrorKind::login_banned:
      return "login_banned";
    case CanonicalLoginErrorKind::login_duplicate:
      return "login_duplicate";
    case CanonicalLoginErrorKind::create_account_failed:
      return "create_account_failed";
    case CanonicalLoginErrorKind::update_account_failed:
      return "update_account_failed";
    case CanonicalLoginErrorKind::update_account_missing:
      return "update_account_missing";
    case CanonicalLoginErrorKind::change_password_failed:
      return "change_password_failed";
    case CanonicalLoginErrorKind::change_password_bad_password:
      return "change_password_bad_password";
    case CanonicalLoginErrorKind::change_password_locked:
      return "change_password_locked";
    case CanonicalLoginErrorKind::select_server_rejected:
      return "select_server_rejected";
    case CanonicalLoginErrorKind::server_not_found:
      return "server_not_found";
    case CanonicalLoginErrorKind::query_characters_rejected:
      return "query_characters_rejected";
    case CanonicalLoginErrorKind::create_character_rejected:
      return "create_character_rejected";
    case CanonicalLoginErrorKind::invalid_character_name:
      return "invalid_character_name";
    case CanonicalLoginErrorKind::character_slots_full:
      return "character_slots_full";
    case CanonicalLoginErrorKind::create_character_duplicate:
      return "create_character_duplicate";
    case CanonicalLoginErrorKind::create_character_failed:
      return "create_character_failed";
    case CanonicalLoginErrorKind::delete_character_rejected:
      return "delete_character_rejected";
    case CanonicalLoginErrorKind::delete_character_failed:
      return "delete_character_failed";
    case CanonicalLoginErrorKind::select_character_rejected:
      return "select_character_rejected";
    case CanonicalLoginErrorKind::character_not_found:
      return "character_not_found";
    case CanonicalLoginErrorKind::invalid_enter_world_token:
      return "invalid_enter_world_token";
    case CanonicalLoginErrorKind::already_entered_world:
      return "already_entered_world";
  }
  return "unknown";
}

}  // namespace mir2
