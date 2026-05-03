#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

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

std::size_t count_packet(const mir2::RuntimeDispatch& dispatch, std::uint64_t session_id,
                         std::uint16_t ident) {
  std::size_t count = 0;
  for (const auto& event : dispatch.session_events) {
    if (event.session_id != session_id) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      ++count;
    }
  }
  return count;
}

mir2::CharacterRecord make_character(std::string name, int x, int y) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = x;
  character.y = y;
  character.ability.hp = 15;
  character.ability.max_hp = 15;
  return character;
}

void enter(mir2::LogicRuntime& runtime, std::uint64_t session_id,
           const mir2::CharacterRecord& character) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = session_id;
  command.account_id = character.account_id;
  command.character_name = character.character_name;
  command.map_id = character.map_id;
  command.x = character.x;
  command.y = character.y;
  command.character = character;
  static_cast<void>(runtime.route_logic_command(command));
  static_cast<void>(runtime.tick());
}

void advance(mir2::LogicRuntime& runtime, int ticks = 12) {
  for (int index = 0; index < ticks; ++index) {
    static_cast<void>(runtime.tick());
  }
}

mir2::RuntimeDispatch walk(mir2::LogicRuntime& runtime, std::uint64_t session_id, int x, int y) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::walk;
  command.session_id = session_id;
  command.x = x;
  command.y = y;
  static_cast<void>(runtime.route_logic_command(command));
  return runtime.tick();
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.budgets.tick_ms = 20;
  config.maps.push_back(mir2::MapConfig{"0", "ViewMap", {}, 100, 100, 10, 10});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  enter(runtime, 1, make_character("Watcher", 10, 10));
  enter(runtime, 2, make_character("Mover", 23, 10));

  advance(runtime);
  auto dispatch = walk(runtime, 2, 22, 10);
  assert(find_packet(dispatch, 1, mir2::kSmTurn).has_value());

  advance(runtime);
  dispatch = walk(runtime, 2, 23, 10);
  assert(find_packet(dispatch, 1, mir2::kSmDisappear).has_value());

  mir2::HostConfig mover_config;
  mover_config.budgets.tick_ms = 20;
  mover_config.maps.push_back(mir2::MapConfig{"0", "MoverViewMap", {}, 100, 100, 10, 10});
  mir2::LogicRuntime mover_runtime(mover_config);
  mover_runtime.initialize();
  enter(mover_runtime, 11, make_character("MoverSelf", 10, 20));
  enter(mover_runtime, 12, make_character("Seen", 23, 20));

  advance(mover_runtime);
  auto self_dispatch = walk(mover_runtime, 11, 11, 20);
  assert(find_packet(self_dispatch, 11, mir2::kSmTurn).has_value());
  assert(count_packet(self_dispatch, 12, mir2::kSmTurn) > 0);

  advance(mover_runtime);
  self_dispatch = walk(mover_runtime, 11, 10, 20);
  assert(find_packet(self_dispatch, 11, mir2::kSmDisappear).has_value());
  assert(find_packet(self_dispatch, 12, mir2::kSmDisappear).has_value());

  return 0;
}
