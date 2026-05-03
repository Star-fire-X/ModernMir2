#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

void write_u16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value & 0xffU);
  bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
}

std::filesystem::path write_door_map() {
  constexpr int width = 5;
  constexpr int height = 5;
  const auto path = std::filesystem::temp_directory_path() / "mir2_door_open_close_smoke.map";
  std::vector<std::uint8_t> bytes(52U + width * height * 12U);
  write_u16(bytes, 0, width);
  write_u16(bytes, 2, height);
  const auto offset = 52U + (2U * height + 2U) * 12U;
  bytes[offset + 6U] = 0x80U | 1U;
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return path;
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

mir2::CharacterRecord make_character() {
  mir2::CharacterRecord character;
  character.account_id = "acct";
  character.character_name = "Hero";
  character.map_id = "0";
  character.x = 1;
  character.y = 2;
  character.ability.hp = 15;
  character.ability.max_hp = 15;
  return character;
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.budgets.tick_ms = 20;
  mir2::MapConfig map;
  map.id = "0";
  map.title = "DoorMap";
  map.source_map = write_door_map();
  map.gates.push_back(mir2::MapGateConfig{2, 2, "1", 1, 1, true});
  config.maps.push_back(map);
  config.maps.push_back(mir2::MapConfig{"1", "Target", {}, 5, 5, 1, 1});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  mir2::LogicCommand enter;
  enter.kind = mir2::LogicCommandKind::enter_world;
  enter.session_id = 1;
  enter.account_id = "acct";
  enter.character_name = "Hero";
  enter.map_id = "0";
  enter.x = 1;
  enter.y = 2;
  enter.character = make_character();
  static_cast<void>(runtime.route_logic_command(enter));
  static_cast<void>(runtime.tick());

  for (int index = 0; index < 12; ++index) {
    static_cast<void>(runtime.tick());
  }

  mir2::LogicCommand walk;
  walk.kind = mir2::LogicCommandKind::walk;
  walk.session_id = 1;
  walk.x = 2;
  walk.y = 2;
  static_cast<void>(runtime.route_logic_command(walk));
  const auto open_dispatch = runtime.tick();
  const auto opened = find_packet(open_dispatch, 1, mir2::kSmOpenDoorOk);
  assert(opened.has_value());
  assert(opened->message.param == 2 && opened->message.tag == 2);
  const auto still_source = runtime.snapshot_character_actor("Hero");
  assert(still_source.has_value() && still_source->map_id == "0");

  auto saw_close = false;
  for (int index = 0; index < 270; ++index) {
    const auto close_dispatch = runtime.tick();
    const auto closed = find_packet(close_dispatch, 1, mir2::kSmCloseDoor);
    if (closed.has_value() && closed->message.param == 2 && closed->message.tag == 2) {
      saw_close = true;
      break;
    }
  }
  assert(saw_close);
  return 0;
}
