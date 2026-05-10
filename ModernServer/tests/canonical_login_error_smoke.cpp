#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

#include "protocol/canonical_login_error.hpp"
#include "protocol/legacy_types.hpp"

namespace {

struct ExpectedError {
  mir2::CanonicalLoginErrorKind kind;
  bool has_legacy;
  std::uint16_t legacy_ident;
  std::int32_t legacy_recog;
  std::uint16_t legacy_param;
  std::uint16_t legacy_tag;
  std::uint16_t legacy_series;
  bool client_disconnect;
  std::int32_t client_code;
  std::string_view client_text;
};

int fail(const char* stage, mir2::CanonicalLoginErrorKind kind) {
  std::cerr << "canonical_login_error_smoke failed at " << stage << " for "
            << mir2::canonical_login_error_name(kind) << '\n';
  return 1;
}

}  // namespace

int main() {
  const std::vector<ExpectedError> cases{
      {mir2::CanonicalLoginErrorKind::unsupported_login_message, false, 0, 0, 0, 0, 0,
       true, 400, "unsupported_login_message"},
      {mir2::CanonicalLoginErrorKind::unsupported_game_message, false, 0, 0, 0, 0, 0,
       true, 400, "unsupported_game_message"},
      {mir2::CanonicalLoginErrorKind::protocol_version_mismatch, false, 0, 0, 0, 0, 0,
       true, 426, "protocol_version_mismatch"},
      {mir2::CanonicalLoginErrorKind::missing_client_hello, false, 0, 0, 0, 0, 0,
       true, 400, "missing_client_hello"},
      {mir2::CanonicalLoginErrorKind::not_authenticated, false, 0, 0, 0, 0, 0,
       true, 401, "not_authenticated"},
      {mir2::CanonicalLoginErrorKind::account_mismatch, false, 0, 0, 0, 0, 0,
       true, 403, "account_mismatch"},
      {mir2::CanonicalLoginErrorKind::login_empty_credentials, true, mir2::kSmPasswdFail,
       -4, 0, 0, 0, false, -4, "login_failed"},
      {mir2::CanonicalLoginErrorKind::login_account_missing, true, mir2::kSmPasswdFail,
       -4, 0, 0, 0, false, -4, "login_failed"},
      {mir2::CanonicalLoginErrorKind::login_bad_password, true, mir2::kSmPasswdFail,
       -1, 0, 0, 0, false, -1, "login_failed"},
      {mir2::CanonicalLoginErrorKind::login_locked, true, mir2::kSmPasswdFail, -2,
       0, 0, 0, false, -2, "login_failed"},
      {mir2::CanonicalLoginErrorKind::login_banned, true, mir2::kSmPasswdFail, -5,
       0, 0, 0, false, -5, "login_failed"},
      {mir2::CanonicalLoginErrorKind::login_duplicate, true, mir2::kSmPasswdFail, -3,
       0, 0, 0, false, -3, "login_failed"},
      {mir2::CanonicalLoginErrorKind::create_account_failed, true, mir2::kSmNewIdFail,
       0, 0, 0, 0, false, 0, "create_account_failed"},
      {mir2::CanonicalLoginErrorKind::update_account_failed, true, mir2::kSmUpdateIdFail,
       -1, 0, 0, 0, false, -1, "update_account_failed"},
      {mir2::CanonicalLoginErrorKind::update_account_missing, true, mir2::kSmUpdateIdFail,
       -1, 0, 0, 0, false, -4, "account_not_found"},
      {mir2::CanonicalLoginErrorKind::change_password_failed, true,
       mir2::kSmChgPasswdFail, 0, 0, 0, 0, false, 0, "change_password_failed"},
      {mir2::CanonicalLoginErrorKind::change_password_bad_password, true,
       mir2::kSmChgPasswdFail, -1, 0, 0, 0, false, -1, "change_password_failed"},
      {mir2::CanonicalLoginErrorKind::change_password_locked, true,
       mir2::kSmChgPasswdFail, -2, 0, 0, 0, false, -2, "change_password_failed"},
      {mir2::CanonicalLoginErrorKind::select_server_rejected, true,
       mir2::kSmPasswdFail, -4, 0, 0, 0, true, 401, "not_authenticated"},
      {mir2::CanonicalLoginErrorKind::server_not_found, false, 0, 0, 0, 0, 0,
       false, 0, "server_not_found"},
      {mir2::CanonicalLoginErrorKind::query_characters_rejected, true,
       mir2::kSmQueryChrFail, 0, 0, 0, 1, true, 401, "not_authenticated"},
      {mir2::CanonicalLoginErrorKind::create_character_rejected, true,
       mir2::kSmNewChrFail, 0, 0, 0, 0, true, 401, "not_authenticated"},
      {mir2::CanonicalLoginErrorKind::invalid_character_name, true,
       mir2::kSmNewChrFail, 0, 0, 0, 0, false, 0, "invalid_character_name"},
      {mir2::CanonicalLoginErrorKind::character_slots_full, true,
       mir2::kSmNewChrFail, 3, 0, 0, 0, false, 3, "character_slots_full"},
      {mir2::CanonicalLoginErrorKind::create_character_duplicate, true,
       mir2::kSmNewChrFail, 2, 0, 0, 0, false, 2, "create_character_failed"},
      {mir2::CanonicalLoginErrorKind::create_character_failed, true,
       mir2::kSmNewChrFail, 4, 0, 0, 0, false, 4, "create_character_failed"},
      {mir2::CanonicalLoginErrorKind::delete_character_rejected, true,
       mir2::kSmDelChrFail, 0, 0, 0, 0, true, 401, "not_authenticated"},
      {mir2::CanonicalLoginErrorKind::delete_character_failed, true,
       mir2::kSmDelChrFail, 0, 0, 0, 0, false, 0, "delete_character_failed"},
      {mir2::CanonicalLoginErrorKind::select_character_rejected, true,
       mir2::kSmStartFail, 0, 0, 0, 0, true, 401, "not_authenticated"},
      {mir2::CanonicalLoginErrorKind::character_not_found, true,
       mir2::kSmStartFail, 0, 0, 0, 0, true, 404, "character_not_found"},
      {mir2::CanonicalLoginErrorKind::invalid_enter_world_token, false, 0, 0, 0, 0, 0,
       true, 401, "invalid_enter_world_token"},
      {mir2::CanonicalLoginErrorKind::already_entered_world, false, 0, 0, 0, 0, 0,
       true, 409, "already_entered_world"},
  };

  for (const auto& expected : cases) {
    const auto mapping = mir2::canonical_login_error_mapping(expected.kind);
    if (mapping.kind != expected.kind) {
      return fail("kind", expected.kind);
    }
    if (mapping.legacy.present != expected.has_legacy ||
        mapping.legacy.ident != expected.legacy_ident ||
        mapping.legacy.recog != expected.legacy_recog ||
        mapping.legacy.param != expected.legacy_param ||
        mapping.legacy.tag != expected.legacy_tag ||
        mapping.legacy.series != expected.legacy_series) {
      return fail("legacy mapping", expected.kind);
    }
    if (mapping.client_v1.disconnect != expected.client_disconnect ||
        mapping.client_v1.code != expected.client_code ||
        mapping.client_v1.text != expected.client_text) {
      return fail("client_v1 mapping", expected.kind);
    }
    if (mir2::canonical_login_error_name(expected.kind).empty()) {
      return fail("name", expected.kind);
    }
  }

  if (mir2::canonical_login_error_from_login_result_code(-1) !=
          mir2::CanonicalLoginErrorKind::login_bad_password ||
      mir2::canonical_login_error_from_login_result_code(-2) !=
          mir2::CanonicalLoginErrorKind::login_locked ||
      mir2::canonical_login_error_from_login_result_code(-3) !=
          mir2::CanonicalLoginErrorKind::login_duplicate ||
      mir2::canonical_login_error_from_login_result_code(-5) !=
          mir2::CanonicalLoginErrorKind::login_banned ||
      mir2::canonical_login_error_from_login_result_code(0) !=
          mir2::CanonicalLoginErrorKind::login_account_missing) {
    return fail("login code helper", mir2::CanonicalLoginErrorKind::login_account_missing);
  }

  if (mir2::canonical_login_error_from_change_password_result_code(-1) !=
          mir2::CanonicalLoginErrorKind::change_password_bad_password ||
      mir2::canonical_login_error_from_change_password_result_code(-2) !=
          mir2::CanonicalLoginErrorKind::change_password_locked ||
      mir2::canonical_login_error_from_change_password_result_code(0) !=
          mir2::CanonicalLoginErrorKind::change_password_failed) {
    return fail("change password code helper",
                mir2::CanonicalLoginErrorKind::change_password_failed);
  }

  return 0;
}
