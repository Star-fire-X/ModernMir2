#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/legacy_item_rules.hpp"
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
                      std::int32_t item_desc = 0) {
  mir2::ItemConfig config;
  config.id = id;
  config.name = std::move(name);
  config.std_mode = std_mode;
  config.weight = 1;
  config.price = 100;
  config.looks = id;
  config.dura_max = 1000;
  config.item_desc = item_desc;
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

mir2::CharacterRecord character(std::string name, std::int32_t x, std::int32_t y) {
  mir2::CharacterRecord record;
  record.account_id = "acct_" + name;
  record.character_name = std::move(name);
  record.map_id = "0";
  record.x = x;
  record.y = y;
  record.gold = 1234;
  record.ability.level = 30;
  record.ability.hp = 100;
  record.ability.max_hp = 100;
  record.ability.mp = 20;
  record.ability.max_mp = 20;
  record.ability.dc = mir2::make_word(120, 120);
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

mir2::LogicCommand attack(std::uint64_t session_id, std::uint64_t target_actor_id) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::attack;
  command.session_id = session_id;
  command.target_actor_id = target_actor_id;
  command.x = 10;
  command.y = 9;
  command.game_message.ident = mir2::kCmHit;
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

}  // namespace

int main() {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 7;
  mir2::MapConfig map{"0", "DeathDropMap", {}, 0, 0, 20, 20};
  map.allow_pk = true;
  config.maps.push_back(map);
  auto sword = item(1, "Heavy Sword", 5);
  sword.dc = mir2::make_word(100, 100);
  config.items.push_back(sword);
  config.items.push_back(item(2, "Fragile Ring", 22, mir2::kLegacyItemDieAndBreak));
  config.items.push_back(item(3, "Drop Gem", 41));
  config.items.push_back(item(4, "Soul Token", 41, mir2::kLegacyItemNeverLose));

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  std::uint64_t now_ms = 1000;

  auto killer = character("Killer", 10, 10);
  killer.equipped_items[mir2::kEquipWeapon] = user_item(1001, 1);
  static_cast<void>(enter_actor(runtime, 30, killer, now_ms));

  auto victim = character("Victim", 10, 9);
  victim.ability.hp = 10;
  victim.ability.max_hp = 10;
  victim.pk_point = 250;
  victim.body_luck = 10000.0;
  victim.equipped_items[mir2::kEquipRingLeft] = user_item(2001, 2);
  victim.bag_items[0] = user_item(2002, 3);
  victim.bag_items[1] = user_item(2003, 4);
  const auto victim_id = enter_actor(runtime, 31, victim, now_ms);

  now_ms += 3001;
  const auto dispatch = route_due(runtime, attack(30, victim_id), now_ms);
  assert(find_packet(dispatch, mir2::kSmDeath).has_value());
  assert(find_packet(dispatch, mir2::kSmItemShow).has_value());
  assert(find_packet(dispatch, mir2::kSmDelItem, 31).has_value());
  const auto snapshot = runtime.snapshot_character_actor("Victim");
  assert(snapshot.has_value());
  assert(snapshot->gold == 1234);
  assert(snapshot->body_luck < 10000.0);
  assert(mir2::is_empty(snapshot->equipped_items[mir2::kEquipRingLeft]));
  assert(!mir2::is_empty(snapshot->bag_items[0]));
  assert(snapshot->bag_items[0].make_index == 2003);
  assert(mir2::is_empty(snapshot->bag_items[1]));

  const auto item_show_count = std::count_if(
      dispatch.session_events.begin(), dispatch.session_events.end(),
      [](const mir2::SessionEvent& event) {
        const auto decoded = mir2::decode_legacy_game_packet(event.packet);
        return decoded.has_value() && decoded->message.ident == mir2::kSmItemShow;
      });
  assert(item_show_count == 2);
  return 0;
}
