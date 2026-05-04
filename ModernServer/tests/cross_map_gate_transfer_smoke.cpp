#include <cassert>
#include <cstdint>
#include <optional>
#include <string>

#include "protocol/legacy_edcode.hpp"
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

mir2::CharacterRecord make_character() {
  mir2::CharacterRecord character;
  character.account_id = "acct";
  character.character_name = "Hero";
  character.map_id = "0";
  character.x = 10;
  character.y = 10;
  character.ability.hp = 15;
  character.ability.max_hp = 15;
  return character;
}

void advance(mir2::LogicRuntime& runtime, int ticks = 12) {
  for (int index = 0; index < ticks; ++index) {
    static_cast<void>(runtime.tick());
  }
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.budgets.tick_ms = 20;
  mir2::MapConfig source;
  source.id = "0";
  source.title = "Source";
  source.width = 20;
  source.height = 20;
  source.home_x = 10;
  source.home_y = 10;
  source.gates.push_back(mir2::MapGateConfig{11, 10, "1", 5, 5, false});
  config.maps.push_back(source);
  config.maps.push_back(mir2::MapConfig{"1", "Target", {}, 20, 20, 5, 5});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  mir2::LogicCommand enter;
  enter.kind = mir2::LogicCommandKind::enter_world;
  enter.session_id = 1;
  enter.account_id = "acct";
  enter.character_name = "Hero";
  enter.map_id = "0";
  enter.x = 10;
  enter.y = 10;
  enter.character = make_character();
  static_cast<void>(runtime.route_logic_command(enter));
  static_cast<void>(runtime.tick());
  advance(runtime);

  mir2::LogicCommand walk;
  walk.kind = mir2::LogicCommandKind::walk;
  walk.session_id = 1;
  walk.x = 11;
  walk.y = 10;
  static_cast<void>(runtime.route_logic_command(walk));
  const auto transfer_dispatch = runtime.tick();
  assert(find_packet(transfer_dispatch, 1, mir2::kSmClearObjects).has_value());
  assert(find_packet(transfer_dispatch, 1, mir2::kSmChangeMap).has_value());
  const auto new_map = find_packet(transfer_dispatch, 1, mir2::kSmNewMap);
  assert(new_map.has_value());
  assert(mir2::legacy_decode_string(new_map->body) == "1");

  const auto snapshot = runtime.snapshot_character_actor("Hero");
  assert(snapshot.has_value());
  assert(snapshot->map_id == "1");
  assert(snapshot->x == 5 && snapshot->y == 5);
  return 0;
}
