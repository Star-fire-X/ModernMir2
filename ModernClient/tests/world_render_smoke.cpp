#include <cassert>
#include <cstddef>
#include <cstdint>

#include "render/software_renderer.hpp"
#include "shared/legacy/map_render_math.hpp"

namespace {

std::uint32_t pixel_at(const mir2::client::SoftwareSurface& surface, int x, int y) {
  return surface.data()[static_cast<std::size_t>(y) *
                            static_cast<std::size_t>(surface.width()) +
                        static_cast<std::size_t>(x)];
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

  return 0;
}
