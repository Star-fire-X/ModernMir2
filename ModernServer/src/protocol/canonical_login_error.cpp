#include "protocol/canonical_login_error.hpp"

#include "protocol/legacy_types.hpp"

namespace mir2 {

namespace {

constexpr CanonicalLegacyLoginErrorResponse no_legacy() { return {}; }

constexpr CanonicalLegacyLoginErrorResponse legacy(std::uint16_t ident, std::int32_t recog,
                                                   std::uint16_t param = 0,
                                                   std::uint16_t tag = 0,
                                                   std::uint16_t series = 0) {
  return CanonicalLegacyLoginErrorResponse{true, ident, recog, param, tag, series};
}

constexpr CanonicalClientV1LoginErrorResponse result(std::int32_t code,
                                                     std::string_view text) {
  return CanonicalClientV1LoginErrorResponse{false, code, text};
}

constexpr CanonicalClientV1LoginErrorResponse disconnect(std::int32_t code,
                                                         std::string_view text) {
  return CanonicalClientV1LoginErrorResponse{true, code, text};
}

}  // namespace

CanonicalLoginErrorMapping canonical_login_error_mapping(CanonicalLoginErrorKind kind) {
  switch (kind) {
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
    case CanonicalLoginErrorKind::create_account_failed:
      return {kind, legacy(kSmNewIdFail, 0), result(0, "create_account_failed")};
    case CanonicalLoginErrorKind::update_account_failed:
      return {kind, legacy(kSmUpdateIdFail, -1), result(-1, "update_account_failed")};
    case CanonicalLoginErrorKind::update_account_missing:
      return {kind, legacy(kSmUpdateIdFail, -1), result(-4, "account_not_found")};
    case CanonicalLoginErrorKind::change_password_failed:
      return {kind, legacy(kSmChgPasswdFail, 0), result(0, "change_password_failed")};
    case CanonicalLoginErrorKind::change_password_bad_password:
      return {kind, legacy(kSmChgPasswdFail, -1), result(-1, "change_password_failed")};
    case CanonicalLoginErrorKind::change_password_locked:
      return {kind, legacy(kSmChgPasswdFail, -2), result(-2, "change_password_failed")};
    case CanonicalLoginErrorKind::select_server_rejected:
      return {kind, legacy(kSmPasswdFail, -4), disconnect(401, "not_authenticated")};
    case CanonicalLoginErrorKind::server_not_found:
      return {kind, no_legacy(), result(0, "server_not_found")};
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
    case CanonicalLoginErrorKind::select_character_rejected:
      return {kind, legacy(kSmStartFail, 0), disconnect(401, "not_authenticated")};
    case CanonicalLoginErrorKind::character_not_found:
      return {kind, legacy(kSmStartFail, 0), disconnect(404, "character_not_found")};
    case CanonicalLoginErrorKind::invalid_enter_world_token:
      return {kind, no_legacy(), disconnect(401, "invalid_enter_world_token")};
    case CanonicalLoginErrorKind::already_entered_world:
      return {kind, no_legacy(), disconnect(409, "already_entered_world")};
  }
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
