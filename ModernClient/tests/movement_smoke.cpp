#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "assets/asset_manager.hpp"
#include "game/game_state.hpp"
#include "shared/legacy/map_document.hpp"
#include "shared/legacy/movement_rules.hpp"

namespace {

void write_u16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
  bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

std::size_t cell_offset(std::size_t header_size, int height, int x, int y) {
  return header_size + (static_cast<std::size_t>(x) * static_cast<std::size_t>(height) +
                        static_cast<std::size_t>(y)) * 12U;
}

void write_cell(std::vector<std::uint8_t>& bytes, std::size_t header_size, int height, int x,
                int y, std::uint16_t bk, std::uint16_t mid, std::uint16_t fr,
                std::uint8_t door_index = 0, std::uint8_t door_offset = 0,
                std::uint16_t check_key = 0) {
  const auto offset = cell_offset(header_size, height, x, y);
  write_u16(bytes, offset, bk ^ check_key);
  write_u16(bytes, offset + 2U, mid ^ check_key);
  write_u16(bytes, offset + 4U, fr ^ check_key);
  bytes[offset + 6U] = door_index;
  bytes[offset + 7U] = door_offset;
}

std::filesystem::path write_client_map_root() {
  const auto root = std::filesystem::temp_directory_path() / "mir2_client_movement_smoke";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "Data");
  std::filesystem::create_directories(root / "Map");

  constexpr int width = 4;
  constexpr int height = 4;
  std::vector<std::uint8_t> bytes(52U + width * height * 12U);
  write_u16(bytes, 0, width);
  write_u16(bytes, 2, height);
  write_cell(bytes, 52U, height, 0, 0, 0x1234U, 0x2345U, 0x3456U);
  write_cell(bytes, 52U, height, 1, 1, 0, 0, 0x8000U);
  write_cell(bytes, 52U, height, 2, 1, 0, 0, 0, 0x80U, 0x00U);
  write_cell(bytes, 52U, height, 3, 1, 0, 0, 0, 0x80U, 0x80U);

  std::ofstream file(root / "Map" / "0.map", std::ios::binary);
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

  constexpr std::uint16_t check_key = 0xAA55U;
  std::vector<std::uint8_t> antihack(64U + width * height * 12U);
  write_u16(antihack, 31U, static_cast<std::uint16_t>(width) ^ check_key);
  write_u16(antihack, 33U, check_key);
  write_u16(antihack, 35U, static_cast<std::uint16_t>(height) ^ check_key);
  write_cell(antihack, 64U, height, 0, 0, 0x1111U, 0x2222U, 0x3333U, 0, 0, check_key);
  write_cell(antihack, 64U, height, 1, 1, 0, 0, 0x8000U, 0, 0, check_key);
  write_cell(antihack, 64U, height, 2, 1, 0, 0, 0, 0x80U, 0x00U, check_key);
  write_cell(antihack, 64U, height, 3, 1, 0, 0, 0, 0x80U, 0x80U, check_key);
  std::ofstream antihack_file(root / "Map" / "LABY01.map", std::ios::binary);
  antihack_file.write(reinterpret_cast<const char*>(antihack.data()),
                      static_cast<std::streamsize>(antihack.size()));
  return root;
}

}  // namespace

int main() {
  using namespace mir2::client;
  using namespace mir2::client_v1;

  const auto map_root = write_client_map_root();
  AssetManager assets;
  assert(assets.initialize(map_root));
  const auto map = assets.load_map("0");
  assert(map != nullptr);
  const auto* origin = map->cell(0, 0);
  assert(origin != nullptr);
  assert(origin->bk_img == 0x1234U);
  assert(origin->mid_img == 0x2345U);
  assert(origin->fr_img == 0x3456U);
  assert(map->can_move(0, 0));
  assert(!map->can_move(1, 1));
  assert(!map->can_move(2, 1));
  assert(map->can_move(3, 1));
  assert(!map->can_move(-1, 0));

  const auto legacy_map = mir2::legacy::decode_map_file(map_root / "Map" / "0.map");
  assert(legacy_map != nullptr);
  assert(!legacy_map->can_move(2, 1));
  assert(legacy_map->can_move(3, 1));

  const auto antihack_map = assets.load_map("LABY01");
  assert(antihack_map != nullptr);
  assert(antihack_map->width == 4);
  assert(antihack_map->height == 4);
  const auto* antihack_origin = antihack_map->cell(0, 0);
  assert(antihack_origin != nullptr);
  assert(antihack_origin->bk_img == 0x1111U);
  assert(antihack_origin->mid_img == 0x2222U);
  assert(antihack_origin->fr_img == 0x3333U);
  assert(!antihack_map->can_move(1, 1));
  assert(!antihack_map->can_move(2, 1));
  assert(antihack_map->can_move(3, 1));

  const auto walk = mir2::legacy::requested_walk_target(4, 4, 0, 0, 2, 2);
  assert(walk.has_value());
  assert(walk->x == 1 && walk->y == 1);
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
  state.apply(ActorAction{100, ActorActionKind::struck, 0, 0, 0, 200, 3, 31, 0, false, 0});
  state.process_legacy_actor_queues(mir2::client::detail::monotonic_ms());
  assert(state.world.latest_struck_ms != 0);
  return 0;
}
