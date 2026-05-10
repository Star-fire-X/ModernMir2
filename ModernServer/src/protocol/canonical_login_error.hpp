#pragma once

#include <cstdint>
#include <string_view>

namespace mir2 {

enum class CanonicalLoginErrorKind {
  unsupported_login_message,
  unsupported_game_message,
  protocol_version_mismatch,
  missing_client_hello,
  not_authenticated,
  account_mismatch,
  login_empty_credentials,
  login_account_missing,
  login_bad_password,
  login_locked,
  login_banned,
  login_duplicate,
  create_account_failed,
  update_account_failed,
  update_account_missing,
  change_password_failed,
  change_password_bad_password,
  change_password_locked,
  select_server_rejected,
  server_not_found,
  query_characters_rejected,
  create_character_rejected,
  invalid_character_name,
  character_slots_full,
  create_character_duplicate,
  create_character_failed,
  delete_character_rejected,
  delete_character_failed,
  select_character_rejected,
  character_not_found,
  invalid_enter_world_token,
  already_entered_world
};

struct CanonicalLegacyLoginErrorResponse {
  bool present{false};
  std::uint16_t ident{0};
  std::int32_t recog{0};
  std::uint16_t param{0};
  std::uint16_t tag{0};
  std::uint16_t series{0};
};

struct CanonicalClientV1LoginErrorResponse {
  bool disconnect{false};
  std::int32_t code{0};
  std::string_view text{};
};

struct CanonicalLoginErrorMapping {
  CanonicalLoginErrorKind kind{CanonicalLoginErrorKind::not_authenticated};
  CanonicalLegacyLoginErrorResponse legacy{};
  CanonicalClientV1LoginErrorResponse client_v1{};
};

[[nodiscard]] CanonicalLoginErrorMapping canonical_login_error_mapping(
    CanonicalLoginErrorKind kind);
[[nodiscard]] CanonicalLoginErrorKind canonical_login_error_from_login_result_code(
    std::int32_t result_code);
[[nodiscard]] CanonicalLoginErrorKind canonical_login_error_from_change_password_result_code(
    std::int32_t result_code);
[[nodiscard]] std::string_view canonical_login_error_name(CanonicalLoginErrorKind kind);

}  // namespace mir2
