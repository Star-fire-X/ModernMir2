#include "delphi_ui_manifest_helpers.hpp"

#include <cassert>
#include <filesystem>
#include <string>

namespace {

using mir2::client::tests::json_bool_after;
using mir2::client::tests::json_int_after;
using mir2::client::tests::json_string_after;
using mir2::client::tests::manifest_object_slice;
using mir2::client::tests::manifest_slice;
using mir2::client::tests::read_text_file;

void assert_control(const std::string& manifest, const std::string& name,
                    const std::string& class_name, const std::string& parent) {
  const auto slice = manifest_object_slice(manifest, "\"name\": \"" + name + "\"", 2200);
  assert(json_string_after(slice, "class") == class_name);
  assert(json_string_after(slice, "parent") == parent);
}

void test_required_controls(const std::string& manifest) {
  assert_control(manifest, "DLogIn", "TDWindow", "DBackground");
  assert_control(manifest, "DLoginOk", "TDButton", "DLogIn");
  assert_control(manifest, "DMsgDlg", "TDWindow", "DBackground");
  assert_control(manifest, "DMsgDlgOk", "TDButton", "DMsgDlg");
  assert_control(manifest, "DSelServerDlg", "TDWindow", "DBackground");
  assert_control(manifest, "DSServer24", "TDButton", "DSelServerDlg");
  assert_control(manifest, "DSelectChr", "TDWindow", "DBackground");
  assert_control(manifest, "DscStart", "TDButton", "DSelectChr");
}

void test_dfm_and_runtime_fields(const std::string& manifest) {
  const auto login_ok = manifest_object_slice(manifest, "\"name\": \"DLoginOk\"", 2600);
  assert(json_int_after(login_ok, "left") == 107);
  assert(json_int_after(login_ok, "top") == 69);
  assert(json_string_after(login_ok, "click_handler") == "DLoginOkClick");
  assert(json_string_after(login_ok, "click_sound") == "csStone");
  assert(json_string_after(login_ok, "click_sound_handler") == "DLoginNewClickSound");
  assert(json_string_after(login_ok, "image_library") == "WProgUse");
  assert(json_int_after(login_ok, "image_index") == 62);
  assert(manifest_slice(login_ok, "\"left\"").find("\"value\": 171") != std::string::npos);
  assert(manifest_slice(login_ok, "\"top\"").find("\"value\": 165") != std::string::npos);

  const auto server_24 = manifest_object_slice(manifest, "\"name\": \"DSServer24\"", 2600);
  assert(json_string_after(server_24, "click_handler") == "DSServer1Click");
  assert(json_string_after(server_24, "image_library") == "WProgUse2");
  assert(json_int_after(server_24, "image_index") == 2);
  assert(!json_bool_after(server_24, "visible"));
}

void test_contract_sections(const std::string& manifest) {
  assert(manifest.find("\"layouts\"") != std::string::npos);
  assert(manifest.find("\"animation\"") != std::string::npos);
  assert(manifest.find("\"modal_focus_audio\"") != std::string::npos);
  assert(json_int_after(manifest_slice(manifest, "\"screen\""), "width") == 800);
  assert(json_int_after(manifest_slice(manifest, "\"screen\""), "height") == 600);
  assert(json_string_after(manifest_slice(manifest, "\"login_password_failure\""),
                           "login_password_failure") == "EdId");
}

}  // namespace

int main() {
  const auto source_dir = std::filesystem::path{MIR2_CLIENT_SOURCE_DIR};
  const auto manifest =
      read_text_file(source_dir / "tests" / "golden" / "delphi_ui" / "auth_ui_manifest.json");
  test_required_controls(manifest);
  test_dfm_and_runtime_fields(manifest);
  test_contract_sections(manifest);
  return 0;
}
