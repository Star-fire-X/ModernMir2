#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <unordered_map>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/game_object.hpp"
#include "world/legacy_item_rules.hpp"
#include "world/logic_runtime.hpp"

#undef assert
#define assert(expression)                                                       \
  do {                                                                           \
    if (!(expression)) {                                                         \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);    \
      std::abort();                                                              \
    }                                                                            \
  } while (false)

namespace {

constexpr std::int32_t kTransparentStatusBit = 0x00800000;
constexpr std::int32_t kPoisonStoneStatusBit = 0x04000000;

mir2::ItemConfig item(std::int32_t id, std::string name, std::int32_t std_mode,
                      std::int32_t shape = 0) {
  mir2::ItemConfig config;
  config.id = id;
  config.name = std::move(name);
  config.weight = 1;
  config.price = 100;
  config.std_mode = std_mode;
  config.shape = shape;
  config.looks = id;
  config.dura_max = 1000;
  return config;
}

mir2::LegacyUserItem user_item(std::int32_t make_index, std::int32_t id,
                               std::uint16_t dura = 1000) {
  mir2::LegacyUserItem item;
  item.make_index = make_index;
  item.index = static_cast<std::uint16_t>(id);
  item.dura = dura;
  item.dura_max = 1000;
  return item;
}

mir2::CharacterRecord character(std::string name, std::int32_t x = 10,
                                std::int32_t y = 10) {
  mir2::CharacterRecord record;
  record.account_id = "acct_" + name;
  record.character_name = std::move(name);
  record.map_id = "0";
  record.x = x;
  record.y = y;
  record.ability.level = 40;
  record.ability.hp = 100;
  record.ability.max_hp = 100;
  record.ability.mp = 50;
  record.ability.max_mp = 50;
  record.ability.dc = mir2::make_word(10, 20);
  record.ability.max_exp = 1000;
  record.ability.max_weight = 50;
  record.ability.max_wear_weight = 100;
  record.ability.max_hand_weight = 100;
  record.ability.reserved1 = 100;
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

mir2::LogicCommand attack(std::uint64_t session_id, std::uint64_t target_actor_id,
                          std::int32_t x = 10, std::int32_t y = 9) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::attack;
  command.session_id = session_id;
  command.target_actor_id = target_actor_id;
  command.x = x;
  command.y = y;
  command.dir = 0;
  command.game_message.ident = mir2::kCmHit;
  return command;
}

std::optional<mir2::DecodedLegacyGamePacket> find_packet(
    const mir2::RuntimeDispatch& dispatch, std::uint16_t ident,
    std::uint64_t session_id) {
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

const mir2::LegacyRuntimeTrace* find_trace(const mir2::RuntimeDispatch& dispatch,
                                           const std::string& action) {
  const auto it = std::find_if(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                               [&](const mir2::LegacyRuntimeTrace& trace) {
                                 return trace.action == action;
                               });
  return it != dispatch.legacy_traces.end() ? &*it : nullptr;
}

std::size_t action_index(const mir2::RuntimeDispatch& dispatch, const std::string& action) {
  for (std::size_t index = 0; index < dispatch.legacy_traces.size(); ++index) {
    if (dispatch.legacy_traces[index].action == action) {
      return index;
    }
  }
  return dispatch.legacy_traces.size();
}

mir2::HostConfig combat_config(std::uint32_t seed) {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = seed;
  mir2::MapConfig map{"0", "EquipmentSpecialMap", {}, 20, 20, 10, 10};
  map.allow_pk = true;
  map.fight_zone = true;
  config.maps.push_back(map);
  return config;
}

std::uint64_t enter_actor(mir2::LogicRuntime& runtime, std::uint64_t session_id,
                          mir2::CharacterRecord record) {
  static_cast<void>(runtime.route_logic_command(enter(session_id, std::move(record))));
  const auto dispatch = runtime.tick();
  const auto map = find_packet(dispatch, mir2::kSmNewMap, session_id);
  assert(map.has_value());
  return static_cast<std::uint64_t>(static_cast<std::uint32_t>(map->message.recog));
}

}  // namespace

int main() {
  {
    std::unordered_map<std::int32_t, mir2::ItemConfig> configs;
    auto sword = item(1, "Lucky Curse Sword", 5);
    sword.ac = mir2::make_word(9, 0);
    sword.mac = mir2::make_word(10, 0);
    sword.special_pwr = -5;
    configs.emplace(sword.id, sword);
    auto bracelet = item(2, "Anti Poison Bracelet", 23);
    bracelet.ac = mir2::make_word(0, 3);
    configs.emplace(bracelet.id, bracelet);

    auto hero = character("Derived");
    hero.equipped_items[mir2::kEquipWeapon] = user_item(1001, 1);
    hero.equipped_items[mir2::kEquipArmRingLeft] = user_item(1002, 2);
    mir2::Player player(1, 101, std::move(hero));
    player.refresh_derived_state(configs);
    assert(player.legacy_luck() == -1);
    assert(player.legacy_anti_poison() == 3);
    assert(player.legacy_undead_power() == 5);
  }

  {
    std::unordered_map<std::int32_t, mir2::ItemConfig> configs;
    auto ring = item(10, "Mana Health Ring", 22, mir2::kLegacyRingManaToHealthItem);
    ring.ani_count = 10;
    auto bracelet = item(11, "Mana Health Bracelet", 24,
                         mir2::kLegacyBraceletManaToHealthItem);
    bracelet.ani_count = 10;
    auto necklace = item(12, "Mana Health Necklace", 19,
                         mir2::kLegacyNecklaceManaToHealthItem);
    necklace.ani_count = 10;
    configs.emplace(ring.id, ring);
    configs.emplace(bracelet.id, bracelet);
    configs.emplace(necklace.id, necklace);

    auto hero = character("ManaToHealth");
    hero.ability.max_hp = 100;
    hero.ability.hp = 100;
    hero.ability.max_mp = 20;
    hero.ability.mp = 20;
    hero.equipped_items[mir2::kEquipRingLeft] = user_item(1010, 10);
    hero.equipped_items[mir2::kEquipArmRingLeft] = user_item(1011, 11);
    hero.equipped_items[mir2::kEquipNecklace] = user_item(1012, 12);
    mir2::Player player(1, 102, std::move(hero));
    player.refresh_derived_state(configs);
    assert(player.character().ability.max_mp == 1);
    assert(player.character().ability.max_hp == 119);
    assert(player.legacy_equipment_specials().mana_to_health == 19);
  }

  {
    std::unordered_map<std::int32_t, mir2::ItemConfig> configs;
    auto shield = item(20, "Magic Shield Ring", 22, mir2::kLegacyRingMagicShieldItem);
    configs.emplace(shield.id, shield);
    auto hero = character("Shield");
    hero.ability.hp = 100;
    hero.ability.max_hp = 100;
    hero.ability.mp = 30;
    hero.ability.max_mp = 30;
    hero.equipped_items[mir2::kEquipRingLeft] = user_item(1020, 20);
    mir2::Player player(1, 103, std::move(hero));
    player.refresh_derived_state(configs);
    auto damage = player.apply_damage(10, 1);
    assert(damage.hp_damage == 0);
    assert(damage.mp_damage == 15);
    assert(damage.absorbed_damage == 10);
    assert(player.character().ability.hp == 100);
    assert(player.character().ability.mp == 15);
    damage = player.apply_damage(20, 1);
    assert(damage.hp_damage == 10);
    assert(damage.mp_damage == 15);
    assert(damage.absorbed_damage == 10);
    assert(player.character().ability.hp == 90);
    assert(player.character().ability.mp == 0);
  }

  {
    std::unordered_map<std::int32_t, mir2::ItemConfig> configs;
    auto ring = item(30, "Suck Health Ring", 22, mir2::kLegacyRingSuckHealthItem);
    ring.ani_count = 20;
    configs.emplace(ring.id, ring);
    auto hero = character("Suck");
    hero.ability.hp = 50;
    hero.ability.max_hp = 100;
    hero.equipped_items[mir2::kEquipRingLeft] = user_item(1030, 30);
    mir2::Player player(1, 104, std::move(hero));
    player.refresh_derived_state(configs);
    assert(player.apply_legacy_suck_health(10) == 2);
    assert(player.character().ability.hp == 52);
  }

  {
    std::unordered_map<std::int32_t, mir2::ItemConfig> configs;
    auto ring = item(40, "Transparent Ring", 22, mir2::kLegacyRingTransparentItem);
    configs.emplace(ring.id, ring);
    auto hero = character("Invisible");
    hero.equipped_items[mir2::kEquipRingLeft] = user_item(1040, 40);
    mir2::Player player(1, 105, std::move(hero));
    player.refresh_derived_state(configs);
    assert((player.character().status & kTransparentStatusBit) != 0);
    assert(!player.clear_legacy_transparent(1));
    assert((player.character().status & kTransparentStatusBit) != 0);
    auto* equipped = player.equipped_item_mutable(mir2::kEquipRingLeft);
    assert(equipped != nullptr);
    equipped->dura = 0;
    player.refresh_derived_state(configs);
    assert((player.character().status & kTransparentStatusBit) == 0);
  }

  {
    auto run_luck_case = [](std::uint8_t luck, std::uint8_t unluck,
                            std::int32_t expected_damage) {
      auto config = combat_config(1);
      auto sword = item(50, "Luck Sword", 5);
      sword.ac = mir2::make_word(luck, 0);
      sword.mac = mir2::make_word(unluck, 0);
      config.items.push_back(sword);
      config.spawns.push_back(
          mir2::SpawnConfig{"0", "monster", "Dummy", 10, 9, 30000, 1, 200,
                            0, 0, 0, 20});

      mir2::LogicRuntime runtime(config);
      runtime.initialize();
      auto hero = character("LuckHero");
      hero.equipped_items[mir2::kEquipWeapon] = user_item(1050, 50);
      static_cast<void>(enter_actor(runtime, 201, std::move(hero)));
      static_cast<void>(runtime.route_logic_command(attack(201, 0)));
      const auto dispatch = runtime.tick();
      const auto damage = find_trace(dispatch, "damage");
      assert(damage != nullptr);
      assert(damage->damage == expected_damage);
      assert(action_index(dispatch, "attack_luck_gate") <
             action_index(dispatch, "hit_check"));
    };
    run_luck_case(9, 0, 20);
    run_luck_case(0, 9, 10);
  }

  {
    auto applied = false;
    for (std::uint32_t seed = 1; seed <= 64 && !applied; ++seed) {
      auto config = combat_config(seed);
      auto ring = item(60, "Make Stone Ring", 22, mir2::kLegacyRingMakeStoneItem);
      config.items.push_back(ring);
      config.spawns.push_back(
          mir2::SpawnConfig{"0", "monster", "StoneDummy", 10, 9, 30000, 1, 200,
                            0, 0, 0, 20});

      mir2::LogicRuntime runtime(config);
      runtime.initialize();
      auto hero = character("StoneHero");
      hero.equipped_items[mir2::kEquipRingLeft] = user_item(1060, 60);
      static_cast<void>(enter_actor(runtime, 301, std::move(hero)));
      static_cast<void>(runtime.route_logic_command(attack(301, 0)));
      const auto dispatch = runtime.tick();
      const auto trace = find_trace(dispatch, "make_stone");
      applied = trace != nullptr && trace->success;
    }
    assert(applied);
  }

  {
    auto config = combat_config(1);
    auto ring = item(70, "Revival Ring", 22, mir2::kLegacyRingRevivalItem);
    config.items.push_back(ring);
    mir2::LogicRuntime runtime(config);
    runtime.initialize();

    auto hero = character("Revived", 10, 10);
    hero.ability.hp = 20;
    hero.ability.max_hp = 20;
    hero.equipped_items[mir2::kEquipRingLeft] = user_item(1070, 70, 2000);
    const auto hero_id = enter_actor(runtime, 401, std::move(hero));
    auto rival = character("Killer", 10, 9);
    rival.ability.dc = mir2::make_word(50, 50);
    static_cast<void>(enter_actor(runtime, 402, std::move(rival)));
    static_cast<void>(runtime.tick(61000));

    static_cast<void>(runtime.route_logic_command(attack(402, hero_id, 10, 10)));
    auto dispatch = runtime.tick(62000);
    assert(!find_packet(dispatch, mir2::kSmNowDeath, 401).has_value());
    assert(!find_packet(dispatch, mir2::kSmDeath, 402).has_value());
    assert(find_packet(dispatch, mir2::kSmHealthSpellChanged, 401).has_value());
    auto snapshot = runtime.snapshot_character_actor("Revived");
    assert(snapshot.has_value());
    assert(snapshot->ability.hp == snapshot->ability.max_hp);
    assert(snapshot->equipped_items[mir2::kEquipRingLeft].dura == 1000);

    static_cast<void>(runtime.route_logic_command(attack(402, hero_id, 10, 10)));
    dispatch = runtime.tick(63000);
    assert(find_packet(dispatch, mir2::kSmNowDeath, 401).has_value());
    snapshot = runtime.snapshot_character_actor("Revived");
    assert(snapshot.has_value());
    assert(snapshot->ability.hp == 0);
    assert(snapshot->death_time_ms != 0);
  }

  {
    auto config = combat_config(1);
    auto ring = item(80, "Break Revival Ring", 22, mir2::kLegacyRingRevivalItem);
    config.items.push_back(ring);
    mir2::LogicRuntime runtime(config);
    runtime.initialize();

    auto hero = character("BreakRevive", 10, 10);
    hero.ability.hp = 20;
    hero.ability.max_hp = 20;
    hero.equipped_items[mir2::kEquipRingLeft] = user_item(1080, 80, 1000);
    const auto hero_id = enter_actor(runtime, 501, std::move(hero));
    auto rival = character("BreakKiller", 10, 9);
    rival.ability.dc = mir2::make_word(50, 50);
    static_cast<void>(enter_actor(runtime, 502, std::move(rival)));
    static_cast<void>(runtime.tick(61000));

    static_cast<void>(runtime.route_logic_command(attack(502, hero_id, 10, 10)));
    const auto dispatch = runtime.tick(62000);
    assert(find_packet(dispatch, mir2::kSmDelItem, 501).has_value());
    auto snapshot = runtime.snapshot_character_actor("BreakRevive");
    assert(snapshot.has_value());
    assert(snapshot->equipped_items[mir2::kEquipRingLeft].index == 0);
    assert(snapshot->ability.hp == snapshot->ability.max_hp);
  }

  return 0;
}
