#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>

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

mir2::LogicCommand make_item_command(mir2::LogicCommandKind kind, std::uint64_t session_id,
                                     std::int32_t make_index, std::string name,
                                     std::int32_t slot = -1) {
  mir2::LogicCommand command;
  command.kind = kind;
  command.session_id = session_id;
  command.item_make_index = make_index;
  command.item_slot = slot;
  command.text = std::move(name);
  return command;
}

bool has_item_trace(const mir2::RuntimeDispatch& dispatch, const std::string& action,
                    const std::string& label) {
  return std::any_of(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                     [&](const mir2::LegacyRuntimeTrace& trace) {
                       return trace.stage == "LegacyItem" && trace.action == action &&
                              trace.label == label;
                     });
}

bool bag_has_make_index(const mir2::CharacterRecord& character, std::int32_t make_index) {
  return std::any_of(character.bag_items.begin(), character.bag_items.end(),
                     [&](const mir2::LegacyUserItem& item) {
                       return item.make_index == make_index;
                     });
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "ItemMap", {}, 0, 0, 10, 10});
  config.items.push_back(mir2::ItemConfig{1, "Wooden Sword", 3, 100, 5, 1, 1, 1000, 1, 0, 0});
  config.items.push_back(mir2::ItemConfig{2, "Basic Drug", 1, 30, 0, 0, 2, 1, -1, 10, 0});

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
  hero.ability.mp = 3;
  hero.ability.max_mp = 10;
  hero.ability.max_exp = 100;
  hero.ability.max_weight = 30;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;
  hero.bag_items[0].index = 1;
  hero.bag_items[0].make_index = 1001;
  hero.bag_items[0].dura = 600;
  hero.bag_items[0].dura_max = 1000;
  hero.bag_items[1].index = 2;
  hero.bag_items[1].make_index = 1002;
  hero.bag_items[1].dura = 1;
  hero.bag_items[1].dura_max = 1;

  static_cast<void>(runtime.route_logic_command(make_enter(81, hero)));
  static_cast<void>(runtime.tick());

  static_cast<void>(
      runtime.route_logic_command(make_item_command(mir2::LogicCommandKind::eat_item, 81, 9999, "")));
  const auto bad_eat = runtime.tick();
  assert(has_item_trace(bad_eat, "bag_reject", "eat_item"));
  auto snapshot = runtime.snapshot_character_actor("Hero");
  assert(snapshot.has_value());
  assert(snapshot->ability.hp == 5);
  assert(bag_has_make_index(*snapshot, 1002));

  static_cast<void>(runtime.route_logic_command(
      make_item_command(mir2::LogicCommandKind::drop_item, 81, 1001, "Wooden Sword")));
  const auto drop = runtime.tick();
  assert(has_item_trace(drop, "validate", "drop_item"));
  assert(has_item_trace(drop, "success", "drop_item"));
  snapshot = runtime.snapshot_character_actor("Hero");
  assert(snapshot.has_value());
  assert(!bag_has_make_index(*snapshot, 1001));

  mir2::LogicCommand pickup;
  pickup.kind = mir2::LogicCommandKind::pickup_item;
  pickup.session_id = 81;
  pickup.x = 10;
  pickup.y = 10;
  const auto pickup_dispatch = [&]() {
    static_cast<void>(runtime.route_logic_command(pickup));
    return runtime.tick();
  }();
  assert(has_item_trace(pickup_dispatch, "success", "pickup_item"));
  snapshot = runtime.snapshot_character_actor("Hero");
  assert(snapshot.has_value());
  assert(bag_has_make_index(*snapshot, 1001));

  static_cast<void>(
      runtime.route_logic_command(make_item_command(mir2::LogicCommandKind::eat_item, 81, 1002,
                                                    "Basic Drug")));
  const auto eat = runtime.tick();
  assert(has_item_trace(eat, "success", "eat_item"));
  snapshot = runtime.snapshot_character_actor("Hero");
  assert(snapshot.has_value());
  assert(snapshot->ability.hp == 15);
  assert(!bag_has_make_index(*snapshot, 1002));

  return 0;
}
