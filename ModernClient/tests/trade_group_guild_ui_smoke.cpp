#include "game/game_state.hpp"
#include "ui/ui.hpp"

#include <cassert>
#include <string>

namespace {

using namespace mir2::client;
using namespace mir2::client_v1;
namespace ui = mir2::client::ui;

ItemState make_item(const char* name, const std::int32_t make_index) {
  ItemState item;
  item.name = name;
  item.make_index = make_index;
  item.looks = 1;
  item.dura = 100;
  item.dura_max = 1000;
  return item;
}

std::string narrow_ascii(const std::wstring& value) {
  std::string result;
  result.reserve(value.size());
  for (const auto ch : value) {
    result.push_back(static_cast<char>(ch));
  }
  return result;
}

void click_at(ui::UiTree& tree, const int x, const int y) {
  InputState press{};
  press.mouse_x = x;
  press.mouse_y = y;
  press.left_pressed = true;
  press.left_down = true;
  tree.update(press);

  InputState release{};
  release.mouse_x = x;
  release.mouse_y = y;
  release.left_released = true;
  tree.update(release);
}

void drag_at(ui::UiTree& tree, const int start_x, const int start_y, const int end_x,
             const int end_y) {
  InputState press{};
  press.mouse_x = start_x;
  press.mouse_y = start_y;
  press.left_pressed = true;
  press.left_down = true;
  tree.update(press);

  InputState move{};
  move.mouse_x = end_x;
  move.mouse_y = end_y;
  move.left_down = true;
  tree.update(move);

  InputState release{};
  release.mouse_x = end_x;
  release.mouse_y = end_y;
  release.left_released = true;
  tree.update(release);
}

void type_and_enter(ui::UiTree& tree, std::wstring text) {
  InputState input{};
  input.text_input = std::move(text);
  tree.update(input);

  InputState enter{};
  enter.enter_pressed = true;
  tree.update(enter);
}

void test_group_window_toggle_drag_and_member_hit() {
  GameStateStore state;
  state.apply(GroupState{true, true, {"Hero", "Ally"}});

  ui::UiTree tree;
  auto* root = tree.set_root<ui::UiNode>(RectI{0, 0, 800, 600});
  auto* group = root->emplace_child<ui::Window>(RectI{100, 100, 240, 160});
  group->floating = true;
  auto* allow = group->emplace_child<ui::Button>(RectI{12, 122, 58, 24});
  allow->on_click = [&] { state.world.group.allow_group = !state.world.group.allow_group; };
  auto* members = group->emplace_child<ui::ListBox>(RectI{12, 30, 150, 70});
  members->items = {L"Hero", L"Ally"};
  int selected_member = -1;
  members->on_selection_changed = [&](const int index) {
    selected_member = index;
  };
  group->show(tree);

  click_at(tree, 118, 234);
  assert(!state.world.group.allow_group);
  click_at(tree, 118, 138);
  click_at(tree, 118, 160);
  assert(members->selected_index == 1);
  assert(selected_member == 1);

  const auto old_x = group->bounds.x;
  drag_at(tree, 104, 104, 144, 124);
  assert(group->bounds.x > old_x);

  group->hide(tree);
  state.world.group.visible = false;
  assert(!group->visible);
  assert(!state.world.group.visible);
}

void test_trade_slots_gold_accept_and_close() {
  GameStateStore state;
  const auto ruby = make_item("Ruby", 1001);
  const auto sapphire = make_item("Sapphire", 2001);
  state.apply(TradeState{true, "Ally", {ItemSlotState{0, ruby}},
                         {ItemSlotState{0, sapphire}}, 0, 7, false, false});

  ui::UiTree tree;
  auto* root = tree.set_root<ui::UiNode>(RectI{0, 0, 800, 600});
  auto* trade = root->emplace_child<ui::Window>(RectI{120, 90, 260, 180});
  trade->floating = true;
  auto* local = trade->emplace_child<ui::Grid>(RectI{12, 34, 80, 40});
  local->col_count = 2;
  local->row_count = 1;
  local->col_width = 40;
  local->row_height = 40;
  local->on_cell_select = [&](ui::Grid&, const int col, const int row) {
    const auto index = row * 2 + col;
    assert(index == 0);
  };
  auto* gold = trade->emplace_child<ui::TextEdit>(RectI{12, 88, 80, 28});
  gold->on_submit = [&] {
    state.world.trade.local_gold = std::stoi(gold->value);
  };
  auto* accept = trade->emplace_child<ui::Button>(RectI{110, 142, 60, 24});
  accept->on_click = [&] { state.world.trade.local_accept = true; };
  auto* close = trade->emplace_child<ui::Button>(RectI{180, 142, 60, 24});
  close->on_click = [&] {
    state.apply(TradeState{});
    trade->hide(tree);
  };
  trade->show(tree);

  click_at(tree, 136, 130);
  click_at(tree, 136, 184);
  type_and_enter(tree, L"25");
  assert(state.world.trade.local_gold == 25);
  click_at(tree, 236, 238);
  assert(state.world.trade.local_accept);
  click_at(tree, 306, 238);
  assert(!trade->visible);
  assert(!state.world.trade.visible);
}

void test_guild_member_notice_rank_controls() {
  GameStateStore state;
  state.apply(GuildState{true,
                         "Guild",
                         "Leader",
                         "Notice",
                         {GuildMemberState{"Hero", "Leader", true},
                          GuildMemberState{"Ally", "Member", false}},
                         {"Leader", "Member"},
                         true});

  ui::UiTree tree;
  auto* root = tree.set_root<ui::UiNode>(RectI{0, 0, 800, 600});
  auto* guild = root->emplace_child<ui::Window>(RectI{140, 80, 300, 220});
  guild->floating = true;
  auto* members = guild->emplace_child<ui::ListBox>(RectI{12, 34, 160, 80});
  members->items = {L"Hero", L"Ally"};
  std::string selected;
  members->on_selection_changed = [&](const int index) {
    selected = state.world.guild.members[static_cast<std::size_t>(index)].name;
  };
  auto* notice = guild->emplace_child<ui::TextEdit>(RectI{12, 126, 120, 28});
  notice->on_submit = [&] {
    state.world.guild.notice = narrow_ascii(notice->value);
  };
  auto* grade = guild->emplace_child<ui::TextEdit>(RectI{140, 126, 120, 28});
  grade->on_submit = [&] {
    state.world.guild.ranks = {narrow_ascii(grade->value)};
  };
  auto* remove = guild->emplace_child<ui::Button>(RectI{210, 176, 54, 24});
  remove->on_click = [&] {
    assert(selected == "Ally");
    state.world.guild.members.pop_back();
  };
  guild->show(tree);

  click_at(tree, 158, 138);
  click_at(tree, 158, 160);
  assert(selected == "Ally");
  click_at(tree, 158, 220);
  type_and_enter(tree, L"New notice");
  assert(state.world.guild.notice == "New notice");
  click_at(tree, 286, 220);
  type_and_enter(tree, L"Officer");
  assert(state.world.guild.ranks.size() == 1);
  assert(state.world.guild.ranks.front() == "Officer");
  click_at(tree, 358, 268);
  assert(state.world.guild.members.size() == 1);
}

}  // namespace

int main() {
  test_group_window_toggle_drag_and_member_hit();
  test_trade_slots_gold_accept_and_close();
  test_guild_member_notice_rank_controls();
  return 0;
}
