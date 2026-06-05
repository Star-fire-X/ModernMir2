#include <cassert>
#include <filesystem>
#include <utility>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

bool has_new_map(const mir2::RuntimeDispatch& dispatch) {
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == mir2::kSmNewMap) {
      return true;
    }
  }
  return false;
}

bool can_enter(mir2::MapConfig map) {
  mir2::HostConfig config;
  config.maps.push_back(std::move(map));
  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  mir2::CharacterRecord character;
  character.account_id = "acct";
  character.character_name = "Hero";
  character.map_id = "0";
  character.x = 0;
  character.y = 0;
  character.ability.hp = 20;
  character.ability.max_hp = 20;

  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = 1;
  command.account_id = character.account_id;
  command.character_name = character.character_name;
  command.map_id = character.map_id;
  command.x = character.x;
  command.y = character.y;
  command.character = character;
  static_cast<void>(runtime.route_logic_command(command));
  return has_new_map(runtime.tick(20));
}

}  // namespace

int main() {
  assert(!can_enter(mir2::MapConfig{
      "0", "BrokenMap", std::filesystem::temp_directory_path() / "missing_legacy_map_file.map",
      0, 0, 0, 0}));

  assert(can_enter(mir2::MapConfig{
      "0", "SizedBrokenMap",
      std::filesystem::temp_directory_path() / "missing_sized_legacy_map_file.map", 5, 5, 0, 0}));

  assert(can_enter(mir2::MapConfig{"0", "SyntheticMap", {}, 5, 5, 0, 0}));
  return 0;
}
