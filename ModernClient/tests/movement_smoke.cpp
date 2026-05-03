#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "assets/asset_manager.hpp"
#include "game/game_state.hpp"
#include "shared/legacy/movement_rules.hpp"

namespace {

void write_u16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
  bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

std::filesystem::path write_client_map_root() {
  const auto root = std::filesystem::temp_directory_path() / "mir2_client_movement_smoke";
  std::filesystem::create_directories(root / "Data");
  std::filesystem::create_directories(root / "Map");

  constexpr int width = 4;
  constexpr int height = 4;
  std::vector<std::uint8_t> bytes(52U + width * height * 12U);
  write_u16(bytes, 0, width);
  write_u16(bytes, 2, height);
  const auto blocked_offset = 52U + (1U * height + 1U) * 12U;
  write_u16(bytes, blocked_offset + 4U, 0x8000U);
  const auto closed_door_offset = 52U + (2U * height + 1U) * 12U;
  bytes[closed_door_offset + 6U] = 0x80U;
  bytes[closed_door_offset + 7U] = 0x80U;

  std::ofstream file(root / "Map" / "0.map", std::ios::binary);
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return root;
}

}  // namespace

int main() {
  using namespace mir2::client;
  using namespace mir2::client_v1;

  AssetManager assets;
  assert(assets.initialize(write_client_map_root()));
  const auto map = assets.load_map("0");
  assert(map != nullptr);
  assert(map->can_move(0, 0));
  assert(!map->can_move(1, 1));
  assert(!map->can_move(2, 1));
  assert(!map->can_move(-1, 0));

  assert(mir2::legacy::requested_run_target(4, 4, 0, 0, 2, 2)->x == 2);
  assert(!mir2::legacy::requested_run_target(4, 4, 3, 3, 5, 5).has_value());

  GameStateStore state;
  WorldSnapshot snapshot;
  snapshot.map_id = "0";
  snapshot.width = 4;
  snapshot.height = 4;
  snapshot.self_actor_id = 100;
  snapshot.actors.push_back(WorldActor{100, "Hero", 2, 2, 2, 0, 0, ActorType::player});
  state.apply(snapshot);

  auto& self = state.world.actors[100];
  self.from_x = 2;
  self.from_y = 2;
  self.x = 3;
  self.y = 2;
  self.current_action = ActorActionKind::walk;
  state.world.action_locked = true;
  state.apply(ActionAck{false, 0});
  assert(!state.world.action_locked);
  assert(self.x == 2 && self.y == 2);
  assert(self.current_action == ActorActionKind::turn);

  state.apply(ActorVitals{100, 10, 15, -1, -1, 3, 200, false});
  assert(state.world.latest_struck_ms != 0);
  return 0;
}
