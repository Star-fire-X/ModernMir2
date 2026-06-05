#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "shared/legacy/movement_rules.hpp"
#include "world/logic_runtime.hpp"

namespace {

void write_u16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value & 0xffU);
  bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
}

std::filesystem::path write_test_map() {
  constexpr int width = 8;
  constexpr int height = 8;
  const auto path =
      std::filesystem::temp_directory_path() / "mir2_movement_overlap_direction_legacy.map";
  std::vector<std::uint8_t> bytes(52U + static_cast<std::size_t>(width) *
                                            static_cast<std::size_t>(height) * 12U);
  write_u16(bytes, 0, width);
  write_u16(bytes, 2, height);
  const auto blocked_offset = 52U + (3U * height + 1U) * 12U;
  write_u16(bytes, blocked_offset + 4U, 0x8000U);
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  return path;
}

mir2::CharacterRecord make_character(std::string name, int x, int y) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = x;
  character.y = y;
  character.dir = mir2::legacy::kDirDown;
  character.ability.hp = 20;
  character.ability.max_hp = 20;
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

void move(mir2::LogicRuntime& runtime, std::uint64_t session_id,
          mir2::LogicCommandKind kind, int x, int y) {
  mir2::LogicCommand command;
  command.kind = kind;
  command.session_id = session_id;
  command.x = x;
  command.y = y;
  static_cast<void>(runtime.route_logic_command(command));
  static_cast<void>(runtime.tick());
}

void turn(mir2::LogicRuntime& runtime, std::uint64_t session_id,
          int x, int y, std::uint8_t dir) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::turn;
  command.session_id = session_id;
  command.x = x;
  command.y = y;
  command.dir = dir;
  static_cast<void>(runtime.route_logic_command(command));
  static_cast<void>(runtime.tick());
}

mir2::CharacterRecord snapshot(mir2::LogicRuntime& runtime, std::string_view name) {
  const auto record = runtime.snapshot_character_actor(name);
  assert(record.has_value());
  return *record;
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.budgets.tick_ms = 20;
  config.maps.push_back(
      mir2::MapConfig{"0", "OverlapMap", write_test_map(), 0, 0, 1, 1});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  enter(runtime, 1, make_character("Hero", 1, 1));
  enter(runtime, 2, make_character("Blocker", 2, 1));
  enter(runtime, 3, make_character("Runner", 1, 3));
  enter(runtime, 4, make_character("RunBlocker", 3, 3));
  advance(runtime);

  move(runtime, 1, mir2::LogicCommandKind::walk, 2, 1);
  auto hero = snapshot(runtime, "Hero");
  assert(hero.x == 2 && hero.y == 1);

  move(runtime, 1, mir2::LogicCommandKind::walk, 3, 1);
  hero = snapshot(runtime, "Hero");
  assert(hero.x == 2 && hero.y == 1);
  assert(hero.dir == mir2::legacy::kDirRight);

  turn(runtime, 1, 3, 1, mir2::legacy::kDirDown);
  hero = snapshot(runtime, "Hero");
  assert(hero.x == 2 && hero.y == 1);
  assert(hero.dir == mir2::legacy::kDirRight);

  move(runtime, 3, mir2::LogicCommandKind::run, 3, 3);
  const auto runner = snapshot(runtime, "Runner");
  assert(runner.x == 3 && runner.y == 3);
  assert(runner.dir == mir2::legacy::kDirRight);
  return 0;
}
