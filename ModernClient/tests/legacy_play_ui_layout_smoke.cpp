#include "scene/legacy_play_ui.hpp"

#include <cassert>

namespace {

using mir2::client::RectI;
namespace play = mir2::client::legacy_play_ui;

bool same(const RectI lhs, const RectI rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.w == rhs.w && lhs.h == rhs.h;
}

}  // namespace

int main() {
  const auto hud = play::legacy_hud_layout(132);
  assert(same(hud.bottom_board, RectI{0, 468, 800, 132}));
  assert(same(hud.quick_belt[0], RectI{285, 59, 32, 29}));
  assert(same(hud.quick_belt[5], RectI{503, 59, 32, 29}));
  assert(same(hud.state_button, RectI{643, 61, 38, 38}));
  assert(same(hud.bag_button, RectI{682, 41, 38, 38}));
  assert(same(hud.magic_button, RectI{722, 21, 38, 38}));
  assert(same(hud.option_button, RectI{764, 11, 36, 36}));
  assert(same(hud.minimap_button, RectI{219, 104, 28, 18}));
  assert(same(hud.trade_button, RectI{249, 104, 28, 18}));
  assert(same(hud.guild_button, RectI{279, 104, 28, 18}));
  assert(same(hud.group_button, RectI{309, 104, 28, 18}));
  assert(same(hud.plus_button, RectI{339, 104, 28, 18}));
  assert(same(hud.logout_button, RectI{530, 104, 28, 18}));
  assert(same(hud.exit_button, RectI{560, 104, 28, 18}));

  const auto chat = play::legacy_chat_layout();
  assert(same(chat.board, RectI{208, 470, 374, 108}));
  assert(same(chat.edit, RectI{208, 581, 387, 12}));
  assert(chat.visible_lines == 9);
  assert(chat.line_height == 12);
  assert(chat.max_length == 70);

  const auto sys = play::legacy_system_message_layout();
  assert(same(sys.bounds, RectI{30, 40, 740, 160}));
  assert(sys.line_height == 16);
  assert(sys.max_messages == 10U);
  assert(sys.expire_ms == 3000U);
  return 0;
}
