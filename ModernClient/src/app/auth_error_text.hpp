/**
 * @file auth_error_text.hpp
 * @brief 认证错误消息本地化 —— 将协议返回的错误码转换为中文用户提示
 *
 * @details 将登录/注册/改密/选服/选角/进入世界等认证流程中服务端返回的
 *          错误码（如 -1 密码错误、-2 账户锁定等）转换为对应的中文错误消息。
 *          消息文本与经典传奇客户端的错误提示保持一致。
 */

#pragma once

#include <string>
#include <string_view>

#include "text/encoding.hpp"

namespace mir2::client {

enum class AuthErrorContext {
  login,
  create_account,
  update_account,
  change_password,
  select_server,
  query_characters,
  create_character,
  delete_character,
  select_character,
  enter_world,
  disconnect
};

namespace detail {

[[nodiscard]] inline std::wstring widen_token(const std::string_view token) {
  return text::utf8_to_wide(std::string(token));
}

[[nodiscard]] inline bool token_is_empty_or(const std::string_view token,
                                            const std::string_view expected) {
  return token.empty() || token == expected;
}

}  // namespace detail

[[nodiscard]] inline std::wstring legacy_auth_error_message(
    const AuthErrorContext context, const int code, const std::string_view token,
    const std::string_view subject = {}) {
  switch (context) {
    case AuthErrorContext::login:
      if (!detail::token_is_empty_or(token, "login_failed")) {
        return detail::widen_token(token);
      }
      switch (code) {
        case -1:
          return L"密码错误.";
        case -2:
          return L"连续三次密码错误。\\你将在一段时间内无法再次连接。.";
        case -3:
          return L"这个帐号正在使用，或者是被异常的终止锁定了,\\请稍后再试。";
        case -4:
        case 0:
          return L"这个帐户不能正确访问。\\请改变帐户,\\或者申请付费注册。";
        case -5:
          return L"这个帐户被禁止了.";
        default:
          return L"ID不存在或未知错误.";
      }

    case AuthErrorContext::create_account:
      if (!detail::token_is_empty_or(token, "create_account_failed")) {
        return detail::widen_token(token);
      }
      switch (code) {
        case 0:
          return L"\"" + detail::widen_token(subject) +
                 L"\"这个帐号正在被其他的玩家使用了。\\请使用一个不同的名字。";
        case -2:
          return L"在本服务器下, 这个新名字禁止使用。\\请联系我们。";
        default:
          return L"建立ID失败，请确认它没有包含空格,\\特殊字符，或难以辨认的字符。";
      }

    case AuthErrorContext::update_account:
      if (token.empty() || token == "update_account_failed" || token == "account_not_found") {
        return L"更新帐户失败";
      }
      return detail::widen_token(token);

    case AuthErrorContext::change_password:
      if (!detail::token_is_empty_or(token, "change_password_failed")) {
        return detail::widen_token(token);
      }
      switch (code) {
        case -1:
          return L"密码错误。不能进行密码变更。";
        case -2:
          return L"帐户被锁定 .请稍后再试。";
        default:
          return L"密码少于4位,你不能改变它。";
      }

    case AuthErrorContext::select_server:
      if (detail::token_is_empty_or(token, "server_not_found")) {
        return L"服务器不可用。请重新选择服务器。";
      }
      return detail::widen_token(token);

    case AuthErrorContext::query_characters:
      if (token.empty() || token == "not_authenticated") {
        return L"这个帐号不可用,服务器认正失败。";
      }
      return detail::widen_token(token);

    case AuthErrorContext::create_character:
      if (!(token.empty() || token == "invalid_character_name" ||
            token == "create_character_failed" || token == "character_slots_full")) {
        return detail::widen_token(token);
      }
      switch (code) {
        case 2:
          return L"[failure] 这个名字已经存在。";
        case 3:
          return L"[Failure] 你只能为一个帐户设两个角色.\\请和游戏管理员联系。";
        case 4:
          return L"[failure] 角色建立失败. Error=4";
        default:
          return L"[Failure] 未知的错误. 请写信给: support@legendofmir.net";
      }

    case AuthErrorContext::delete_character:
      if (detail::token_is_empty_or(token, "delete_character_failed")) {
        return L"[Failure] 删除角色失败, Email: support@legendofmir.com.cn";
      }
      return detail::widen_token(token);

    case AuthErrorContext::select_character:
      if (token.empty() || token == "character_not_found") {
        return L"您选择的服务器用户满员。";
      }
      return detail::widen_token(token);

    case AuthErrorContext::enter_world:
      if (token.empty() || token == "character_not_found" ||
          token == "invalid_enter_world_token") {
        return L"您选择的服务器用户满员。";
      }
      return detail::widen_token(token);

    case AuthErrorContext::disconnect:
      if (token == "protocol_version_mismatch" || token == "missing_client_hello") {
        return L"版本错误。请下载最新版本。 (http://www.legendofmir.com.cn)";
      }
      if (token == "not_authenticated") {
        return L"这个帐号不可用,服务器认正失败。";
      }
      if (token == "character_not_found" || token == "invalid_enter_world_token") {
        return L"您选择的服务器用户满员。";
      }
      return detail::widen_token(token);
  }
  return detail::widen_token(token);
}

}  // namespace mir2::client
