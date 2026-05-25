#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/legacy_item_rules.hpp"
#include "world/logic_runtime.hpp"

#undef assert
#define assert(expression)                                                    \
  do {                                                                        \
    if (!(expression)) {                                                      \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
      std::abort();                                                           \
    }                                                                         \
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

mir2::LegacyUserItem user_item(std::int32_t make_index, std::int32_t id,
                               std::uint16_t dura = 1000) {
  mir2::LegacyUserItem item;
  item.make_index = make_index;
  item.index = static_cast<std::uint16_t>(id);
  item.dura = dura;
  item.dura_max = std::max<std::uint16_t>(dura, 1000);
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

mir2::HostConfig make_config(bool fight_zone) {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 7;
  mir2::MapConfig map{"0", "DeathDropMap", {}, 0, 0, 20, 20};
  map.allow_pk = true;
  map.fight_zone = fight_zone;
  config.maps.push_back(map);
  auto sword = item(1, "Heavy Sword", 5);
  sword.dc = mir2::make_word(100, 100);
  config.items.push_back(sword);
  config.items.push_back(item(2, "Fragile Ring", 22, mir2::kLegacyItemDieAndBreak));
  config.items.push_back(item(3, "Drop Gem", 41));
  config.items.push_back(item(4, "Soul Token", 41, mir2::kLegacyItemNeverLose));
  config.items.push_back(item(5, "Raw Meat", 40));
  return config;
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

mir2::LogicCommand pickup(std::uint64_t session_id, std::int32_t x, std::int32_t y) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::pickup_item;
  command.session_id = session_id;
  command.x = x;
  command.y = y;
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

std::optional<std::size_t> first_packet_index(const mir2::RuntimeDispatch& dispatch,
                                              std::uint16_t ident,
                                              std::uint64_t session_id = 0) {
  for (std::size_t index = 0; index < dispatch.session_events.size(); ++index) {
    const auto& event = dispatch.session_events[index];
    if (session_id != 0 && event.session_id != session_id) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return index;
    }
  }
  return std::nullopt;
}

std::optional<std::size_t> last_packet_index(const mir2::RuntimeDispatch& dispatch,
                                             std::uint16_t ident,
                                             std::uint64_t session_id = 0) {
  std::optional<std::size_t> result;
  for (std::size_t index = 0; index < dispatch.session_events.size(); ++index) {
    const auto& event = dispatch.session_events[index];
    if (session_id != 0 && event.session_id != session_id) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      result = index;
    }
  }
  return result;
}

std::optional<std::size_t> first_death_packet_index(const mir2::RuntimeDispatch& dispatch) {
  const auto death = first_packet_index(dispatch, mir2::kSmDeath);
  const auto now_death = first_packet_index(dispatch, mir2::kSmNowDeath);
  if (!death.has_value()) {
    return now_death;
  }
  if (!now_death.has_value()) {
    return death;
  }
  return std::min(*death, *now_death);
}

std::optional<mir2::DecodedLegacyGamePacket> find_item_show(
    const mir2::RuntimeDispatch& dispatch, std::string_view name) {
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == mir2::kSmItemShow &&
        mir2::legacy_decode_string(decoded->body) == name) {
      return decoded;
    }
  }
  return std::nullopt;
}

std::optional<mir2::LegacyClientItem> decode_client_item(std::string_view body) {
  mir2::LegacyClientItem item;
  if (!mir2::legacy_decode_buffer(body, &item, sizeof(item))) {
    return std::nullopt;
  }
  return item;
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

bool has_save_character(const mir2::RuntimeDispatch& dispatch, std::string_view name) {
  return std::any_of(dispatch.persist_requests.begin(), dispatch.persist_requests.end(),
                     [&](const mir2::PersistRequest& request) {
                       return request.kind == mir2::PersistRequestKind::save_character &&
                              request.character_name == name;
                     });
}

mir2::CharacterRecord make_victim() {
  auto victim = character("Victim", 10, 9);
  victim.ability.hp = 10;
  victim.ability.max_hp = 10;
  victim.pk_point = 250;
  victim.body_luck = 10000.0;
  victim.equipped_items[mir2::kEquipRingLeft] = user_item(2001, 2);
  victim.bag_items[0] = user_item(2002, 5, 3500);
  victim.bag_items[1] = user_item(2003, 3);
  victim.bag_items[2] = user_item(2004, 4);
  return victim;
}

void assert_index_before(std::optional<std::size_t> before, std::optional<std::size_t> after) {
  assert(before.has_value());
  assert(after.has_value());
  assert(*before < *after);
}

void run_open_map_death_drop_case() {
  mir2::LogicRuntime runtime(make_config(false));
  runtime.initialize();
  std::uint64_t now_ms = 1000;

  auto killer = character("Killer", 10, 10);
  killer.equipped_items[mir2::kEquipWeapon] = user_item(1001, 1);
  static_cast<void>(enter_actor(runtime, 30, killer, now_ms));
  static_cast<void>(enter_actor(runtime, 32, character("Looter", 9, 7), now_ms));
  const auto victim_id = enter_actor(runtime, 31, make_victim(), now_ms);

  now_ms += 3001;
  const auto death = route_due(runtime, attack(30, victim_id), now_ms);
  const auto death_index = first_death_packet_index(death);
  assert(death_index.has_value());
  assert_index_before(last_packet_index(death, mir2::kSmItemShow), death_index);
  assert_index_before(last_packet_index(death, mir2::kSmDelItem, 31), death_index);
  assert_index_before(last_packet_index(death, mir2::kSmSendUseItems, 31), death_index);
  assert_index_before(last_packet_index(death, mir2::kSmWeightChanged, 31), death_index);
  assert_index_before(last_packet_index(death, mir2::kSmAbility, 31), death_index);
  assert_index_before(last_packet_index(death, mir2::kSmSubAbility, 31), death_index);
  assert(find_item_show(death, "Raw Meat").has_value());
  assert(find_item_show(death, "Drop Gem").has_value());
  assert(!find_item_show(death, "Fragile Ring").has_value());
  assert(!find_item_show(death, "Soul Token").has_value());
  assert(!find_packet(death, mir2::kSmGoldChanged).has_value());
  assert(has_save_character(death, "Victim"));

  const auto snapshot = runtime.snapshot_character_actor("Victim");
  assert(snapshot.has_value());
  assert(snapshot->gold == 1234);
  assert(snapshot->body_luck < 10000.0);
  assert(mir2::is_empty(snapshot->equipped_items[mir2::kEquipRingLeft]));
  assert(snapshot->bag_items[0].make_index == 2004);
  assert(mir2::is_empty(snapshot->bag_items[1]));

  const auto meat_show = find_item_show(death, "Raw Meat");
  assert(meat_show.has_value());
  assert(meat_show->message.param == 9);
  assert(meat_show->message.tag == 7);
  const auto pickup_dispatch = route_due(runtime, pickup(32, 9, 7), now_ms);
  assert(find_packet(pickup_dispatch, mir2::kSmItemHide, 32).has_value());
  const auto add = find_packet(pickup_dispatch, mir2::kSmAddItem, 32);
  assert(add.has_value());
  const auto picked = decode_client_item(add->body);
  assert(picked.has_value());
  assert(picked->make_index == 2002);
  assert(picked->item.std_mode == 40);
  assert(picked->dura == 1500);
}

void run_fight_zone_no_death_drop_case() {
  mir2::LogicRuntime runtime(make_config(true));
  runtime.initialize();
  std::uint64_t now_ms = 1000;

  auto killer = character("Killer", 10, 10);
  killer.equipped_items[mir2::kEquipWeapon] = user_item(1001, 1);
  static_cast<void>(enter_actor(runtime, 30, killer, now_ms));
  const auto victim_id = enter_actor(runtime, 31, make_victim(), now_ms);

  now_ms += 3001;
  const auto death = route_due(runtime, attack(30, victim_id), now_ms);
  assert(first_death_packet_index(death).has_value());
  assert(!find_packet(death, mir2::kSmItemShow).has_value());
  assert(!find_packet(death, mir2::kSmDelItem, 31).has_value());
  assert(!find_packet(death, mir2::kSmSendUseItems, 31).has_value());
  assert(!find_packet(death, mir2::kSmWeightChanged, 31).has_value());
  assert(!find_packet(death, mir2::kSmGoldChanged).has_value());
  assert(has_save_character(death, "Victim"));

  const auto snapshot = runtime.snapshot_character_actor("Victim");
  assert(snapshot.has_value());
  assert(snapshot->gold == 1234);
  assert(snapshot->equipped_items[mir2::kEquipRingLeft].make_index == 2001);
  assert(snapshot->bag_items[0].make_index == 2002);
  assert(snapshot->bag_items[1].make_index == 2003);
  assert(snapshot->bag_items[2].make_index == 2004);
}

}  // namespace

int main() {
  run_open_map_death_drop_case();
  run_fight_zone_no_death_drop_case();
  return 0;
}
