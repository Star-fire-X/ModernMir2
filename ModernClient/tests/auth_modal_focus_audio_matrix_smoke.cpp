#include "delphi_ui_manifest_helpers.hpp"

#include <cassert>
#include <filesystem>
#include <string>

namespace {

using mir2::client::tests::json_bool_after;
using mir2::client::tests::json_string_after;
using mir2::client::tests::manifest_slice;
using mir2::client::tests::read_text_file;

void test_modal_matrix(const std::string& manifest) {
  const auto section = manifest_slice(manifest, "\"modal\": {", 4500);
  const auto ok = manifest_slice(section, "\"mbOk\": {", 900);
  assert(ok.find("\"ok\"") != std::string::npos);
  assert(json_string_after(ok, "enter") == "mrOk");
  assert(json_string_after(ok, "escape") == "ignored");

  const auto yes = manifest_slice(section, "\"mbYes\": {", 900);
  assert(yes.find("\"yes\"") != std::string::npos);
  assert(json_string_after(yes, "enter") == "mrYes");
  assert(json_string_after(yes, "escape") == "ignored");

  const auto yes_no_cancel = manifest_slice(section, "\"mbYesNoCancel\": {", 1200);
  assert(yes_no_cancel.find("\"yes\"") != std::string::npos);
  assert(yes_no_cancel.find("\"no\"") != std::string::npos);
  assert(yes_no_cancel.find("\"cancel\"") != std::string::npos);
  assert(json_string_after(yes_no_cancel, "enter") == "ignored");
  assert(json_string_after(yes_no_cancel, "escape") == "mrCancel");

  const auto ok_abort = manifest_slice(section, "\"mbOkAbort\": {", 1200);
  assert(ok_abort.find("\"abort\"") != std::string::npos);
  assert(json_bool_after(ok_abort, "edit_visible"));
  assert(json_string_after(ok_abort, "enter") == "mrOk");
}

void test_focus_matrix(const std::string& manifest) {
  const auto section = manifest_slice(manifest, "\"focus\": {", 1200);
  assert(json_string_after(section, "login_account_enter") == "EdPasswd");
  assert(json_string_after(section, "login_password_enter_success") == "hide_login_edits");
  assert(json_string_after(section, "login_password_failure") == "EdId");
  assert(json_string_after(section, "login_notice_enter") == "CM_LOGINNOTICEOK");
  assert(json_string_after(section, "login_notice_escape") == "ignored");
  assert(json_string_after(section, "character_start_without_character") == "DMsgDlg");
}

void test_audio_matrix(const std::string& manifest) {
  const auto section = manifest_slice(manifest, "\"audio\": {", 1800);
  assert(json_string_after(section, "login_open") == "bmg_intro");
  assert(json_string_after(section, "login_close") == "SilenceSound");
  assert(json_string_after(section, "character_select_open") == "bmg_select");
  assert(json_string_after(section, "character_select_close") == "SilenceSound");
  assert(json_string_after(section, "login_door_open") == "s_rock_door_open");
  assert(json_string_after(section, "character_slot_select") == "s_meltstone");
  assert(json_string_after(section, "csNorm") == "s_norm_button_click");
  assert(json_string_after(section, "csStone") == "s_rock_button_click");
  assert(json_string_after(section, "csGlass") == "s_glass_button_click");
}

void test_recorded_delta(const std::string& manifest) {
  const auto section = manifest_slice(manifest, "\"known_cxx_deltas\": [", 1000);
  assert(json_string_after(section, "behavior") == "login_password_failure_focus");
  assert(json_string_after(section, "delphi") == "EdId");
  assert(json_string_after(section, "modern_client_pr3") == "password");
}

}  // namespace

int main() {
  const auto source_dir = std::filesystem::path{MIR2_CLIENT_SOURCE_DIR};
  const auto manifest =
      read_text_file(source_dir / "tests" / "golden" / "delphi_ui" / "auth_ui_manifest.json");
  test_modal_matrix(manifest);
  test_focus_matrix(manifest);
  test_audio_matrix(manifest);
  test_recorded_delta(manifest);
  return 0;
}
