#include "scene/legacy_trade_group_guild_ui.hpp"

#include <cassert>

namespace {

using mir2::client::RectI;
namespace social = mir2::client::legacy_trade_group_guild_ui;

bool same(const RectI lhs, const RectI rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.w == rhs.w && lhs.h == rhs.h;
}

}  // namespace

int main() {
  const auto group = social::legacy_group_layout();
  assert(group.resource_index == 120);
  assert(same(group.window, RectI{262, 179, 276, 242}));
  assert(same(group.close_button, RectI{260, 0, 16, 16}));
  assert(same(group.allow_button, RectI{20, 18, 20, 19}));
  assert(same(group.create_button, RectI{22, 203, 72, 19}));
  assert(same(group.add_button, RectI{97, 203, 72, 19}));
  assert(same(group.remove_button, RectI{172, 203, 72, 19}));
  assert(same(group.member_text_rect(0), RectI{28, 80, 210, 16}));
  assert(same(group.member_text_rect(1), RectI{28, 96, 96, 16}));
  assert(same(group.member_text_rect(2), RectI{128, 96, 96, 16}));

  const auto trade = social::legacy_trade_layout();
  assert(trade.local_resource_index == 389);
  assert(trade.remote_resource_index == 390);
  assert(same(trade.remote_window, RectI{464, 0, 220, 175}));
  assert(same(trade.local_window, RectI{464, 160, 236, 175}));
  assert(same(trade.grid, RectI{21, 56, 180, 66}));
  assert(same(trade.cell_rect(0), RectI{21, 56, 36, 33}));
  assert(same(trade.cell_rect(9), RectI{165, 89, 36, 33}));
  assert(trade.slot_at(21, 56) == 0);
  assert(trade.slot_at(200, 121) == 9);
  assert(trade.slot_at(201, 122) == -1);
  assert(same(trade.ok_button, RectI{155, 128, 44, 16}));
  assert(same(trade.close_button, RectI{220, 42, 16, 16}));
  assert(same(trade.gold_button, RectI{11, 137, 44, 16}));

  const auto guild = social::legacy_guild_layout();
  assert(guild.resource_index == 180);
  assert(same(guild.window, RectI{0, -3, 628, 453}));
  assert(same(guild.close_button, RectI{584, 6, 16, 16}));
  assert(same(guild.home_button, RectI{13, 411, 64, 16}));
  assert(same(guild.list_button, RectI{13, 429, 64, 16}));
  assert(same(guild.chat_button, RectI{94, 429, 64, 16}));
  assert(same(guild.add_button, RectI{243, 411, 64, 16}));
  assert(same(guild.remove_button, RectI{243, 429, 64, 16}));
  assert(same(guild.up_button, RectI{595, 239, 16, 16}));
  assert(same(guild.down_button, RectI{595, 291, 16, 16}));
  assert(guild.title_x == 320);
  assert(guild.line_x == 24);
  assert(guild.line_height == 14);
  assert(guild.scroll_step == 3);

  const auto prompt = social::legacy_social_prompt_layout();
  assert(same(prompt.window, RectI{244, 244, 312, 112}));
  assert(same(prompt.edit, RectI{28, 42, 196, 24}));
  assert(same(prompt.ok_button, RectI{232, 38, 54, 24}));
  assert(same(prompt.cancel_button, RectI{232, 68, 54, 24}));
  return 0;
}
