#include "scene/legacy_auth_ui.hpp"

namespace mir2::client::legacy_auth_ui {

std::string_view legacy_auth_ui_trace_label(const LegacyAuthUiTraceLabel label) {
  switch (label) {
    case LegacyAuthUiTraceLabel::account_enter_focus_password:
      return "account_enter_focus_password";
    case LegacyAuthUiTraceLabel::password_enter_send_login:
      return "password_enter_send_login";
    case LegacyAuthUiTraceLabel::send_login:
      return "send_login";
    case LegacyAuthUiTraceLabel::recv_login_failure:
      return "recv_login_failure";
    case LegacyAuthUiTraceLabel::show_login_error_modal:
      return "show_login_error_modal";
    case LegacyAuthUiTraceLabel::modal_ok:
      return "modal_ok";
    case LegacyAuthUiTraceLabel::focus_login_password:
      return "focus_login_password";
    case LegacyAuthUiTraceLabel::recv_login_success:
      return "recv_login_success";
    case LegacyAuthUiTraceLabel::recv_server_list:
      return "recv_server_list";
    case LegacyAuthUiTraceLabel::show_server_select:
      return "show_server_select";
    case LegacyAuthUiTraceLabel::click_server_select:
      return "click_server_select";
    case LegacyAuthUiTraceLabel::send_select_server:
      return "send_select_server";
    case LegacyAuthUiTraceLabel::recv_select_server_ok:
      return "recv_select_server_ok";
    case LegacyAuthUiTraceLabel::connect_character_gateway:
      return "connect_character_gateway";
    case LegacyAuthUiTraceLabel::send_query_character:
      return "send_query_character";
    case LegacyAuthUiTraceLabel::recv_query_character:
      return "recv_query_character";
    case LegacyAuthUiTraceLabel::show_character_select:
      return "show_character_select";
    case LegacyAuthUiTraceLabel::select_character_slot:
      return "select_character_slot";
    case LegacyAuthUiTraceLabel::click_start_character:
      return "click_start_character";
    case LegacyAuthUiTraceLabel::send_select_character:
      return "send_select_character";
    case LegacyAuthUiTraceLabel::recv_start_play:
      return "recv_start_play";
    case LegacyAuthUiTraceLabel::connect_game_gateway:
      return "connect_game_gateway";
    case LegacyAuthUiTraceLabel::show_login_notice_or_loading:
      return "show_login_notice_or_loading";
    case LegacyAuthUiTraceLabel::login_notice_ok:
      return "login_notice_ok";
    case LegacyAuthUiTraceLabel::waiting_world_snapshot:
      return "waiting_world_snapshot";
    case LegacyAuthUiTraceLabel::open_create_character_dialog:
      return "open_create_character_dialog";
    case LegacyAuthUiTraceLabel::focus_create_character_name:
      return "focus_create_character_name";
    case LegacyAuthUiTraceLabel::send_new_character:
      return "send_new_character";
    case LegacyAuthUiTraceLabel::recv_new_character_success:
      return "recv_new_character_success";
    case LegacyAuthUiTraceLabel::refresh_character_slots:
      return "refresh_character_slots";
    case LegacyAuthUiTraceLabel::click_delete_character:
      return "click_delete_character";
    case LegacyAuthUiTraceLabel::confirm_delete_character:
      return "confirm_delete_character";
    case LegacyAuthUiTraceLabel::send_delete_character:
      return "send_delete_character";
    case LegacyAuthUiTraceLabel::recv_delete_character_success:
      return "recv_delete_character_success";
  }
  return "";
}

RectI LegacyServerSelectLayout::server_button(const std::size_t index) const {
  if (index < 8) {
    const auto x = dialog_uses_prguse2 ? 25 : 63;
    return RectI{dialog.x + x, dialog.y + row_top + static_cast<int>(index) * row_gap, 180, 34};
  }
  if (index < 16) {
    return RectI{dialog.x + 195, dialog.y + row_top + static_cast<int>(index - 8) * row_gap,
                 180, 34};
  }
  return RectI{dialog.x + 365, dialog.y + row_top + static_cast<int>(index - 16) * row_gap,
               180, 34};
}

RectI legacy_centered_rect(const RectI sprite_template) {
  return RectI{(800 - sprite_template.w) / 2, (600 - sprite_template.h) / 2, sprite_template.w,
               sprite_template.h};
}

LegacyLoginLayout legacy_login_layout(const RectI dialog_template) {
  LegacyLoginLayout layout;
  layout.dialog = legacy_centered_rect(dialog_template);
  layout.account_edit = RectI{layout.dialog.x + 130, layout.dialog.y + 99, 137, 16};
  layout.password_edit = RectI{layout.dialog.x + 130, layout.dialog.y + 131, 137, 16};
  layout.change_password_button = RectI{layout.dialog.x + 111, layout.dialog.y + 207, 88, 28};
  layout.create_account_button = RectI{layout.dialog.x + 24, layout.dialog.y + 207, 88, 28};
  layout.login_button = RectI{layout.dialog.x + 171, layout.dialog.y + 165, 88, 28};
  layout.close_button = RectI{layout.dialog.x + 252, layout.dialog.y + 28, 88, 28};
  return layout;
}

LegacyServerSelectLayout legacy_server_select_layout(const RectI dialog_template,
                                                     const std::size_t visible_count) {
  LegacyServerSelectLayout layout;
  const auto count = visible_count > 24 ? 24 : visible_count;
  layout.row_gap = 42;
  layout.dialog_sprite_index = count <= 8 ? 256 : (count <= 16 ? 4 : 5);
  layout.dialog_uses_prguse2 = count > 8;
  layout.dialog = legacy_centered_rect(dialog_template);
  if (count <= 8) {
    layout.close_button = RectI{layout.dialog.x + 244, layout.dialog.y + 30, 24, 24};
    layout.row_top = 235 - static_cast<int>(layout.row_gap * count) / 2;
  } else if (count <= 16) {
    layout.close_button = RectI{layout.dialog.x + 348, layout.dialog.y + 31, 24, 24};
    layout.row_top = 235 - (static_cast<int>(layout.row_gap) * 16 / 2) / 2;
  } else {
    layout.close_button = RectI{layout.dialog.x + 527, layout.dialog.y + 35, 24, 24};
    layout.row_top = 235 - static_cast<int>(layout.row_gap) * 8 / 2;
  }
  return layout;
}

LegacyMessageModalLayout legacy_message_modal_layout(const RectI dialog_template) {
  LegacyMessageModalLayout layout;
  layout.dialog = legacy_centered_rect(dialog_template);
  layout.title_origin = RectI{layout.dialog.x + 39, layout.dialog.y + 20, 0, 0};
  layout.text_origin = RectI{layout.dialog.x + 39, layout.dialog.y + 38, 0, 0};
  layout.ok_button = RectI{layout.dialog.x + (layout.dialog.w - 88) / 2, layout.dialog.y + 126,
                           88, 28};
  layout.yes_button = RectI{layout.dialog.x + 104, layout.dialog.y + 126, 88, 28};
  layout.no_button = RectI{layout.dialog.x + 136, layout.dialog.y + 126, 88, 28};
  layout.cancel_button = RectI{layout.dialog.x + 210, layout.dialog.y + 126, 88, 28};
  return layout;
}

LegacyCharacterSelectLayout legacy_character_select_layout() {
  LegacyCharacterSelectLayout layout;
  layout.left_button = RectI{133, 453, 88, 28};
  layout.right_button = RectI{685, 454, 88, 28};
  layout.start_button = RectI{385, 456, 88, 28};
  layout.new_button = RectI{348, 486, 88, 28};
  layout.erase_button = RectI{347, 506, 88, 28};
  layout.left_name_text = RectI{117, 494, 0, 0};
  layout.left_level_text = RectI{117, 523, 0, 0};
  layout.left_job_text = RectI{117, 553, 0, 0};
  layout.right_name_text = RectI{671, 496, 0, 0};
  layout.right_level_text = RectI{671, 525, 0, 0};
  layout.right_job_text = RectI{671, 555, 0, 0};
  layout.server_name_text = RectI{400, 8, 0, 0};
  return layout;
}

LegacyCreateCharacterLayout legacy_create_character_layout(
    const RectI dialog_template, const RectI job_button_template,
    const RectI sex_button_template, const RectI prev_hair_button_template,
    const RectI next_hair_button_template, const RectI ok_button_template,
    const RectI close_button_template) {
  LegacyCreateCharacterLayout layout;
  layout.dialog = legacy_centered_rect(dialog_template);
  layout.name_edit = RectI{layout.dialog.x + 71, layout.dialog.y + 107, 137, 20};
  layout.warrior_button = RectI{layout.dialog.x + 48, layout.dialog.y + 157,
                                job_button_template.w, job_button_template.h};
  layout.wizard_button = RectI{layout.dialog.x + 93, layout.dialog.y + 157,
                               job_button_template.w, job_button_template.h};
  layout.taoist_button = RectI{layout.dialog.x + 138, layout.dialog.y + 157,
                               job_button_template.w, job_button_template.h};
  layout.male_button = RectI{layout.dialog.x + 93, layout.dialog.y + 231,
                             sex_button_template.w, sex_button_template.h};
  layout.female_button = RectI{layout.dialog.x + 138, layout.dialog.y + 231,
                               sex_button_template.w, sex_button_template.h};
  layout.prev_hair_button = RectI{layout.dialog.x + 76, layout.dialog.y + 308,
                                  prev_hair_button_template.w, prev_hair_button_template.h};
  layout.next_hair_button = RectI{layout.dialog.x + 170, layout.dialog.y + 308,
                                  next_hair_button_template.w, next_hair_button_template.h};
  layout.ok_button = RectI{layout.dialog.x + 102, layout.dialog.y + 359,
                           ok_button_template.w, ok_button_template.h};
  layout.close_button = RectI{layout.dialog.x + 248, layout.dialog.y + 31,
                              close_button_template.w, close_button_template.h};
  return layout;
}

}  // namespace mir2::client::legacy_auth_ui
