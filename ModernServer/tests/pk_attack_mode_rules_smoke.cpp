#include <optional>
#include <memory>
#include <string>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

std::optional<mir2::DecodedLegacyGamePacket> find_packet(const mir2::RuntimeDispatch& dispatch,
                                                         std::uint16_t ident,
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

bool has_notice(const mir2::RuntimeDispatch& dispatch, std::uint64_t session_id,
                const std::string& needle) {
  const auto packet = find_packet(dispatch, mir2::kSmHear, session_id);
  return packet.has_value() && mir2::legacy_decode_string(packet->body).find(needle) != std::string::npos;
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

mir2::LogicCommand make_attack(std::uint64_t session_id, std::uint64_t target_actor_id) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::attack;
  command.session_id = session_id;
  command.dir = 0;
  command.x = 10;
  command.y = 9;
  command.target_actor_id = target_actor_id;
  command.game_message.ident = mir2::kCmHit;
  return command;
}

mir2::LogicCommand group_create(std::uint64_t session_id, std::string target_name) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::group_create;
  command.session_id = session_id;
  command.text = std::move(target_name);
  return command;
}

mir2::CharacterRecord make_player(std::string name, std::uint8_t attack_mode,
                                  std::uint8_t level = 20, std::int32_t pk_point = 0,
                                  std::string guild_name = {}) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = 10;
  character.y = character.character_name == "Hero" ? 10 : 9;
  character.ability.level = level;
  character.ability.dc = mir2::make_word(10, 10);
  character.ability.hp = 30;
  character.ability.max_hp = 30;
  character.ability.mp = 10;
  character.ability.max_mp = 10;
  character.ability.max_weight = 50;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  character.attack_mode = attack_mode;
  character.pk_point = pk_point;
  character.guild_name = std::move(guild_name);
  return character;
}

mir2::RuntimeDispatch advance(mir2::LogicRuntime& runtime, int ticks) {
  mir2::RuntimeDispatch last;
  for (int i = 0; i < ticks; ++i) {
    last = runtime.tick();
  }
  return last;
}

struct DuelState {
  std::unique_ptr<mir2::LogicRuntime> runtime;
  std::uint64_t rival_actor_id{0};
};

DuelState make_duel(mir2::MapConfig map, std::uint8_t hero_mode, std::uint8_t hero_level = 20,
                    std::int32_t rival_pk = 0, std::uint8_t rival_level = 20,
                    std::string hero_guild = {}, std::string rival_guild = {}) {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 1;
  config.maps.push_back(std::move(map));
  DuelState state{std::make_unique<mir2::LogicRuntime>(config), 0};
  state.runtime->initialize();
  static_cast<void>(
      state.runtime->route_logic_command(make_enter(
          301, make_player("Hero", hero_mode, hero_level, 0, std::move(hero_guild)))));
  static_cast<void>(state.runtime->tick());
  static_cast<void>(state.runtime->route_logic_command(
      make_enter(302, make_player("Rival", 0, rival_level, rival_pk, std::move(rival_guild)))));
  const auto rival_login = state.runtime->tick();
  const auto rival_map = find_packet(rival_login, mir2::kSmNewMap, 302);
  if (rival_map.has_value()) {
    state.rival_actor_id =
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(rival_map->message.recog));
  }
  return state;
}

mir2::MapConfig pk_map() {
  mir2::MapConfig map{"0", "PkMap", {}, 20, 20, 10, 10};
  map.allow_pk = true;
  return map;
}

mir2::MapConfig fight_pk_map() {
  auto map = pk_map();
  map.fight_zone = true;
  return map;
}

}  // namespace

int main() {
  auto fail = [](int stage) { return stage; };

  {
    auto state = make_duel(pk_map(), 0);
    static_cast<void>(state.runtime->route_logic_command(make_attack(301, state.rival_actor_id)));
    const auto blocked = state.runtime->tick();
    if (!has_notice(blocked, 301, "Map change") ||
        find_packet(blocked, mir2::kSmStruck, 302).has_value()) {
      return fail(1);
    }
  }

  {
    auto state = make_duel(fight_pk_map(), 1);
    static_cast<void>(advance(*state.runtime, 160));
    static_cast<void>(state.runtime->route_logic_command(make_attack(301, state.rival_actor_id)));
    const auto blocked = state.runtime->tick();
    if (!has_notice(blocked, 301, "Peace") ||
        find_packet(blocked, mir2::kSmStruck, 302).has_value()) {
      return fail(2);
    }
  }

  {
    auto state = make_duel(fight_pk_map(), 0);
    static_cast<void>(advance(*state.runtime, 160));
    static_cast<void>(state.runtime->route_logic_command(make_attack(301, state.rival_actor_id)));
    const auto struck = state.runtime->tick();
    if (!find_packet(struck, mir2::kSmStruck, 302).has_value()) {
      return fail(3);
    }
  }

  {
    auto state = make_duel(fight_pk_map(), 4, 20, 0);
    static_cast<void>(advance(*state.runtime, 160));
    static_cast<void>(state.runtime->route_logic_command(make_attack(301, state.rival_actor_id)));
    const auto blocked = state.runtime->tick();
    if (!has_notice(blocked, 301, "Red-name") ||
        find_packet(blocked, mir2::kSmStruck, 302).has_value()) {
      return fail(4);
    }
  }

  {
    auto state = make_duel(fight_pk_map(), 4, 20, 200);
    static_cast<void>(advance(*state.runtime, 160));
    static_cast<void>(state.runtime->route_logic_command(make_attack(301, state.rival_actor_id)));
    const auto struck = state.runtime->tick();
    if (!find_packet(struck, mir2::kSmStruck, 302).has_value()) {
      return fail(5);
    }
  }

  {
    auto state = make_duel(fight_pk_map(), 2);
    static_cast<void>(state.runtime->route_logic_command(group_create(301, "Rival")));
    static_cast<void>(advance(*state.runtime, 161));
    static_cast<void>(state.runtime->route_logic_command(make_attack(301, state.rival_actor_id)));
    const auto blocked = state.runtime->tick();
    if (!has_notice(blocked, 301, "Group") ||
        find_packet(blocked, mir2::kSmStruck, 302).has_value()) {
      return fail(6);
    }
  }

  {
    auto state = make_duel(fight_pk_map(), 3, 20, 0, 20, "Warriors", "warriors");
    static_cast<void>(advance(*state.runtime, 160));
    static_cast<void>(state.runtime->route_logic_command(make_attack(301, state.rival_actor_id)));
    const auto blocked = state.runtime->tick();
    if (!has_notice(blocked, 301, "Guild") ||
        find_packet(blocked, mir2::kSmStruck, 302).has_value()) {
      return fail(7);
    }
  }

  {
    auto map = pk_map();
    map.allow_pk = false;
    auto state = make_duel(map, 0);
    static_cast<void>(advance(*state.runtime, 160));
    static_cast<void>(state.runtime->route_logic_command(make_attack(301, state.rival_actor_id)));
    const auto blocked = state.runtime->tick();
    if (!has_notice(blocked, 301, "forbids PK")) {
      return fail(8);
    }
  }

  {
    auto map = pk_map();
    map.safe_zones.push_back(mir2::MapZoneConfig{9, 9, 3, 3});
    auto state = make_duel(map, 0);
    static_cast<void>(advance(*state.runtime, 160));
    static_cast<void>(state.runtime->route_logic_command(make_attack(301, state.rival_actor_id)));
    const auto blocked = state.runtime->tick();
    if (!has_notice(blocked, 301, "Safe zone")) {
      return fail(9);
    }
  }

  {
    auto state = make_duel(pk_map(), 0, 9, 0, 9);
    static_cast<void>(advance(*state.runtime, 160));
    static_cast<void>(state.runtime->route_logic_command(make_attack(301, state.rival_actor_id)));
    const auto blocked = state.runtime->tick();
    if (!has_notice(blocked, 301, "Newbie")) {
      return fail(10);
    }
  }

  return 0;
}
