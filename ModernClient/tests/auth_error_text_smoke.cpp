#include "app/auth_error_text.hpp"

#include <cassert>
#include <string>

namespace {

void assert_message(const mir2::client::AuthErrorContext context, const int code,
                    const std::string& token, const std::wstring& expected,
                    const std::string& subject = {}) {
  const auto actual = mir2::client::legacy_auth_error_message(context, code, token, subject);
  assert(actual == expected);
}

}  // namespace

int main() {
  using mir2::client::AuthErrorContext;
  using mir2::client::legacy_auth_error_message;

  assert_message(AuthErrorContext::login, -1, "login_failed", L"密码错误.");
  assert_message(AuthErrorContext::login, -2, "login_failed",
                 L"连续三次密码错误。\\你将在一段时间内无法再次连接。.");
  assert_message(AuthErrorContext::login, -3, "login_failed",
                 L"这个帐号正在使用，或者是被异常的终止锁定了,\\请稍后再试。");
  assert_message(AuthErrorContext::login, -4, "login_failed",
                 L"这个帐户不能正确访问。\\请改变帐户,\\或者申请付费注册。");
  assert_message(AuthErrorContext::login, -5, "login_failed", L"这个帐户被禁止了.");
  assert_message(AuthErrorContext::login, 0, "login_failed",
                 L"这个帐户不能正确访问。\\请改变帐户,\\或者申请付费注册。");
  assert_message(AuthErrorContext::login, 99, "login_failed", L"ID不存在或未知错误.");

  assert_message(AuthErrorContext::create_account, 0, "create_account_failed",
                 L"\"guest\"这个帐号正在被其他的玩家使用了。\\请使用一个不同的名字。",
                 "guest");
  assert_message(AuthErrorContext::create_account, -2, "create_account_failed",
                 L"在本服务器下, 这个新名字禁止使用。\\请联系我们。");
  assert_message(AuthErrorContext::create_account, 7, "create_account_failed",
                 L"建立ID失败，请确认它没有包含空格,\\特殊字符，或难以辨认的字符。");

  assert_message(AuthErrorContext::update_account, -1, "update_account_failed",
                 L"更新帐户失败");
  assert_message(AuthErrorContext::change_password, -1, "change_password_failed",
                 L"密码错误。不能进行密码变更。");
  assert_message(AuthErrorContext::change_password, -2, "change_password_failed",
                 L"帐户被锁定 .请稍后再试。");
  assert_message(AuthErrorContext::change_password, 0, "change_password_failed",
                 L"密码少于4位,你不能改变它。");

  assert_message(AuthErrorContext::select_server, 0, "server_not_found",
                 L"服务器不可用。请重新选择服务器。");
  assert_message(AuthErrorContext::query_characters, 401, "not_authenticated",
                 L"这个帐号不可用,服务器认正失败。");
  assert_message(AuthErrorContext::create_character, 2, "create_character_failed",
                 L"[failure] 这个名字已经存在。");
  assert_message(AuthErrorContext::create_character, 3, "character_slots_full",
                 L"[Failure] 你只能为一个帐户设两个角色.\\请和游戏管理员联系。");
  assert_message(AuthErrorContext::create_character, 4, "create_character_failed",
                 L"[failure] 角色建立失败. Error=4");
  assert_message(AuthErrorContext::create_character, 0, "invalid_character_name",
                 L"[Failure] 未知的错误. 请写信给: support@legendofmir.net");
  assert_message(AuthErrorContext::delete_character, 0, "delete_character_failed",
                 L"[Failure] 删除角色失败, Email: support@legendofmir.com.cn");
  assert_message(AuthErrorContext::select_character, 0, "character_not_found",
                 L"您选择的服务器用户满员。");
  assert_message(AuthErrorContext::enter_world, 0, "invalid_enter_world_token",
                 L"您选择的服务器用户满员。");
  assert_message(AuthErrorContext::disconnect, 426, "protocol_version_mismatch",
                 L"版本错误。请下载最新版本。 (http://www.legendofmir.com.cn)");
  assert_message(AuthErrorContext::disconnect, 401, "not_authenticated",
                 L"这个帐号不可用,服务器认正失败。");
  assert_message(AuthErrorContext::disconnect, 404, "character_not_found",
                 L"您选择的服务器用户满员。");

  const auto unknown =
      legacy_auth_error_message(AuthErrorContext::login, -1, "future_server_error");
  assert(unknown == L"future_server_error");

  const auto known_login =
      legacy_auth_error_message(AuthErrorContext::login, -1, "login_failed");
  const auto known_character =
      legacy_auth_error_message(AuthErrorContext::select_character, 0, "character_not_found");
  const auto known_version =
      legacy_auth_error_message(AuthErrorContext::disconnect, 426, "protocol_version_mismatch");
  assert(known_login != L"login_failed");
  assert(known_character != L"character_not_found");
  assert(known_version != L"protocol_version_mismatch");
  assert(known_login.find(L'\\') == std::wstring::npos);
  assert(legacy_auth_error_message(AuthErrorContext::login, -4, "login_failed")
             .find(L'\\') != std::wstring::npos);
  assert(legacy_auth_error_message(AuthErrorContext::create_character, 3,
                                   "character_slots_full")
             .find(L'\\') != std::wstring::npos);

  return 0;
}
