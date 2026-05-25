#include <optional>
#include <string>
#include <vector>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

std::vector<mir2::DecodedLegacyGamePacket> find_packets(const mir2::RuntimeDispatch& dispatch,
                                                        std::uint16_t ident,
                                                        std::uint64_t session_id) {
  std::vector<mir2::DecodedLegacyGamePacket> packets;
  for (const auto& event : dispatch.session_events) {
    if (event.session_id != session_id) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      packets.push_back(*decoded);
    }
  }
  return packets;
}

std::optional<mir2::DecodedLegacyGamePacket> find_packet(const mir2::RuntimeDispatch& dispatch,
                                                         std::uint16_t ident,
                                                         std::uint64_t session_id) {
  const auto packets = find_packets(dispatch, ident, session_id);
  if (packets.empty()) {
    return std::nullopt;
  }
  return packets.front();
}

bool has_packet(const mir2::RuntimeDispatch& dispatch, std::uint16_t ident,
                std::uint64_t session_id) {
  return find_packet(dispatch, ident, session_id).has_value();
}

template <typename T>
std::optional<T> decode_body(std::string_view body) {
  T value{};
  if (!mir2::legacy_decode_buffer(body, &value, sizeof(value))) {
    return std::nullopt;
  }
  return value;
}

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

mir2::LogicCommand make_attack(std::uint64_t session_id, std::uint8_t dir, std::int32_t x,
                               std::int32_t y, std::uint64_t target_actor_id = 0) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::attack;
  command.session_id = session_id;
  command.dir = dir;
  command.x = x;
  command.y = y;
  command.target_actor_id = target_actor_id;
  command.game_message.ident = mir2::kCmHit;
  return command;
}

mir2::LogicCommand make_spell(std::uint64_t session_id, std::uint64_t target_actor_id,
                              std::int32_t x, std::int32_t y, std::int32_t magic_id) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::spell;
  command.session_id = session_id;
  command.target_actor_id = target_actor_id;
  command.x = x;
  command.y = y;
  command.game_message.ident = mir2::kCmSpell;
  command.game_message.tag = static_cast<std::uint16_t>(magic_id);
  return command;
}

mir2::CharacterRecord make_player(std::string name, std::int32_t x, std::int32_t y) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = x;
  character.y = y;
  character.ability.level = 20;
  character.ability.hp = 40;
  character.ability.max_hp = 40;
  character.ability.mp = 20;
  character.ability.max_mp = 20;
  character.ability.max_weight = 50;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  character.attack_mode = 0;
  return character;
}

}  // namespace

int main() {
  auto fail = [](int stage) { return stage; };

  {
    mir2::HostConfig config;
    config.runtime.legacy_random_seed = 1;
    config.maps.push_back(mir2::MapConfig{"0", "FormulaMap", {}, 20, 20, 10, 10});
    config.spawns.push_back(
        mir2::SpawnConfig{"0", "monster", "Armored", 10, 9, 30000, 1, 12, 0, 3, 0, 20});

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    auto hero = make_player("Hero", 10, 10);
    hero.ability.dc = mir2::make_word(8, 8);
    static_cast<void>(runtime.route_logic_command(make_enter(101, hero)));
    static_cast<void>(runtime.tick());

    static_cast<void>(runtime.route_logic_command(make_attack(101, 0, 10, 9, 1)));
    const auto dispatch = runtime.tick();
    const auto struck = find_packet(dispatch, mir2::kSmStruck, 101);
    if (!struck.has_value() || struck->message.recog != 1 || struck->message.param != 7 ||
        struck->message.tag != 12 || struck->message.series != 5) {
      return fail(1);
    }
  }

  {
    mir2::HostConfig config;
    config.runtime.legacy_random_seed = 1;
    config.maps.push_back(mir2::MapConfig{"0", "UndeadMap", {}, 20, 20, 10, 10});
    config.spawns.push_back(
        mir2::SpawnConfig{"0", "monster", "Undead", 10, 9, 30000, 1, 20, 0, 0, 0, 20, 1});

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    auto hero = make_player("Slayer", 10, 10);
    hero.ability.dc = mir2::make_word(8, 8);
    static_cast<void>(runtime.route_logic_command(make_enter(102, hero)));
    static_cast<void>(runtime.tick());

    static_cast<void>(runtime.route_logic_command(make_attack(102, 0, 10, 9, 1)));
    const auto dispatch = runtime.tick();
    const auto struck = find_packet(dispatch, mir2::kSmStruck, 102);
    if (!struck.has_value() || struck->message.param != 12 || struck->message.tag != 20 ||
        struck->message.series != 8) {
      return fail(2);
    }
  }

  {
    mir2::HostConfig config;
    config.runtime.legacy_random_seed = 1;
    config.maps.push_back(mir2::MapConfig{"0", "UndeadPowerMap", {}, 20, 20, 10, 10});
    config.spawns.push_back(
        mir2::SpawnConfig{"0", "monster", "Undead", 10, 9, 30000, 1, 20, 0, 0, 0, 20, 1});
    mir2::ItemConfig slayer{3, "Undead Slayer", 1, 100, 5, 0, 10, 1000, 1, 0, 0};
    slayer.undead = 4;
    config.items.push_back(slayer);

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    auto hero = make_player("SlayerWithPower", 10, 10);
    hero.ability.dc = mir2::make_word(8, 8);
    hero.equipped_items[mir2::kEquipWeapon] = mir2::LegacyUserItem{3001, 3, 1000, 1000};
    static_cast<void>(runtime.route_logic_command(make_enter(106, hero)));
    static_cast<void>(runtime.tick());

    static_cast<void>(runtime.route_logic_command(make_attack(106, 0, 10, 9, 1)));
    const auto dispatch = runtime.tick();
    const auto struck = find_packet(dispatch, mir2::kSmStruck, 106);
    if (!struck.has_value() || struck->message.param != 8 || struck->message.tag != 20 ||
        struck->message.series != 12) {
      return fail(7);
    }
  }

  {
    mir2::HostConfig config;
    config.runtime.legacy_random_seed = 1;
    config.maps.push_back(mir2::MapConfig{"0", "MagicFormulaMap", {}, 20, 20, 10, 10});
    config.spawns.push_back(
        mir2::SpawnConfig{"0", "monster", "MagicDummy", 10, 9, 30000, 1, 10, 0, 0, 2, 20});
    config.magics.push_back(mir2::MagicConfig{1, "Fireball", 4, 6});

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    auto hero = make_player("Mage", 10, 10);
    hero.ability.mc = mir2::make_word(2, 2);
    hero.magics[0].magic_id = 1;
    static_cast<void>(runtime.route_logic_command(make_enter(103, hero)));
    static_cast<void>(runtime.tick());

    static_cast<void>(runtime.route_logic_command(make_spell(103, 1, 10, 9, 1)));
    const auto dispatch = runtime.tick();
    const auto struck = find_packet(dispatch, mir2::kSmStruck, 103);
    if (!struck.has_value() || struck->message.param != 4 || struck->message.tag != 10 ||
        struck->message.series != 6) {
      return fail(3);
    }
  }

  {
    mir2::HostConfig config;
    config.runtime.legacy_random_seed = 1;
    mir2::MapConfig map{"0", "DuraMap", {}, 20, 20, 10, 10};
    map.allow_pk = true;
    map.fight_zone = true;
    config.maps.push_back(map);
    mir2::ItemConfig armor{2, "Training Armor", 1, 10, 10, 0, 0, 1000, 0, 0, 0};
    config.items.push_back(armor);

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    auto attacker = make_player("Attacker", 10, 10);
    attacker.ability.dc = mir2::make_word(10, 10);
    auto target = make_player("Target", 10, 9);
    target.equipped_items[0].index = 2;
    target.equipped_items[0].make_index = 2001;
    target.equipped_items[0].dura = 500;
    target.equipped_items[0].dura_max = 1000;
    static_cast<void>(runtime.route_logic_command(make_enter(104, attacker)));
    const auto attacker_login = runtime.tick();
    static_cast<void>(runtime.route_logic_command(make_enter(105, target)));
    const auto target_login = runtime.tick();
    const auto target_map = find_packet(target_login, mir2::kSmNewMap, 105);
    if (!find_packet(attacker_login, mir2::kSmNewMap, 104).has_value() ||
        !target_map.has_value()) {
      return fail(4);
    }
    const auto target_actor_id =
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(target_map->message.recog));

    static_cast<void>(runtime.route_logic_command(make_attack(104, 0, 10, 9, target_actor_id)));
    const auto dispatch = runtime.tick();
    const auto update = find_packet(dispatch, mir2::kSmUpdateItem, 105);
    const auto dura = find_packet(dispatch, mir2::kSmDuraChange, 105);
    if (!update.has_value() || !dura.has_value()) {
      return fail(5);
    }
    const auto item = decode_body<mir2::LegacyClientItem>(update->body);
    if (!item.has_value() || item->make_index != 2001 || item->dura >= 500 ||
        dura->message.param != 0 || dura->message.tag != 1000) {
      return fail(6);
    }
  }

  {
    mir2::HostConfig config;
    config.runtime.legacy_random_seed = 2;
    mir2::MapConfig map{"0", "SlotDuraMap", {}, 20, 20, 10, 10};
    map.allow_pk = true;
    map.fight_zone = true;
    config.maps.push_back(map);
    config.items.push_back(mir2::ItemConfig{3, "Training Necklace", 1, 10, 19, 0, 0, 1000, 0, 0, 0});

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    auto attacker = make_player("SlotAttacker", 10, 10);
    attacker.ability.dc = mir2::make_word(10, 10);
    auto target = make_player("SlotTarget", 10, 9);
    target.equipped_items[mir2::kEquipNecklace].index = 3;
    target.equipped_items[mir2::kEquipNecklace].make_index = 3001;
    target.equipped_items[mir2::kEquipNecklace].dura = 500;
    target.equipped_items[mir2::kEquipNecklace].dura_max = 1000;
    static_cast<void>(runtime.route_logic_command(make_enter(106, attacker)));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_enter(107, target)));
    const auto target_login = runtime.tick();
    const auto target_map = find_packet(target_login, mir2::kSmNewMap, 107);
    if (!target_map.has_value()) {
      return fail(7);
    }
    const auto target_actor_id =
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(target_map->message.recog));

    static_cast<void>(runtime.route_logic_command(make_attack(106, 0, 10, 9, target_actor_id)));
    const auto dispatch = runtime.tick();
    const auto update = find_packet(dispatch, mir2::kSmUpdateItem, 107);
    if (!update.has_value()) {
      return fail(8);
    }
    const auto item = decode_body<mir2::LegacyClientItem>(update->body);
    if (!item.has_value() || item->make_index != 3001 || item->dura >= 500) {
      return fail(9);
    }
  }

  {
    mir2::HostConfig config;
    config.runtime.legacy_random_seed = 2;
    mir2::MapConfig map{"0", "BujukSkipMap", {}, 20, 20, 10, 10};
    map.allow_pk = true;
    map.fight_zone = true;
    config.maps.push_back(map);
    config.items.push_back(mir2::ItemConfig{4, "Training Bujuk", 1, 10, 25, 0, 0, 1000, 0, 0, 0});

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    auto attacker = make_player("BujukAttacker", 10, 10);
    attacker.ability.dc = mir2::make_word(10, 10);
    auto target = make_player("BujukTarget", 10, 9);
    target.equipped_items[mir2::kEquipBujuk].index = 4;
    target.equipped_items[mir2::kEquipBujuk].make_index = 4001;
    target.equipped_items[mir2::kEquipBujuk].dura = 500;
    target.equipped_items[mir2::kEquipBujuk].dura_max = 1000;
    static_cast<void>(runtime.route_logic_command(make_enter(108, attacker)));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_enter(109, target)));
    const auto target_login = runtime.tick();
    const auto target_map = find_packet(target_login, mir2::kSmNewMap, 109);
    if (!target_map.has_value()) {
      return fail(10);
    }
    const auto target_actor_id =
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(target_map->message.recog));

    static_cast<void>(runtime.route_logic_command(make_attack(108, 0, 10, 9, target_actor_id)));
    const auto dispatch = runtime.tick();
    if (has_packet(dispatch, mir2::kSmUpdateItem, 109) ||
        has_packet(dispatch, mir2::kSmDuraChange, 109)) {
      return fail(11);
    }
  }

  {
    mir2::HostConfig config;
    config.runtime.legacy_random_seed = 1;
    mir2::MapConfig map{"0", "DuraOrderMap", {}, 20, 20, 10, 10};
    map.allow_pk = true;
    map.fight_zone = true;
    config.maps.push_back(map);
    config.items.push_back(mir2::ItemConfig{5, "Training Sword", 1, 100, 5, 0, 10, 1000, 1, 0, 0});
    config.items.push_back(mir2::ItemConfig{6, "Training Armor", 1, 10, 10, 0, 0, 1000, 0, 0, 0});

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    auto attacker = make_player("OrderAttacker", 10, 10);
    attacker.ability.dc = mir2::make_word(10, 10);
    attacker.equipped_items[mir2::kEquipWeapon] = mir2::LegacyUserItem{5001, 5, 1000, 1000};
    auto target = make_player("OrderTarget", 10, 9);
    target.equipped_items[mir2::kEquipDress] = mir2::LegacyUserItem{6001, 6, 500, 1000};
    static_cast<void>(runtime.route_logic_command(make_enter(110, attacker)));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_enter(111, target)));
    const auto target_login = runtime.tick();
    const auto target_map = find_packet(target_login, mir2::kSmNewMap, 111);
    if (!target_map.has_value()) {
      return fail(12);
    }
    const auto target_actor_id =
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(target_map->message.recog));

    static_cast<void>(runtime.route_logic_command(make_attack(110, 0, 10, 9, target_actor_id)));
    const auto dispatch = runtime.tick();
    std::optional<std::size_t> weapon_trace;
    std::optional<std::size_t> struck_trace;
    for (std::size_t i = 0; i < dispatch.legacy_traces.size(); ++i) {
      if (dispatch.legacy_traces[i].action == "weapon_dura_damage") {
        weapon_trace = i;
      } else if (dispatch.legacy_traces[i].action == "struck_dura_damage") {
        struck_trace = i;
      }
    }
    if (!weapon_trace.has_value() || !struck_trace.has_value() ||
        *struck_trace >= *weapon_trace) {
      return fail(13);
    }
  }

  {
    mir2::HostConfig config;
    config.runtime.legacy_random_seed = 1;
    config.maps.push_back(mir2::MapConfig{"0", "WeaponDuraMap", {}, 20, 20, 10, 10});
    config.spawns.push_back(
        mir2::SpawnConfig{"0", "monster", "Dummy", 10, 9, 30000, 1, 30, 0, 0, 0, 20});
    config.items.push_back(mir2::ItemConfig{4, "Training Sword", 1, 100, 5, 0, 10, 1000, 1, 0, 0});

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    auto hero = make_player("WeaponWear", 10, 10);
    hero.ability.dc = mir2::make_word(10, 10);
    hero.equipped_items[mir2::kEquipWeapon] = mir2::LegacyUserItem{4001, 4, 1000, 1000};
    static_cast<void>(runtime.route_logic_command(make_enter(107, hero)));
    static_cast<void>(runtime.tick());

    static_cast<void>(runtime.route_logic_command(make_attack(107, 0, 10, 9, 1)));
    const auto dispatch = runtime.tick();
    const auto update = find_packet(dispatch, mir2::kSmUpdateItem, 107);
    if (!update.has_value() || has_packet(dispatch, mir2::kSmDuraChange, 107)) {
      return fail(14);
    }
    const auto item = decode_body<mir2::LegacyClientItem>(update->body);
    if (!item.has_value() || item->make_index != 4001 || item->dura >= 1000 ||
        item->dura < 994) {
      return fail(15);
    }
  }

  {
    mir2::HostConfig config;
    config.runtime.legacy_random_seed = 1;
    config.maps.push_back(mir2::MapConfig{"0", "StrongWeaponMap", {}, 20, 20, 10, 10});
    config.spawns.push_back(
        mir2::SpawnConfig{"0", "monster", "Dummy", 10, 9, 30000, 1, 30, 0, 0, 0, 20});
    mir2::ItemConfig sword{5, "Strong Sword", 1, 100, 5, 0, 10, 1000, 1, 0, 0};
    sword.special_pwr = 10;
    config.items.push_back(sword);

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    auto hero = make_player("StrongWeapon", 10, 10);
    hero.ability.dc = mir2::make_word(10, 10);
    hero.equipped_items[mir2::kEquipWeapon] = mir2::LegacyUserItem{5001, 5, 1000, 1000};
    static_cast<void>(runtime.route_logic_command(make_enter(108, hero)));
    static_cast<void>(runtime.tick());

    static_cast<void>(runtime.route_logic_command(make_attack(108, 0, 10, 9, 1)));
    const auto dispatch = runtime.tick();
    if (has_packet(dispatch, mir2::kSmUpdateItem, 108) ||
        has_packet(dispatch, mir2::kSmDuraChange, 108)) {
      return fail(16);
    }
  }

  {
    mir2::HostConfig config;
    config.runtime.legacy_random_seed = 1;
    config.maps.push_back(mir2::MapConfig{"0", "WeaponBreakMap", {}, 20, 20, 10, 10});
    config.spawns.push_back(
        mir2::SpawnConfig{"0", "monster", "Dummy", 10, 9, 30000, 1, 30, 0, 0, 0, 20});
    config.items.push_back(mir2::ItemConfig{6, "Fragile Sword", 1, 100, 5, 0, 10, 1000, 1, 0, 0});

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    auto hero = make_player("Breaker", 10, 10);
    hero.ability.dc = mir2::make_word(10, 10);
    hero.equipped_items[mir2::kEquipWeapon] = mir2::LegacyUserItem{6001, 6, 2, 1000};
    static_cast<void>(runtime.route_logic_command(make_enter(109, hero)));
    static_cast<void>(runtime.tick());

    static_cast<void>(runtime.route_logic_command(make_attack(109, 0, 10, 9, 1)));
    const auto dispatch = runtime.tick();
    const auto update = find_packet(dispatch, mir2::kSmUpdateItem, 109);
    const auto dura = find_packet(dispatch, mir2::kSmDuraChange, 109);
    if (!update.has_value() || !dura.has_value() ||
        !has_packet(dispatch, mir2::kSmAbility, 109) ||
        !has_packet(dispatch, mir2::kSmSubAbility, 109) ||
        has_packet(dispatch, mir2::kSmFeatureChanged, 109)) {
      return fail(17);
    }
    const auto item = decode_body<mir2::LegacyClientItem>(update->body);
    if (!item.has_value() || item->make_index != 6001 || item->dura != 0 ||
        dura->message.recog != 0 || dura->message.param != mir2::kEquipWeapon ||
        dura->message.tag != 1000 || dura->message.series != 0) {
      return fail(18);
    }
  }

  return 0;
}
