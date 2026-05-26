#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "world/legacy_map_environment.hpp"
#include "world/logic_runtime.hpp"

namespace {

void write_u16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value & 0xffU);
  bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
}

std::filesystem::path write_test_map(
    int width, int height, const std::vector<std::pair<int, int>>& blocked) {
  const auto path = std::filesystem::temp_directory_path() / "mir2_movement_blocking_legacy.map";
  std::vector<std::uint8_t> bytes(52U + static_cast<std::size_t>(width) *
                                            static_cast<std::size_t>(height) * 12U);
  write_u16(bytes, 0, static_cast<std::uint16_t>(width));
  write_u16(bytes, 2, static_cast<std::uint16_t>(height));
  for (const auto& [x, y] : blocked) {
    const auto offset = 52U +
        (static_cast<std::size_t>(x) * static_cast<std::size_t>(height) +
         static_cast<std::size_t>(y)) *
            12U;
    write_u16(bytes, offset + 4U, 0x8000U);
  }
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return path;
}

mir2::CharacterRecord make_character(std::string name, int x, int y, std::uint16_t hp = 15) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = x;
  character.y = y;
  character.ability.hp = hp;
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

mir2::CharacterRecord snapshot(mir2::LogicRuntime& runtime, std::string_view name) {
  const auto record = runtime.snapshot_character_actor(name);
  assert(record.has_value());
  return *record;
}

}  // namespace

int main() {
  auto map = std::make_shared<mir2::legacy::MapDocument>();
  map->width = 4;
  map->height = 4;
  map->cells.resize(16);
  map->cells[1 * 4 + 1].fr_img = 0x8000U;
  map->cells[0 * 4 + 3].bk_img = 0x8000U;
  map->cells[2 * 4 + 2].door_index = 0x81U;
  map->cells[3 * 4 + 2].door_index = 0x81U;
  mir2::LegacyMapEnvironment env(4, 4, map);
  assert(!env.can_walk(1, 1, false));
  assert(!env.can_walk(3, 0, false));
  assert(env.static_can_fly(3, 0));
  assert(env.static_can_fly(2, 2));
  assert(env.static_can_fly(2, 3));
  assert(env.can_walk(2, 2, false));
  assert(!env.can_fly_line(0, 0, 3, 0));
  assert(env.can_fire_fly_line(0, 0, 3, 0));
  assert(!env.can_fly_line(0, 1, 1, 1));
  assert(!env.can_fire_fly_line(0, 1, 1, 1));
  assert(env.can_fly_line(2, 1, 2, 2));
  assert(env.can_fly_line(2, 1, 2, 3));
  assert(env.can_fire_fly_line(2, 1, 2, 3));
  const auto opened_door_tiles = env.open_doors_around(2, 2, 100);
  assert(opened_door_tiles.size() == 2);
  assert(env.static_can_fly(2, 2));
  assert(env.static_can_fly(2, 3));
  assert(env.can_fire_fly_line(2, 1, 2, 3));
  assert(env.add_moving_object(0, 0, 1, 100));
  assert(!env.can_walk(0, 0, false));
  assert(env.add_moving_object(0, 1, 2, 100, mir2::LegacyMovingObjectState{true, true, false, false, false}));
  assert(env.can_walk(0, 1, false));
  assert(env.add_moving_object(0, 2, 3, 100, mir2::LegacyMovingObjectState{false, true, true, false, false}));
  assert(env.can_walk(0, 2, false));
  assert(env.add_moving_object(0, 3, 4, 100, mir2::LegacyMovingObjectState{false, true, false, true, false}));
  assert(env.can_walk(0, 3, false));
  assert(env.add_moving_object(1, 0, 5, 100, mir2::LegacyMovingObjectState{false, true, false, false, true}));
  assert(env.can_walk(1, 0, false));
  assert(env.add_moving_object(2, 0, 6, 100, mir2::LegacyMovingObjectState{false, false, false, false, false}));
  assert(env.can_walk(2, 0, false));

  const auto map_path = write_test_map(10, 10, {{5, 3}, {4, 6}, {6, 5}});
  mir2::HostConfig config;
  config.budgets.tick_ms = 20;
  config.maps.push_back(mir2::MapConfig{"0", "MovementMap", map_path, 0, 0, 1, 1});
  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  enter(runtime, 10, make_character("Hero", 3, 3));
  enter(runtime, 11, make_character("Blocker", 5, 5));
  advance(runtime);

  move(runtime, 10, mir2::LogicCommandKind::walk, 4, 3);
  auto hero = snapshot(runtime, "Hero");
  assert(hero.x == 4 && hero.y == 3);
  advance(runtime);

  move(runtime, 10, mir2::LogicCommandKind::walk, 5, 3);
  hero = snapshot(runtime, "Hero");
  assert(hero.x == 4 && hero.y == 3);
  advance(runtime);

  move(runtime, 10, mir2::LogicCommandKind::run, 4, 7);
  hero = snapshot(runtime, "Hero");
  assert(hero.x == 4 && hero.y == 3);
  advance(runtime);

  move(runtime, 10, mir2::LogicCommandKind::walk, 5, 4);
  hero = snapshot(runtime, "Hero");
  assert(hero.x == 5 && hero.y == 4);
  advance(runtime);

  move(runtime, 10, mir2::LogicCommandKind::walk, 5, 5);
  hero = snapshot(runtime, "Hero");
  assert(hero.x == 5 && hero.y == 4);

  enter(runtime, 12, make_character("LowHp", 1, 8, 9));
  move(runtime, 12, mir2::LogicCommandKind::run, 3, 8);
  const auto low_hp = snapshot(runtime, "LowHp");
  assert(low_hp.x == 1 && low_hp.y == 8);

  mir2::HostConfig npc_blocking_config;
  npc_blocking_config.budgets.tick_ms = 20;
  npc_blocking_config.maps.push_back(
      mir2::MapConfig{"0", "NpcBlocking", {}, 0, 0, 12, 12});
  {
    mir2::NpcConfig npc;
    npc.id = "guide";
    npc.map_id = "0";
    npc.name = "Guide";
    npc.x = 6;
    npc.y = 5;
    npc.service = "none";
    npc_blocking_config.npcs.push_back(std::move(npc));
  }
  {
    mir2::NpcConfig merchant;
    merchant.id = "trader";
    merchant.map_id = "0";
    merchant.name = "Trader";
    merchant.x = 5;
    merchant.y = 4;
    merchant.service = "merchant";
    npc_blocking_config.npcs.push_back(std::move(merchant));
  }

  mir2::LogicRuntime npc_runtime(npc_blocking_config);
  npc_runtime.initialize();
  enter(npc_runtime, 20, make_character("Walker", 5, 5));
  advance(npc_runtime);

  move(npc_runtime, 20, mir2::LogicCommandKind::walk, 6, 5);
  auto walker = snapshot(npc_runtime, "Walker");
  assert(walker.x == 5 && walker.y == 5);

  move(npc_runtime, 20, mir2::LogicCommandKind::walk, 5, 4);
  walker = snapshot(npc_runtime, "Walker");
  assert(walker.x == 5 && walker.y == 5);
  return 0;
}
