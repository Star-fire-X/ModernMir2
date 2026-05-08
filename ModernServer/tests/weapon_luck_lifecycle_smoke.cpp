#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

#undef assert
#define assert(expression)                                                     \
  do {                                                                         \
    if (!(expression)) {                                                       \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);  \
      std::abort();                                                            \
    }                                                                          \
  } while (false)

namespace {

mir2::ItemConfig item(std::int32_t id, std::string name, std::int32_t std_mode,
                      std::int32_t shape = 0) {
  mir2::ItemConfig config;
  config.id = id;
  config.name = std::move(name);
  config.std_mode = std_mode;
  config.shape = shape;
  config.weight = 1;
  config.price = 100;
  config.looks = id;
  config.dura_max = 1000;
  return config;
}

mir2::LegacyUserItem user_item(std::int32_t make_index, std::int32_t id) {
  mir2::LegacyUserItem item;
  item.make_index = make_index;
  item.index = static_cast<std::uint16_t>(id);
  item.dura = 1000;
  item.dura_max = 1000;
  return item;
}

mir2::CharacterRecord character(std::string name) {
  mir2::CharacterRecord record;
  record.account_id = "acct_" + name;
  record.character_name = std::move(name);
  record.map_id = "0";
  record.x = 10;
  record.y = 10;
  record.gold = 1000;
  record.ability.level = 30;
  record.ability.hp = 100;
  record.ability.max_hp = 100;
  record.ability.mp = 20;
  record.ability.max_mp = 20;
  record.ability.dc = mir2::make_word(20, 20);
  record.ability.max_exp = 1000;
  record.ability.max_weight = 60;
  record.ability.max_wear_weight = 100;
  record.ability.max_hand_weight = 100;
  record.attack_mode = 0;
  return record;
}

mir2::LogicCommand enter(std::uint64_t session_id, mir2::CharacterRecord record) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = session_id;
  command.account_id = record.account_id;
  command.character_name = record.character_name;
  command.map_id = record.map_id;
  command.x = record.x;
  command.y = record.y;
  command.character = std::move(record);
  return command;
}

mir2::LogicCommand eat(std::uint64_t session_id, std::int32_t make_index, std::string name) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::eat_item;
  command.session_id = session_id;
  command.item_make_index = make_index;
  command.text = std::move(name);
  return command;
}

std::optional<mir2::DecodedLegacyGamePacket> find_packet(
    const mir2::RuntimeDispatch& dispatch, std::uint16_t ident,
    std::uint64_t session_id = 0) {
  for (const auto& event : dispatch.session_events) {
    if (session_id != 0 && event.session_id != session_id) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return decoded;
    }
  }
  return std::nullopt;
}

std::uint64_t enter_actor(mir2::LogicRuntime& runtime, std::uint64_t session_id,
                          mir2::CharacterRecord record, std::uint64_t& now_ms) {
  static_cast<void>(runtime.route_logic_command(enter(session_id, std::move(record))));
  now_ms += 251;
  const auto dispatch = runtime.tick(now_ms);
  const auto map = find_packet(dispatch, mir2::kSmNewMap, session_id);
  assert(map.has_value());
  return static_cast<std::uint64_t>(static_cast<std::uint32_t>(map->message.recog));
}

mir2::RuntimeDispatch route_due(mir2::LogicRuntime& runtime,
                                const mir2::LogicCommand& command,
                                std::uint64_t& now_ms) {
  static_cast<void>(runtime.route_logic_command(command));
  now_ms += 251;
  return runtime.tick(now_ms);
}

mir2::HostConfig config() {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 11;
  config.maps.push_back(mir2::MapConfig{"0", "LuckMap", {}, 0, 0, 20, 20});
  auto sword = item(1, "Sword", 5);
  sword.dc = mir2::make_word(5, 10);
  config.items.push_back(sword);
  config.items.push_back(item(2, "Blessed Oil", 3, 4));
  return config;
}

}  // namespace

int main() {
  {
    mir2::LogicRuntime runtime(config());
    runtime.initialize();
    std::uint64_t now_ms = 1000;
    auto hero = character("NoWeapon");
    hero.bag_items[0] = user_item(1002, 2);
    static_cast<void>(enter_actor(runtime, 40, hero, now_ms));
    const auto dispatch = route_due(runtime, eat(40, 1002, "Blessed Oil"), now_ms);
    const auto eat_fail = find_packet(dispatch, mir2::kSmEatFail, 40);
    assert(eat_fail.has_value());
    const auto snapshot = runtime.snapshot_character_actor("NoWeapon");
    assert(snapshot.has_value());
    assert(!mir2::is_empty(snapshot->bag_items[0]));
    assert(snapshot->bag_items[0].make_index == 1002);
  }

  {
    mir2::LogicRuntime runtime(config());
    runtime.initialize();
    std::uint64_t now_ms = 2000;
    auto hero = character("OilHero");
    hero.equipped_items[mir2::kEquipWeapon] = user_item(2001, 1);
    hero.equipped_items[mir2::kEquipWeapon].desc[4] = 2;
    hero.bag_items[0] = user_item(2002, 2);
    static_cast<void>(enter_actor(runtime, 41, hero, now_ms));
    const auto dispatch = route_due(runtime, eat(41, 2002, "Blessed Oil"), now_ms);
    assert(find_packet(dispatch, mir2::kSmEatOk, 41).has_value());
    assert(find_packet(dispatch, mir2::kSmUpdateItem, 41).has_value());
    const auto snapshot = runtime.snapshot_character_actor("OilHero");
    assert(snapshot.has_value());
    assert(mir2::is_empty(snapshot->bag_items[0]));
    const auto& weapon = snapshot->equipped_items[mir2::kEquipWeapon];
    assert(weapon.desc[3] != 0 || weapon.desc[4] != 2);
  }

  return 0;
}
