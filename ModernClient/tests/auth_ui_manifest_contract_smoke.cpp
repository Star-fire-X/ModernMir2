#include "delphi_ui_manifest_helpers.hpp"
#include "scene/legacy_auth_ui.hpp"

#include <cassert>
#include <filesystem>
#include <string>

namespace {

using mir2::client::RectI;
using mir2::client::tests::json_direct_rect_after;
using mir2::client::tests::json_int_after;
using mir2::client::tests::json_rect_after;
using mir2::client::tests::manifest_slice;
using mir2::client::tests::read_text_file;
using mir2::client::tests::same_rect;
namespace auth = mir2::client::legacy_auth_ui;

void assert_rect(const RectI actual, const RectI expected) {
  assert(same_rect(actual, expected));
}

void test_login_contract(const std::string& manifest) {
  const auto section = manifest_slice(manifest, "\"login\": {", 9000);
  const auto layout = auth::legacy_login_layout(RectI{0, 0, 360, 280});
  assert_rect(json_direct_rect_after(section, "\"dialog\": {"), layout.dialog);
  assert_rect(json_rect_after(section, "\"account_edit\""), layout.account_edit);
  assert_rect(json_rect_after(section, "\"password_edit\""), layout.password_edit);
  assert_rect(json_rect_after(section, "\"create_account_button\""), layout.create_account_button);
  assert_rect(json_rect_after(section, "\"change_password_button\""), layout.change_password_button);
  assert_rect(json_rect_after(section, "\"login_button\""), layout.login_button);
  assert_rect(json_rect_after(section, "\"close_button\""), layout.close_button);
  assert(json_int_after(manifest_slice(section, "\"dialog\": {"), "image_index") == 60);
}

void test_server_select_contract(const std::string& manifest) {
  const auto section = manifest_slice(manifest, "\"server_select\": {", 65000);
  const auto check = [&section](const int count, const RectI dialog_template,
                                const int first_button, const int last_button) {
    const auto count_section = manifest_slice(section, "\"" + std::to_string(count) + "\": {",
                                              count == 24 ? 24000 : 18000);
    const auto layout = auth::legacy_server_select_layout(dialog_template, count);
    assert_rect(json_direct_rect_after(count_section, "\"dialog\": {"), layout.dialog);
    assert_rect(json_direct_rect_after(count_section, "\"close_button\": {"), layout.close_button);
    assert_rect(json_rect_after(count_section, "\"name\": \"DSServer" +
                                                   std::to_string(first_button) + "\""),
                layout.server_button(static_cast<std::size_t>(first_button - 1)));
    assert_rect(json_rect_after(count_section, "\"name\": \"DSServer" +
                                                   std::to_string(last_button) + "\""),
                layout.server_button(static_cast<std::size_t>(last_button - 1)));
    assert(json_int_after(manifest_slice(count_section, "\"dialog_sprite\""), "index") ==
           layout.dialog_sprite_index);
  };

  check(1, RectI{0, 0, 300, 360}, 1, 1);
  check(8, RectI{0, 0, 300, 360}, 1, 8);
  check(16, RectI{0, 0, 404, 360}, 1, 16);
  check(24, RectI{0, 0, 584, 360}, 1, 24);
}

void test_message_modal_contract(const std::string& manifest) {
  const auto section = manifest_slice(manifest, "\"message_modal\": {", 11000);
  const auto size1 = manifest_slice(section, "\"dialog_size_1\": {", 5000);
  const auto layout = auth::legacy_message_modal_layout(RectI{0, 0, 360, 180});
  assert_rect(json_direct_rect_after(size1, "\"dialog\": {"), layout.dialog);
  assert_rect(json_direct_rect_after(size1, "\"ok_button\": {"), layout.ok_button);
  assert_rect(json_direct_rect_after(size1, "\"yes_button\": {"), layout.yes_button);
  assert_rect(json_direct_rect_after(size1, "\"no_button\": {"), layout.no_button);
  assert_rect(json_direct_rect_after(size1, "\"cancel_button\": {"), layout.cancel_button);
}

void test_character_select_contract(const std::string& manifest) {
  const auto layouts = manifest_slice(manifest, "\"layouts\": {", 120000);
  const auto section = manifest_slice(layouts, "\"character_select\": {", 16000);
  const auto layout = auth::legacy_character_select_layout();
  assert_rect(json_direct_rect_after(section, "\"dialog\": {"), RectI{0, 0, 800, 600});
  assert_rect(json_rect_after(section, "\"left_button\""), layout.left_button);
  assert_rect(json_rect_after(section, "\"right_button\""), layout.right_button);
  assert_rect(json_rect_after(section, "\"start_button\""), layout.start_button);
  assert_rect(json_rect_after(section, "\"new_button\""), layout.new_button);
  assert_rect(json_rect_after(section, "\"erase_button\""), layout.erase_button);
  assert_rect(json_direct_rect_after(section, "\"left_name_text\": {"), layout.left_name_text);
  assert_rect(json_direct_rect_after(section, "\"right_job_text\": {"), layout.right_job_text);
  assert_rect(json_direct_rect_after(section, "\"server_name_text\": {"), layout.server_name_text);
}

}  // namespace

int main() {
  const auto source_dir = std::filesystem::path{MIR2_CLIENT_SOURCE_DIR};
  const auto manifest =
      read_text_file(source_dir / "tests" / "golden" / "delphi_ui" / "auth_ui_manifest.json");
  test_login_contract(manifest);
  test_server_select_contract(manifest);
  test_message_modal_contract(manifest);
  test_character_select_contract(manifest);
  return 0;
}
