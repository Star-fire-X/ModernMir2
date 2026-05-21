#pragma once

#include <cstddef>
#include <string_view>

#include "render/software_renderer.hpp"

namespace mir2::client::legacy_auth_ui {

enum class LegacyAuthUiTraceLabel {
  account_enter_focus_password,
  password_enter_send_login,
  send_login,
  recv_login_failure,
  show_login_error_modal,
  modal_ok,
  focus_login_password,
  recv_login_success,
  recv_server_list,
  show_server_select,
  click_server_select,
  send_select_server,
  recv_select_server_ok,
  connect_character_gateway,
  send_query_character,
  recv_query_character,
  show_character_select,
  select_character_slot,
  click_start_character,
  send_select_character,
  recv_start_play,
  connect_game_gateway,
  show_login_notice_or_loading,
  login_notice_ok,
  waiting_world_snapshot,
  open_create_character_dialog,
  focus_create_character_name,
  send_new_character,
  recv_new_character_success,
  refresh_character_slots,
  click_delete_character,
  confirm_delete_character,
  send_delete_character,
  recv_delete_character_success
};

[[nodiscard]] std::string_view legacy_auth_ui_trace_label(LegacyAuthUiTraceLabel label);

struct LegacyLoginLayout {
  RectI dialog{};
  RectI account_edit{};
  RectI password_edit{};
  RectI change_password_button{};
  RectI create_account_button{};
  RectI login_button{};
  RectI close_button{};
};

struct LegacyServerSelectLayout {
  RectI dialog{};
  RectI close_button{};
  int row_top{0};
  int row_gap{42};
  int dialog_sprite_index{256};
  bool dialog_uses_prguse2{false};

  [[nodiscard]] RectI server_button(std::size_t index) const;
};

struct LegacyMessageModalLayout {
  RectI dialog{};
  RectI title_origin{};
  RectI text_origin{};
  RectI ok_button{};
  RectI yes_button{};
  RectI no_button{};
  RectI cancel_button{};
};

struct LegacyCharacterSelectLayout {
  RectI left_button{};
  RectI right_button{};
  RectI start_button{};
  RectI new_button{};
  RectI erase_button{};
  RectI left_name_text{};
  RectI left_level_text{};
  RectI left_job_text{};
  RectI right_name_text{};
  RectI right_level_text{};
  RectI right_job_text{};
  RectI server_name_text{};
};

struct LegacyCreateCharacterLayout {
  RectI dialog{};
  RectI name_edit{};
  RectI warrior_button{};
  RectI wizard_button{};
  RectI taoist_button{};
  RectI male_button{};
  RectI female_button{};
  RectI prev_hair_button{};
  RectI next_hair_button{};
  RectI ok_button{};
  RectI close_button{};
};

[[nodiscard]] RectI legacy_centered_rect(RectI sprite_template);
[[nodiscard]] LegacyLoginLayout legacy_login_layout(RectI dialog_template);
[[nodiscard]] LegacyServerSelectLayout legacy_server_select_layout(RectI dialog_template,
                                                                   std::size_t visible_count);
[[nodiscard]] LegacyMessageModalLayout legacy_message_modal_layout(RectI dialog_template);
[[nodiscard]] LegacyCharacterSelectLayout legacy_character_select_layout();
[[nodiscard]] LegacyCreateCharacterLayout legacy_create_character_layout(
    RectI dialog_template, RectI job_button_template, RectI sex_button_template,
    RectI prev_hair_button_template, RectI next_hair_button_template, RectI ok_button_template,
    RectI close_button_template);

}  // namespace mir2::client::legacy_auth_ui
