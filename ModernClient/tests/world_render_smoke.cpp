#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "game/game_state.hpp"
#include "render/software_renderer.hpp"
#include "shared/legacy/map_render_math.hpp"
#include "shared/protocol/client_v1/protocol.hpp"

namespace {

std::uint32_t pixel_at(const mir2::client::SoftwareSurface& surface, int x, int y) {
  return surface.data()[static_cast<std::size_t>(y) *
                            static_cast<std::size_t>(surface.width()) +
                        static_cast<std::size_t>(x)];
}

mir2::client_v1::WorldActor actor(const std::uint64_t id, const int x, const int y) {
  mir2::client_v1::WorldActor result;
  result.actor_id = id;
  result.name = "actor";
  result.x = x;
  result.y = y;
  return result;
}

mir2::client_v1::GroundItemState item(const std::uint64_t id, const int x, const int y) {
  mir2::client_v1::GroundItemState result;
  result.object_id = id;
  result.x = x;
  result.y = y;
  result.looks = 1;
  result.name = "item";
  return result;
}

void test_actor_draw_order_tracks_delphi_actor_list() {
  mir2::client::GameStateStore store;
  mir2::client_v1::WorldSnapshot snapshot;
  snapshot.map_id = "0";
  snapshot.self_actor_id = 30;
  snapshot.actors = {actor(30, 10, 10), actor(10, 10, 10), actor(20, 10, 10)};

  store.apply(snapshot);
  assert((store.world.actor_draw_order == std::vector<std::uint64_t>{30, 10, 20}));

  store.apply(mir2::client_v1::ActorUpsert{actor(10, 11, 10)});
  assert((store.world.actor_draw_order == std::vector<std::uint64_t>{30, 10, 20}));
  assert(store.world.actors.at(10).x == 10);

  store.apply(mir2::client_v1::ActorUpsert{actor(40, 12, 10)});
  assert((store.world.actor_draw_order == std::vector<std::uint64_t>{30, 10, 20, 40}));

  store.apply(mir2::client_v1::ActorRemove{10});
  assert((store.world.actor_draw_order == std::vector<std::uint64_t>{30, 20, 40}));
}

void test_ground_item_draw_order_tracks_delphi_drop_list() {
  mir2::client::GameStateStore store;

  store.apply(mir2::client_v1::GroundItemAdd{item(300, 10, 10)});
  store.apply(mir2::client_v1::GroundItemAdd{item(100, 10, 10)});
  store.apply(mir2::client_v1::GroundItemAdd{item(200, 10, 10)});
  assert((store.world.ground_item_draw_order == std::vector<std::uint64_t>{300, 100, 200}));

  auto updated = item(100, 11, 10);
  updated.name = "updated";
  store.apply(mir2::client_v1::GroundItemAdd{updated});
  assert((store.world.ground_item_draw_order == std::vector<std::uint64_t>{300, 100, 200}));
  assert(store.world.ground_items.at(100).x == 11);

  store.apply(mir2::client_v1::GroundItemRemove{300, 0, 0});
  assert((store.world.ground_item_draw_order == std::vector<std::uint64_t>{100, 200}));

  store.apply(mir2::client_v1::GroundItemAdd{item(400, 12, 12)});
  store.apply(mir2::client_v1::GroundItemAdd{item(500, 12, 12)});
  store.apply(mir2::client_v1::GroundItemRemove{0, 12, 12});
  assert((store.world.ground_item_draw_order == std::vector<std::uint64_t>{100, 200}));
  assert(store.world.ground_items.find(400) == store.world.ground_items.end());
  assert(store.world.ground_items.find(500) == store.world.ground_items.end());
}

}  // namespace

int main() {
  using namespace mir2::client;
  using namespace mir2::legacy;

  SoftwareSurface surface(16, 16);
  surface.clear(0xFF000000U);

  surface.fill_rect(RectI{5, 5, 4, 4}, 0xFFB91C1CU);
  assert(pixel_at(surface, 5, 5) == 0xFFB91C1CU);

  surface.fill_rect(RectI{6, 6, 3, 3}, 0xFF16A34AU);
  assert(pixel_at(surface, 6, 6) == 0xFF16A34AU);
  assert(pixel_at(surface, 5, 5) == 0xFFB91C1CU);

  surface.fill_rect(RectI{7, 7, 3, 3}, 0xFF2563EBU);
  assert(pixel_at(surface, 7, 7) == 0xFF2563EBU);
  assert(pixel_at(surface, 6, 6) == 0xFF16A34AU);

  surface.fill_rect(RectI{8, 8, 2, 2}, 0xFFC026D3U);
  assert(pixel_at(surface, 8, 8) == 0xFFC026D3U);
  assert(pixel_at(surface, 7, 7) == 0xFF2563EBU);

  surface.fill_rect(RectI{9, 9, 2, 2}, 0xFFFACC15U);
  assert(pixel_at(surface, 9, 9) == 0xFFFACC15U);
  assert(pixel_at(surface, 8, 8) == 0xFFC026D3U);

  const auto viewport = make_legacy_map_viewport(100, 200);
  const auto item_x = legacy_ground_item_draw_x(viewport, 100, 20);
  const auto item_y = legacy_ground_item_draw_y(viewport, 200, 14);
  assert(item_x == legacy_tile_draw_x(viewport, 100) + kLegacyHalfX - 10);
  assert(item_y == legacy_object_row_y(viewport, 200) + kLegacyHalfY - 7);
  assert(legacy_actor_draw_row(200, 0) == 200);
  assert(legacy_actor_draw_row(200, 3) == 197);

  test_actor_draw_order_tracks_delphi_actor_list();
  test_ground_item_draw_order_tracks_delphi_drop_list();

  return 0;
}
