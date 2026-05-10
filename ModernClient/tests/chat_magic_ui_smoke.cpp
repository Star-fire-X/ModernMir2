#include "game/game_state.hpp"
#include "ui/ui.hpp"

#include <cassert>
#include <string>

namespace {

using namespace mir2::client;
using namespace mir2::client_v1;
namespace ui = mir2::client::ui;

std::string narrow_ascii(const std::wstring& text) {
  std::string out;
  out.reserve(text.size());
  for (wchar_t ch : text) {
    out.push_back(static_cast<char>(ch));
  }
  return out;
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

void test_chat_input_preserves_legacy_prefixes() {
  ui::UiTree tree;
  auto* root = tree.set_root<ui::UiNode>(RectI{0, 0, 800, 600});
  auto* input = root->emplace_child<ui::TextEdit>(RectI{10, 10, 200, 24});

  std::string submitted;
  input->on_submit = [&] {
    submitted = narrow_ascii(input->value);
    input->value.clear();
  };

  click_at(tree, 20, 20);
  InputState text{};
  text.text_input = L"!hello @npc /move";
  tree.update(text);
  assert(input->value == L"!hello @npc /move");

  InputState enter{};
  enter.enter_pressed = true;
  tree.update(enter);
  assert(submitted == "!hello @npc /move");
  assert(input->value.empty());
}

void test_chat_board_wrap_scroll_and_color() {
  GameStateStore state;
  state.apply(ChatLine{std::string(80, 'A'), 0xFF00FF00U, 0xFF000000U});
  assert(state.world.chat_lines.size() == 2);
  assert(state.world.chat_lines.front().fore_color == 0xFF00FF00U);
  assert(state.world.chat_lines.front().back_color == 0xFF000000U);

  for (int index = 0; index < 205; ++index) {
    state.push_chat_line("m" + std::to_string(index), 0xFFFFFFFFU, 0);
  }
  assert(state.world.chat_lines.size() == 200);
  assert(state.world.chat_lines.front().text == "m5");
  assert(state.world.chat_board_top ==
         static_cast<int>(state.world.chat_lines.size()) - 9);
}

void test_magic_list_binding_rebind_and_spell_intent() {
  GameStateStore state;
  MagicList list;
  list.magics.push_back(MagicEntry{1, 1, 0, 0, 500, "Fireball", 7, 100, 0});
  list.magics.push_back(MagicEntry{2, 0, 0, 0, 700, "Thunder", 8, 100, 1});
  state.apply(list);

  ui::UiTree tree;
  auto* root = tree.set_root<ui::UiNode>(RectI{0, 0, 800, 600});
  auto* magics = root->emplace_child<ui::ListBox>(RectI{10, 10, 180, 60});
  for (const auto& magic : state.world.magics) {
    magics->items.push_back(std::wstring(magic.name.begin(), magic.name.end()));
  }

  std::uint16_t selected_magic = 0;
  magics->on_selection_changed = [&](const int index) {
    selected_magic = state.world.magics[static_cast<std::size_t>(index)].magic_id;
  };
  click_at(tree, 20, 38);
  assert(selected_magic == 2);

  state.bind_magic_key(selected_magic, 1);
  assert(state.world.magics[0].key == 0);
  assert(state.world.magics[1].key == 1);
  state.bind_magic_key(selected_magic, 0);
  assert(state.world.magics[1].key == 0);

  const SpellIntent spell{331, 270, 2, 2000, selected_magic};
  assert(spell.magic_id == 2);
  assert(spell.target_actor_id == 2000);
}

void test_hud_and_state_pages_refresh_from_ability_messages() {
  GameStateStore state;
  state.apply(SelfAbility{22, 1, 1234, 5000, 31, 80, 4321, 2});
  assert(state.world.self_ability.level == 22);
  assert(state.world.self_ability.gold == 4321);
  assert(state.world.self_ability_detail.level == 22);
  assert(state.world.self_ability_detail.weight == 31);

  SelfAbilityDetail detail;
  detail.level = 23;
  detail.job = 2;
  detail.hp = 45;
  detail.max_hp = 60;
  detail.mp = 30;
  detail.max_mp = 70;
  detail.exp = 2222;
  detail.max_exp = 9999;
  detail.weight = 28;
  detail.max_weight = 90;
  detail.guild_name = "Guild";
  state.apply(detail);
  assert(state.world.self_ability.level == 23);
  assert(state.world.self_ability.job == 2);
  assert(state.world.self_ability_detail.hp == 45);
  assert(state.world.self_ability_detail.guild_name == "Guild");
}

}  // namespace

int main() {
  test_chat_input_preserves_legacy_prefixes();
  test_chat_board_wrap_scroll_and_color();
  test_magic_list_binding_rebind_and_spell_intent();
  test_hud_and_state_pages_refresh_from_ability_messages();
  return 0;
}
