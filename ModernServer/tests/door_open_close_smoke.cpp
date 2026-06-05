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
  const auto far_offset = 52U + (4U * height + 4U) * 12U;
  bytes[far_offset + 6U] = 0x80U | 2U;
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

  {
    mir2::LogicRuntime adjacent_runtime(config);
    adjacent_runtime.initialize();

    mir2::LogicCommand adjacent_enter;
    adjacent_enter.kind = mir2::LogicCommandKind::enter_world;
    adjacent_enter.session_id = 2;
    adjacent_enter.account_id = "acct";
    adjacent_enter.character_name = "Hero";
    adjacent_enter.map_id = "0";
    adjacent_enter.x = 1;
    adjacent_enter.y = 2;
    adjacent_enter.character = make_character();
    static_cast<void>(adjacent_runtime.route_logic_command(adjacent_enter));
    std::uint64_t adjacent_now_ms = 100000;
    static_cast<void>(adjacent_runtime.tick(adjacent_now_ms));
    for (int index = 0; index < 12; ++index) {
      adjacent_now_ms += 20;
      static_cast<void>(adjacent_runtime.tick(adjacent_now_ms));
    }

    mir2::LogicCommand adjacent_open_door;
    adjacent_open_door.kind = mir2::LogicCommandKind::open_door;
    adjacent_open_door.session_id = 2;
    adjacent_open_door.x = 2;
    adjacent_open_door.y = 2;
    static_cast<void>(adjacent_runtime.route_logic_command(adjacent_open_door));
    adjacent_now_ms += 20;
    const auto adjacent_open_dispatch = adjacent_runtime.tick(adjacent_now_ms);
    const auto adjacent_opened =
        find_packet(adjacent_open_dispatch, 2, mir2::kSmOpenDoorOk);
    assert(adjacent_opened.has_value());
    assert(adjacent_opened->message.param == 2 && adjacent_opened->message.tag == 2);
  }

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
  std::uint64_t now_ms = 100000;
  static_cast<void>(runtime.tick(now_ms));

  for (int index = 0; index < 12; ++index) {
    now_ms += 20;
    static_cast<void>(runtime.tick(now_ms));
  }

  mir2::LogicCommand far_open_door;
  far_open_door.kind = mir2::LogicCommandKind::open_door;
  far_open_door.session_id = 1;
  far_open_door.x = 4;
  far_open_door.y = 4;
  static_cast<void>(runtime.route_logic_command(far_open_door));
  now_ms += 20;
  const auto far_open_dispatch = runtime.tick(now_ms);
  assert(!find_packet(far_open_dispatch, 1, mir2::kSmOpenDoorOk).has_value());

  mir2::LogicCommand walk;
  walk.kind = mir2::LogicCommandKind::walk;
  walk.session_id = 1;
  walk.x = 2;
  walk.y = 2;
  static_cast<void>(runtime.route_logic_command(walk));
  now_ms += 20;
  const auto walk_dispatch = runtime.tick(now_ms);
  assert(!find_packet(walk_dispatch, 1, mir2::kSmOpenDoorOk).has_value());
  const auto still_source = runtime.snapshot_character_actor("Hero");
  assert(still_source.has_value() && still_source->map_id == "0" &&
         still_source->x == 2 && still_source->y == 2);

  mir2::LogicCommand open_door;
  open_door.kind = mir2::LogicCommandKind::open_door;
  open_door.session_id = 1;
  open_door.x = 2;
  open_door.y = 2;
  static_cast<void>(runtime.route_logic_command(open_door));
  now_ms += 20;
  const auto open_time_ms = now_ms;
  const auto open_dispatch = runtime.tick(now_ms);
  const auto opened = find_packet(open_dispatch, 1, mir2::kSmOpenDoorOk);
  assert(opened.has_value());
  assert(opened->message.param == 2 && opened->message.tag == 2);

  const auto at_ttl = runtime.tick(open_time_ms + 5000);
  assert(!find_packet(at_ttl, 1, mir2::kSmCloseDoor).has_value());

  const auto after_ttl_before_gate = runtime.tick(open_time_ms + 5001);
  assert(!find_packet(after_ttl_before_gate, 1, mir2::kSmCloseDoor).has_value());

  const auto at_door_gate_boundary = runtime.tick(open_time_ms + 5500);
  assert(!find_packet(at_door_gate_boundary, 1, mir2::kSmCloseDoor).has_value());

  const auto close_dispatch = runtime.tick(open_time_ms + 5501);
  const auto closed = find_packet(close_dispatch, 1, mir2::kSmCloseDoor);
  assert(closed.has_value());
  assert(closed->message.param == 2 && closed->message.tag == 2);

  const auto no_repeat = runtime.tick(open_time_ms + 6002);
  assert(!find_packet(no_repeat, 1, mir2::kSmCloseDoor).has_value());
  return 0;
}
