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

  return 0;
}
