#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

int fail(std::string_view stage) {
  std::fprintf(stderr, "death_pk_exp_order_smoke failed at %.*s\n",
               static_cast<int>(stage.size()), stage.data());
  return 1;
}

std::optional<mir2::DecodedLegacyGamePacket> find_packet(
    const mir2::RuntimeDispatch& dispatch, std::uint64_t session_id, std::uint16_t ident) {
  for (const auto& event : dispatch.session_events) {
    if (event.session_id != session_id) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return decoded;
    }
  }
  return std::nullopt;
}

std::optional<std::size_t> packet_index(const mir2::RuntimeDispatch& dispatch,
                                        std::uint16_t ident) {
  for (std::size_t index = 0; index < dispatch.session_events.size(); ++index) {
    const auto decoded = mir2::decode_legacy_game_packet(dispatch.session_events[index].packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return index;
    }
  }
  return std::nullopt;
}

mir2::CharacterRecord player(std::string account, std::string name, std::int32_t x,
                             std::int32_t y, std::uint16_t hp, std::int32_t pk) {
  mir2::CharacterRecord record;
  record.account_id = std::move(account);
  record.character_name = std::move(name);
  record.map_id = "0";
  record.x = x;
  record.y = y;
  record.gold = 100;
  record.pk_point = pk;
  record.ability.level = 30;
  record.ability.hp = hp;
  record.ability.max_hp = hp;
  record.ability.mp = 20;
  record.ability.max_mp = 20;
  record.ability.dc = mir2::make_word(120, 120);
  record.ability.max_exp = 1000;
  record.ability.max_weight = 100;
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

mir2::LogicCommand attack(std::uint64_t session_id, std::uint64_t target_id,
                          std::int32_t x, std::int32_t y) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::attack;
  command.session_id = session_id;
  command.target_actor_id = target_id;
  command.x = x;
  command.y = y;
  command.game_message.ident = mir2::kCmHit;
  return command;
}

std::optional<std::uint64_t> actor_id_from_login(const mir2::RuntimeDispatch& dispatch,
                                                 std::uint64_t session_id) {
  const auto packet = find_packet(dispatch, session_id, mir2::kSmNewMap);
  if (!packet.has_value()) {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(static_cast<std::uint32_t>(packet->message.recog));
}

mir2::HostConfig pk_config(bool fight_zone) {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 7;
  mir2::MapConfig map{"0", "PkDeath", {}, 0, 0, 20, 20};
  map.allow_pk = true;
  map.fight_zone = fight_zone;
  config.maps.push_back(map);
  mir2::ItemConfig sword;
  sword.id = 1;
  sword.name = "Sword";
  sword.std_mode = 5;
  sword.weight = 1;
  sword.dc = mir2::make_word(120, 120);
  config.items.push_back(sword);
  return config;
}

int killer_pk_after_death(std::int32_t victim_pk, bool fight_zone,
                          std::int32_t expected_pk) {
  mir2::LogicRuntime runtime(pk_config(fight_zone));
  runtime.initialize();
  auto killer_record = player("killer", "Killer", 10, 10, 100, 0);
  mir2::LegacyUserItem sword;
  sword.index = 1;
  sword.make_index = 1001;
  sword.dura = 1000;
  sword.dura_max = 1000;
  killer_record.equipped_items[mir2::kEquipWeapon] = sword;
  static_cast<void>(runtime.route_logic_command(enter(10, killer_record)));
  const auto killer_login = runtime.tick(1000);
  if (!actor_id_from_login(killer_login, 10).has_value()) {
    return fail("killer login");
  }
  static_cast<void>(runtime.route_logic_command(
      enter(11, player("victim", "Victim", 10, 9, 1, victim_pk))));
  const auto victim_login = runtime.tick(1251);
  const auto victim_id = actor_id_from_login(victim_login, 11);
  if (!victim_id.has_value()) {
    return fail("victim login");
  }

  static_cast<void>(runtime.route_logic_command(attack(10, *victim_id, 10, 9)));
  const auto death = runtime.tick(4503);
  const auto victim_after = runtime.snapshot_character_actor("Victim");
  if (!victim_after.has_value() || victim_after->ability.hp != 0) {
    return fail("player not dead");
  }
  if (!find_packet(death, 11, mir2::kSmNowDeath).has_value() &&
      !find_packet(death, 11, mir2::kSmDeath).has_value()) {
    return fail("player death packet");
  }
  const auto killer = runtime.snapshot_character_actor("Killer");
  if (!killer.has_value() || killer->pk_point != expected_pk) {
    return fail("killer pk");
  }
  return 0;
}

int monster_exp_before_death_packet() {
  mir2::HostConfig config;
  mir2::MapConfig map{"0", "MonsterDeath", {}, 0, 0, 20, 20};
  config.maps.push_back(map);
  mir2::ItemConfig sword;
  sword.id = 1;
  sword.name = "Sword";
  sword.std_mode = 5;
  sword.weight = 1;
  sword.dc = mir2::make_word(120, 120);
  config.items.push_back(sword);
  mir2::MonsterDefConfig oma;
  oma.name = "Oma";
  oma.hp = 8;
  oma.dc = 1;
  oma.exp = 10;
  oma.ai_profile = mir2::MonsterAiProfile::aggressive;
  config.monsters.push_back(oma);
  config.spawns.push_back(mir2::SpawnConfig{"0", "monster", "Oma", 10, 9, 0,
                                            1, 200, 4, 0, 0, 10});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  static_cast<void>(runtime.tick(1000));
  static_cast<void>(runtime.tick(1201));
  auto hero = player("hero", "Hero", 10, 10, 40, 0);
  mir2::LegacyUserItem hero_sword;
  hero_sword.index = 1;
  hero_sword.make_index = 2001;
  hero_sword.dura = 1000;
  hero_sword.dura_max = 1000;
  hero.equipped_items[mir2::kEquipWeapon] = hero_sword;
  static_cast<void>(runtime.route_logic_command(enter(20, hero)));
  if (!actor_id_from_login(runtime.tick(1252), 20).has_value()) {
    return fail("hero login");
  }
  static_cast<void>(runtime.route_logic_command(attack(20, 0, 10, 9)));
  const auto death = runtime.tick(1503);
  const auto exp_index = packet_index(death, mir2::kSmWinExp);
  const auto death_index = packet_index(death, mir2::kSmDeath);
  if (!exp_index.has_value() || !death_index.has_value() || !(*exp_index < *death_index)) {
    return fail("monster exp before death");
  }
  return 0;
}

}  // namespace

int main() {
  if (const auto result = killer_pk_after_death(0, false, 100); result != 0) {
    return result;
  }
  if (const auto result = killer_pk_after_death(250, false, 0); result != 0) {
    return result;
  }
  if (const auto result = killer_pk_after_death(0, true, 0); result != 0) {
    return result;
  }
  if (const auto result = monster_exp_before_death_packet(); result != 0) {
    return result;
  }
  return 0;
}
