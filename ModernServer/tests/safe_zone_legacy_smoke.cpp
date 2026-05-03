#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
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

mir2::CharacterRecord make_character(std::string name, int x, int y) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = x;
  character.y = y;
  character.ability.hp = 30;
  character.ability.max_hp = 30;
  character.ability.dc = mir2::make_word(0, 8);
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
}

}  // namespace

int main() {
  mir2::HostConfig config;
  mir2::MapConfig map;
  map.id = "0";
  map.title = "SafeMap";
  map.width = 100;
  map.height = 100;
  map.allow_pk = true;
  map.safe_zones.push_back(mir2::MapZoneConfig{20, 20, 21, 21});
  config.maps.push_back(map);

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  enter(runtime, 1, make_character("Hero", 20, 20));
  auto dispatch = runtime.tick();
  auto area = find_packet(dispatch, 1, mir2::kSmAreaState);
  assert(area.has_value() && (area->message.recog & 1) != 0);

  mir2::LogicCommand walk;
  walk.kind = mir2::LogicCommandKind::walk;
  walk.session_id = 1;
  walk.x = 19;
  walk.y = 20;
  for (int index = 0; index < 12; ++index) {
    static_cast<void>(runtime.tick());
  }
  static_cast<void>(runtime.route_logic_command(walk));
  dispatch = runtime.tick();
  area = find_packet(dispatch, 1, mir2::kSmAreaState);
  assert(area.has_value() && (area->message.recog & 1) == 0);

  mir2::HostConfig full_safe_config;
  mir2::MapConfig full_safe;
  full_safe.id = "0";
  full_safe.title = "FullSafe";
  full_safe.width = 100;
  full_safe.height = 100;
  full_safe.law_full = true;
  full_safe.allow_pk = false;
  full_safe_config.maps.push_back(full_safe);
  mir2::LogicRuntime full_safe_runtime(full_safe_config);
  full_safe_runtime.initialize();
  enter(full_safe_runtime, 2, make_character("Lawful", 90, 90));
  const auto full_safe_dispatch = full_safe_runtime.tick();
  area = find_packet(full_safe_dispatch, 2, mir2::kSmAreaState);
  assert(area.has_value() && (area->message.recog & 1) != 0);
  return 0;
}
