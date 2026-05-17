#include "scene/legacy_auth_ui.hpp"

#include <cassert>

namespace {

using mir2::client::RectI;
namespace auth = mir2::client::legacy_auth_ui;

bool same(const RectI lhs, const RectI rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.w == rhs.w && lhs.h == rhs.h;
}

void test_login_layout() {
  const auto layout = auth::legacy_login_layout(RectI{0, 0, 360, 280});
  assert(same(layout.dialog, RectI{220, 160, 360, 280}));
  assert(same(layout.account_edit, RectI{350, 259, 137, 16}));
  assert(same(layout.password_edit, RectI{350, 291, 137, 16}));
  assert(same(layout.create_account_button, RectI{244, 367, 88, 28}));
  assert(same(layout.change_password_button, RectI{331, 367, 88, 28}));
  assert(same(layout.login_button, RectI{391, 325, 88, 28}));
  assert(same(layout.close_button, RectI{472, 188, 88, 28}));
}

void test_server_select_layout() {
  const auto layout = auth::legacy_server_select_layout(RectI{0, 0, 300, 360}, 2);
  assert(same(layout.dialog, RectI{250, 120, 300, 360}));
  assert(same(layout.close_button, RectI{494, 150, 24, 24}));
  assert(same(layout.server_button(0), RectI{313, 313, 180, 34}));
  assert(same(layout.server_button(1), RectI{313, 355, 180, 34}));
  assert(layout.dialog_sprite_index == 256);
  assert(!layout.dialog_uses_prguse2);
}

void test_server_select_layout_columns() {
  const auto one = auth::legacy_server_select_layout(RectI{0, 0, 300, 360}, 1);
  assert(same(one.server_button(0), RectI{313, 334, 180, 34}));

  const auto eight = auth::legacy_server_select_layout(RectI{0, 0, 300, 360}, 8);
  assert(same(eight.server_button(0), RectI{313, 187, 180, 34}));
  assert(same(eight.server_button(7), RectI{313, 481, 180, 34}));

  const auto sixteen = auth::legacy_server_select_layout(RectI{0, 0, 404, 360}, 16);
  assert(same(sixteen.dialog, RectI{198, 120, 404, 360}));
  assert(same(sixteen.close_button, RectI{546, 151, 24, 24}));
  assert(same(sixteen.server_button(0), RectI{223, 187, 180, 34}));
  assert(same(sixteen.server_button(8), RectI{393, 187, 180, 34}));
  assert(same(sixteen.server_button(15), RectI{393, 481, 180, 34}));
  assert(sixteen.dialog_sprite_index == 4);
  assert(sixteen.dialog_uses_prguse2);

  const auto twenty_four = auth::legacy_server_select_layout(RectI{0, 0, 584, 360}, 24);
  assert(same(twenty_four.dialog, RectI{108, 120, 584, 360}));
  assert(same(twenty_four.close_button, RectI{635, 155, 24, 24}));
  assert(same(twenty_four.server_button(0), RectI{133, 187, 180, 34}));
  assert(same(twenty_four.server_button(8), RectI{303, 187, 180, 34}));
  assert(same(twenty_four.server_button(16), RectI{473, 187, 180, 34}));
  assert(same(twenty_four.server_button(23), RectI{473, 481, 180, 34}));
  assert(twenty_four.dialog_sprite_index == 5);
  assert(twenty_four.dialog_uses_prguse2);
}

void test_message_modal_layout() {
  const auto layout = auth::legacy_message_modal_layout(RectI{0, 0, 360, 180});
  assert(same(layout.dialog, RectI{220, 210, 360, 180}));
  assert(same(layout.title_origin, RectI{259, 230, 0, 0}));
  assert(same(layout.text_origin, RectI{259, 248, 0, 0}));
  assert(same(layout.ok_button, RectI{356, 336, 88, 28}));
  assert(same(layout.yes_button, RectI{324, 336, 88, 28}));
  assert(same(layout.no_button, RectI{356, 336, 88, 28}));
  assert(same(layout.cancel_button, RectI{430, 336, 88, 28}));
}

void test_character_select_layout() {
  const auto layout = auth::legacy_character_select_layout();
  assert(same(layout.left_button, RectI{133, 453, 88, 28}));
  assert(same(layout.right_button, RectI{685, 454, 88, 28}));
  assert(same(layout.start_button, RectI{385, 456, 88, 28}));
  assert(same(layout.new_button, RectI{348, 486, 88, 28}));
  assert(same(layout.erase_button, RectI{347, 506, 88, 28}));
  assert(same(layout.left_name_text, RectI{117, 494, 0, 0}));
  assert(same(layout.right_job_text, RectI{671, 555, 0, 0}));
  assert(same(layout.server_name_text, RectI{400, 8, 0, 0}));
}

void test_create_character_layout_is_centered() {
  const auto layout =
      auth::legacy_create_character_layout(RectI{0, 0, 310, 400}, RectI{0, 0, 40, 24},
                                           RectI{0, 0, 40, 24}, RectI{0, 0, 40, 24},
                                           RectI{0, 0, 40, 24}, RectI{0, 0, 88, 28},
                                           RectI{0, 0, 16, 16});
  assert(same(layout.dialog, RectI{245, 100, 310, 400}));
  assert(same(layout.name_edit, RectI{316, 207, 137, 20}));
  assert(same(layout.warrior_button, RectI{293, 257, 40, 24}));
  assert(same(layout.wizard_button, RectI{338, 257, 40, 24}));
  assert(same(layout.taoist_button, RectI{383, 257, 40, 24}));
  assert(same(layout.male_button, RectI{338, 331, 40, 24}));
  assert(same(layout.female_button, RectI{383, 331, 40, 24}));
  assert(same(layout.prev_hair_button, RectI{321, 408, 40, 24}));
  assert(same(layout.next_hair_button, RectI{415, 408, 40, 24}));
  assert(same(layout.ok_button, RectI{347, 459, 88, 28}));
  assert(same(layout.close_button, RectI{493, 131, 16, 16}));
}

}  // namespace

int main() {
  test_login_layout();
  test_server_select_layout();
  test_server_select_layout_columns();
  test_message_modal_layout();
  test_character_select_layout();
  test_create_character_layout_is_centered();
  return 0;
}
