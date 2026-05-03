#include <optional>
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

mir2::LogicCommand make_attack(std::uint64_t session_id) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::attack;
  command.session_id = session_id;
  command.dir = 0;
  command.x = 10;
  command.y = 9;
  command.target_actor_id = 1;
  command.game_message.ident = mir2::kCmHit;
  return command;
}

mir2::CharacterRecord make_player(std::string name, std::uint8_t level) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = 10;
  character.y = 10;
  character.ability.level = level;
  character.ability.dc = mir2::make_word(20, 20);
  character.ability.hp = 10;
  character.ability.max_hp = 10;
  character.ability.mp = 5;
  character.ability.max_mp = 5;
  character.ability.max_exp = 100;
  character.ability.max_weight = 50;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  return character;
}

}  // namespace

int main() {
  auto fail = [](int stage) { return stage; };

  {
    mir2::HostConfig config;
    config.runtime.legacy_random_seed = 1;
    config.maps.push_back(mir2::MapConfig{"0", "ExpMap", {}, 20, 20, 10, 10});
    config.spawns.push_back(
        mir2::SpawnConfig{"0", "monster", "RichDummy", 10, 9, 30000, 1, 1, 0, 0, 0, 70000});

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(make_enter(501, make_player("Hero", 1))));
    static_cast<void>(runtime.tick());

    static_cast<void>(runtime.route_logic_command(make_attack(501)));
    const auto dispatch = runtime.tick();
    const auto win_exp = find_packet(dispatch, mir2::kSmWinExp, 501);
    const auto level_up = find_packet(dispatch, mir2::kSmLevelUp, 501);
    const auto ability_packet = find_packet(dispatch, mir2::kSmAbility, 501);
    const auto hpmp = find_packet(dispatch, mir2::kSmHealthSpellChanged, 501);
    if (!win_exp.has_value() || !level_up.has_value() || !ability_packet.has_value() ||
        !hpmp.has_value()) {
      return fail(1);
    }
    const auto ability = decode_body<mir2::LegacyAbility>(ability_packet->body);
    if (!ability.has_value()) {
      return fail(2);
    }
    if (win_exp->message.recog != 60000 || win_exp->message.param != 60000 ||
        level_up->message.param != 2 || ability->level != 2 || ability->exp != 59900 ||
        ability->max_exp != 200 || ability->hp != 15 || ability->max_hp != 15 ||
        ability->mp != 8 || ability->max_mp != 8 || hpmp->message.param != 15 ||
        hpmp->message.tag != 8) {
      return fail(3);
    }
  }

  {
    mir2::HostConfig config;
    config.runtime.legacy_random_seed = 1;
    config.maps.push_back(mir2::MapConfig{"0", "LowExpMap", {}, 20, 20, 10, 10});
    config.spawns.push_back(
        mir2::SpawnConfig{"0", "monster", "LowDummy", 10, 9, 30000, 1, 1, 0, 0, 0, 15});

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(make_enter(502, make_player("Veteran", 20))));
    static_cast<void>(runtime.tick());

    static_cast<void>(runtime.route_logic_command(make_attack(502)));
    const auto dispatch = runtime.tick();
    const auto win_exp = find_packet(dispatch, mir2::kSmWinExp, 502);
    if (!win_exp.has_value() || win_exp->message.param != 6 ||
        find_packet(dispatch, mir2::kSmLevelUp, 502).has_value()) {
      return fail(4);
    }
  }

  return 0;
}
