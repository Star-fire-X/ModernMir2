#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

#include "animation/legacy_animation.hpp"
#include "assets/asset_manager.hpp"
#include "render/software_renderer.hpp"
#include "shared/legacy/map_render_math.hpp"

namespace {

std::filesystem::path resolve_root() {
  const std::filesystem::path configured = LR"(F:\mir2\Legend of Mir)";
  if (std::filesystem::exists(configured / "Data")) {
    return configured;
  }
  const auto local = std::filesystem::absolute("Legend of Mir");
  if (std::filesystem::exists(local / "Data")) {
    return local;
  }
  return configured;
}

void write_u16(std::vector<std::uint8_t>& bytes, const std::size_t offset,
               const std::uint16_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
  bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void write_cell(std::vector<std::uint8_t>& bytes, const int height, const int x, const int y,
                const std::uint16_t bk_img, const std::uint16_t mid_img,
                const std::uint16_t fr_img, const std::uint8_t door_index,
                const std::uint8_t door_offset, const std::uint8_t ani_frame,
                const std::uint8_t ani_tick, const std::uint8_t area,
                const std::uint8_t light) {
  constexpr std::size_t kMapHeaderSize = 52U;
  constexpr std::size_t kMapCellSize = 12U;
  const auto source = kMapHeaderSize +
      (static_cast<std::size_t>(x) * static_cast<std::size_t>(height) +
       static_cast<std::size_t>(y)) *
          kMapCellSize;
  write_u16(bytes, source, bk_img);
  write_u16(bytes, source + 2U, mid_img);
  write_u16(bytes, source + 4U, fr_img);
  bytes[source + 6U] = door_index;
  bytes[source + 7U] = door_offset;
  bytes[source + 8U] = ani_frame;
  bytes[source + 9U] = ani_tick;
  bytes[source + 10U] = area;
  bytes[source + 11U] = light;
}

std::filesystem::path write_alignment_map_root() {
  const auto root = std::filesystem::temp_directory_path() / "mir2_map_render_alignment_smoke";
  std::filesystem::create_directories(root / "Data");
  std::filesystem::create_directories(root / "Map");

  constexpr int width = 3;
  constexpr int height = 2;
  std::vector<std::uint8_t> bytes(52U + width * height * 12U);
  write_u16(bytes, 0U, width);
  write_u16(bytes, 2U, height);

  write_cell(bytes, height, 0, 0, 2, 3, 4, 5, 6, 7, 8, 1, 9);
  write_cell(bytes, height, 1, 0, 0, 0, 0x8000U, 0, 0, 0, 0, 0, 0);
  write_cell(bytes, height, 2, 1, 0, 0, 10, 0x80U, 0x80U, 0, 0, 6, 11);

  std::ofstream file(root / "Map" / "phase1.map", std::ios::binary);
  file.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  return root;
}

mir2::client::ArchiveId delphi_object_archive_for_area(const int area) {
  using mir2::client::ArchiveId;
  switch (area) {
    case 0: return ArchiveId::objects1;
    case 1: return ArchiveId::objects2;
    case 2: return ArchiveId::objects3;
    case 3: return ArchiveId::objects4;
    case 4: return ArchiveId::objects5;
    case 5: return ArchiveId::objects6;
    case 6: return ArchiveId::objects7;
    default: return ArchiveId::objects1;
  }
}

std::shared_ptr<const mir2::client::SpriteFrame> first_decodable_frame(
    mir2::client::AssetManager& assets, const mir2::client::ArchiveId archive_id,
    const int max_index) {
  for (int index = 0; index < max_index; ++index) {
    auto frame = assets.get_frame(archive_id, index);
    if (frame != nullptr && !frame->empty()) {
      return frame;
    }
  }
  return nullptr;
}

std::uint8_t legacy_blend_channel(const std::uint8_t src, const std::uint8_t dst) {
  const auto value =
      static_cast<int>(src) + ((255 - static_cast<int>(src)) * static_cast<int>(dst) + 127) / 255;
  return static_cast<std::uint8_t>(std::min(255, value));
}

std::uint32_t legacy_blend_color(const std::uint32_t src, const std::uint32_t dst) {
  const auto src_r = static_cast<std::uint8_t>((src >> 16U) & 0xFFU);
  const auto src_g = static_cast<std::uint8_t>((src >> 8U) & 0xFFU);
  const auto src_b = static_cast<std::uint8_t>(src & 0xFFU);
  const auto dst_r = static_cast<std::uint8_t>((dst >> 16U) & 0xFFU);
  const auto dst_g = static_cast<std::uint8_t>((dst >> 8U) & 0xFFU);
  const auto dst_b = static_cast<std::uint8_t>(dst & 0xFFU);
  return 0xFF000000U | (static_cast<std::uint32_t>(legacy_blend_channel(src_r, dst_r)) << 16U) |
         (static_cast<std::uint32_t>(legacy_blend_channel(src_g, dst_g)) << 8U) |
         legacy_blend_channel(src_b, dst_b);
}

}  // namespace

int main() {
  using namespace mir2::client;
  using namespace mir2::legacy;

  static_assert(kLegacyLogicalMapUnit == 40);
  static_assert(kLegacyUnitX == 48);
  static_assert(kLegacyUnitY == 32);
  static_assert(kLegacyLongHeightRows == 35);

  assert(legacy_up_int(-1.2) == -1);
  assert(legacy_up_int(-0.5) == 0);
  assert(legacy_up_int(0.0) == 0);
  assert(legacy_up_int(0.5) == 1);
  assert(legacy_up_int(2.0) == 2);

  const auto viewport = make_legacy_map_viewport(100, 200);
  assert(viewport.left == 91);
  assert(viewport.top == 191);
  assert(viewport.right == 109);
  assert(viewport.bottom == 208);
  assert(viewport.draw_origin_x == -66);
  assert(viewport.draw_origin_y == -64);

  const auto shifted = make_legacy_map_viewport(100, 200, 12, -8);
  assert(shifted.left == 91);
  assert(shifted.top == 191);
  assert(shifted.draw_origin_x == -78);
  assert(shifted.draw_origin_y == -56);

  const auto edge = make_legacy_map_viewport(4, 5);
  assert(edge.left == -5);
  assert(edge.top == -4);

  const auto same_tile = legacy_screen_from_map(viewport, 100, 200);
  assert(same_tile.first == 388);
  assert(same_tile.second == 208);
  assert(legacy_mouse_to_map(viewport, same_tile.first, same_tile.second) ==
         std::make_pair(100, 200));

  const auto next_tile = legacy_screen_from_map(viewport, 101, 201);
  assert(next_tile.first == 436);
  assert(next_tile.second == 240);
  assert(legacy_mouse_to_map(viewport, next_tile.first, next_tile.second) ==
         std::make_pair(101, 201));

  const auto shifted_tile = legacy_screen_from_map(shifted, 100, 200);
  assert(shifted_tile.first == 376);
  assert(shifted_tile.second == 216);
  assert(legacy_mouse_to_map(shifted, shifted_tile.first, shifted_tile.second) ==
         std::make_pair(100, 200));
  assert(legacy_mouse_to_map_clamped(shifted, -9999, -9999, 700, 700) == std::make_pair(0, 0));
  assert(legacy_mouse_to_map_clamped(shifted, 99999, 99999, 700, 700) ==
         std::make_pair(699, 699));

  assert(legacy_tile_draw_x(shifted, 100) == 354);
  assert(legacy_ground_back_y(shifted, 200) == 200);
  assert(legacy_ground_mid_y(shifted, 200) == 232);
  assert(legacy_object_row_y(shifted, 200) == 200);
  assert(legacy_actor_base_x(shifted, 100, 8) == 362);
  assert(legacy_actor_base_y(shifted, 200, -16) == 184);
  assert(legacy_actor_draw_row(200, 0) == 200);
  assert(legacy_actor_draw_row(200, 2) == 198);
  assert(legacy_ground_item_draw_x(shifted, 100, 20) ==
         legacy_tile_draw_x(shifted, 100) + kLegacyHalfX - 10);
  assert(legacy_ground_item_draw_y(shifted, 200, 14) ==
         legacy_object_row_y(shifted, 200) + kLegacyHalfY - 7);

  const std::vector<int> delphi_row_order{/*large object*/ 0, /*ground item*/ 1,
                                          /*actor*/ 2, /*fly effect*/ 3};
  assert(delphi_row_order[0] < delphi_row_order[1]);
  assert(delphi_row_order[1] < delphi_row_order[2]);
  assert(delphi_row_order[2] < delphi_row_order[3]);

  SoftwareSurface blend_surface(2, 1);
  constexpr std::uint32_t kBlendDst = 0xFF204060U;
  blend_surface.clear(kBlendDst);
  const std::array<std::uint32_t, 2> blend_pixels{0x00000000U, 0xFF804020U};
  blend_surface.blit_rgba_legacy_blend(0, 0, 2, 1, blend_pixels.data());
  assert(blend_surface.data()[0] == kBlendDst);
  assert(blend_surface.data()[1] == legacy_blend_color(blend_pixels[1], kBlendDst));

  assert(legacy_ground_tile_frame_index(2, 4, 2) == 1);
  assert(legacy_ground_tile_frame_index(2, 4, 0x8002U) == 1);
  assert(legacy_ground_tile_frame_index(1, 4, 2) == -1);
  assert(legacy_ground_tile_frame_index(2, 3, 2) == -1);
  assert(legacy_ground_tile_frame_index(2, 4, 0) == -1);
  assert(legacy_small_tile_frame_index(3) == 2);
  assert(legacy_small_tile_frame_index(0) == -1);

  assert(delphi_object_archive_for_area(0) == ArchiveId::objects1);
  assert(delphi_object_archive_for_area(1) == ArchiveId::objects2);
  assert(delphi_object_archive_for_area(6) == ArchiveId::objects7);
  assert(delphi_object_archive_for_area(99) == ArchiveId::objects1);

  AssetManager temp_assets;
  assert(temp_assets.initialize(write_alignment_map_root()));
  const auto phase1_map = temp_assets.load_map("phase1");
  assert(phase1_map != nullptr);
  assert(phase1_map->width == 3);
  assert(phase1_map->height == 2);
  const auto* first = phase1_map->cell(0, 0);
  assert(first != nullptr);
  assert(first->bk_img == 2);
  assert(first->mid_img == 3);
  assert(first->fr_img == 4);
  assert(first->door_index == 5);
  assert(first->door_offset == 6);
  assert(first->ani_frame == 7);
  assert(first->ani_tick == 8);
  assert(first->area == 1);
  assert(first->light == 9);
  assert(phase1_map->cell(-1, 0) == nullptr);
  assert(phase1_map->cell(3, 0) == nullptr);
  assert(phase1_map->can_move(0, 0));
  assert(!phase1_map->can_move(1, 0));
  assert(phase1_map->can_move(2, 1));

  MapCell object_cell;
  object_cell.fr_img = 100;
  object_cell.ani_frame = 3;
  object_cell.ani_tick = 1;
  assert(legacy_map_object_frame(object_cell, 0) == 99);
  assert(legacy_map_object_frame(object_cell, 2) == 100);
  assert(legacy_map_object_frame(object_cell, 5) == 101);
  object_cell.ani_frame = 0x83U;
  assert(legacy_map_object_blend(object_cell));
  assert(legacy_map_object_frame(object_cell, 0) == 99);
  assert(legacy_map_object_frame(object_cell, 2) == 100);
  object_cell.ani_frame = 3;
  object_cell.ani_tick = 0;
  assert(legacy_map_object_frame(object_cell, 0) == 99);
  assert(legacy_map_object_frame(object_cell, 1) == 100);
  assert(legacy_map_object_frame(object_cell, 2) == 101);
  assert(legacy_map_object_frame(object_cell, 3) == 99);
  object_cell.ani_tick = 2;
  assert(legacy_map_object_frame(object_cell, 0) == 99);
  assert(legacy_map_object_frame(object_cell, 2) == 99);
  assert(legacy_map_object_frame(object_cell, 3) == 100);
  assert(legacy_map_object_frame(object_cell, 6) == 101);
  assert(legacy_map_object_frame(object_cell, 9) == 99);
  object_cell.ani_frame = 0;
  object_cell.door_index = 1;
  object_cell.door_offset = 0x82U;
  assert(legacy_map_object_frame(object_cell, 0) == 101);

  AssetManager assets;
  assert(assets.initialize(resolve_root()));
  const auto map0 = assets.load_map("0");
  assert(map0 != nullptr);
  assert(map0->width > 0);
  assert(map0->height > 0);
  assert(map0->cell(0, 0) != nullptr);
  assert(first_decodable_frame(assets, ArchiveId::tiles, 32) != nullptr);
  assert(first_decodable_frame(assets, ArchiveId::sm_tiles, 128) != nullptr);
  assert(first_decodable_frame(assets, ArchiveId::objects1, 512) != nullptr);
  assert(first_decodable_frame(assets, ArchiveId::dn_items, 4096) != nullptr);
  assert(first_decodable_frame(assets, ArchiveId::effect, 512) != nullptr);
  assert(assets.get_frame(ArchiveId::tiles, 9999999) == nullptr);
  assert(assets.get_frame(ArchiveId::tiles, 9999999) == nullptr);

  std::cout << "phase4_map=" << phase1_map->width << "x" << phase1_map->height
            << " map0=" << map0->width << "x" << map0->height << '\n';
  return 0;
}
