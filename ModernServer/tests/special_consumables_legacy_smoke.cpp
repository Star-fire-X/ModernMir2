#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

mir2::LogicCommand make_enter(std::uint64_t session_id, mir2::CharacterRecord character) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = session_id;
  command.account_id = character.account_id;
  command.character_name = character.character_name;
  command.map_id = character.map_id;
  command.x = character.x;
  command.y = character.y;
  command.character = std::move(character);
  return command;
}

mir2::LogicCommand make_use(std::uint64_t session_id, std::int32_t make_index,
                            std::string name) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::eat_item;
  command.session_id = session_id;
  command.item_make_index = make_index;
  command.text = std::move(name);
  return command;
}

std::optional<mir2::DecodedLegacyGamePacket> find_packet(const mir2::RuntimeDispatch& dispatch,
                                                         std::uint16_t ident) {
  for (const auto& event : dispatch.session_events) {
    if (event.kind != mir2::SessionEventKind::send_packet) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return decoded;
    }
  }
  return std::nullopt;
}

bool bag_has_make_index(const mir2::CharacterRecord& character, std::int32_t make_index) {
  return std::any_of(character.bag_items.begin(), character.bag_items.end(),
                     [&](const mir2::LegacyUserItem& item) {
                       return item.make_index == make_index;
                     });
}

std::int32_t bag_count_by_index(const mir2::CharacterRecord& character, std::int32_t item_index) {
  return static_cast<std::int32_t>(
      std::count_if(character.bag_items.begin(), character.bag_items.end(),
                    [&](const mir2::LegacyUserItem& item) { return item.index == item_index; }));
}

mir2::RuntimeDispatch tick_player_due(mir2::LogicRuntime& runtime, std::uint64_t& now_ms) {
  now_ms += 251;
  return runtime.tick(now_ms);
}

}  // namespace

int main() {
  mir2::HostConfig config;
  auto no_drug = mir2::MapConfig{"0", "NoDrugMap", {}, 20, 20, 10, 10};
  no_drug.no_drug = true;
  config.maps.push_back(no_drug);
  config.maps.push_back(mir2::MapConfig{"1", "ScrollMap", {}, 20, 20, 10, 10});

  config.items.push_back(mir2::ItemConfig{1, "Healing Potion", 1, 10, 0, 0, 1, 1, -1, 10, 0});
  mir2::ItemConfig bundle{2, "Potion Bundle", 1, 10, 3, 0, 2, 1, -1, 0, 0};
  bundle.unbind_item = "Healing Potion";
  bundle.unbind_count = 3;
  config.items.push_back(bundle);
  mir2::ItemConfig town_scroll{3, "Town Portal", 1, 10, 31, 0, 3, 1, -1, 0, 0};
  town_scroll.scroll_kind = "town";
  config.items.push_back(town_scroll);
  mir2::ItemConfig random_scroll{4, "Random Scroll", 1, 10, 31, 1, 4, 1, -1, 0, 0};
  random_scroll.scroll_kind = "random";
  config.items.push_back(random_scroll);

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  mir2::CharacterRecord hero;
  hero.account_id = "acct";
  hero.character_name = "Hero";
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;
  hero.ability.hp = 5;
  hero.ability.max_hp = 15;
  hero.ability.max_weight = 20;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;
  hero.bag_items[0] = mir2::LegacyUserItem{1001, 1, 1, 1};

  static_cast<void>(runtime.route_logic_command(make_enter(701, hero)));
  std::uint64_t now_ms = 20;
  static_cast<void>(runtime.tick(now_ms));
  static_cast<void>(runtime.route_logic_command(make_use(701, 1001, "Healing Potion")));
  auto dispatch = tick_player_due(runtime, now_ms);
  assert(find_packet(dispatch, mir2::kSmEatFail).has_value());
  auto snapshot = runtime.snapshot_character_actor("Hero");
  assert(snapshot.has_value());
  assert(snapshot->ability.hp == 5);
  assert(bag_has_make_index(*snapshot, 1001));

  mir2::CharacterRecord user;
  user.account_id = "acct";
  user.character_name = "User";
  user.map_id = "1";
  user.x = 12;
  user.y = 12;
  user.ability.hp = 5;
  user.ability.max_hp = 15;
  user.ability.max_weight = 20;
  user.ability.max_wear_weight = 100;
  user.ability.max_hand_weight = 100;
  user.bag_items[0] = mir2::LegacyUserItem{2001, 2, 1, 1};
  user.bag_items[1] = mir2::LegacyUserItem{2002, 3, 1, 1};
  user.bag_items[2] = mir2::LegacyUserItem{2003, 4, 1, 1};

  static_cast<void>(runtime.route_logic_command(make_enter(702, user)));
  now_ms += 20;
  static_cast<void>(runtime.tick(now_ms));

  static_cast<void>(runtime.route_logic_command(make_use(702, 2001, "Potion Bundle")));
  dispatch = tick_player_due(runtime, now_ms);
  assert(find_packet(dispatch, mir2::kSmEatOk).has_value());
  snapshot = runtime.snapshot_character_actor("User");
  assert(snapshot.has_value());
  assert(!bag_has_make_index(*snapshot, 2001));
  assert(bag_count_by_index(*snapshot, 1) == 3);

  static_cast<void>(runtime.route_logic_command(make_use(702, 2003, "Random Scroll")));
  dispatch = tick_player_due(runtime, now_ms);
  assert(find_packet(dispatch, mir2::kSmEatOk).has_value());
  auto moved = runtime.snapshot_character_actor("User");
  assert(moved.has_value());
  assert(moved->map_id == "1");
  assert(moved->x != 12 || moved->y != 12);
  assert(!bag_has_make_index(*moved, 2003));

  static_cast<void>(runtime.route_logic_command(make_use(702, 2002, "Town Portal")));
  dispatch = tick_player_due(runtime, now_ms);
  assert(find_packet(dispatch, mir2::kSmEatOk).has_value());
  moved = runtime.snapshot_character_actor("User");
  assert(moved.has_value());
  assert(moved->x == 10);
  assert(moved->y == 10);
  assert(!bag_has_make_index(*moved, 2002));

  return 0;
}
