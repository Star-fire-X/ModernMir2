#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

#include "animation/legacy_animation.hpp"
#include "assets/asset_manager.hpp"
#include "assets/legacy_map_resources.hpp"
#include "render/software_renderer.hpp"
#include "shared/legacy/map_render_order.hpp"
#include "shared/legacy/map_render_math.hpp"

namespace {

std::filesystem::path optional_asset_root() {
  const auto* env = std::getenv("MIR2_ASSET_ROOT");
  if (env != nullptr && *env != '\0') {
    const std::filesystem::path env_root{env};
    if (std::filesystem::exists(env_root / "Data") && std::filesystem::exists(env_root / "Map")) {
      return env_root;
    }
  }
  const std::filesystem::path configured = LR"(F:\mir2\Legend of Mir)";
  if (std::filesystem::exists(configured / "Data") && std::filesystem::exists(configured / "Map")) {
    return configured;
  }
  const auto local = std::filesystem::absolute("Legend of Mir");
  if (std::filesystem::exists(local / "Data") && std::filesystem::exists(local / "Map")) {
    return local;
  }
  return {};
}

void write_u16(std::vector<std::uint8_t>& bytes, const std::size_t offset,
               const std::uint16_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
  bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void write_cell(std::vector<std::uint8_t>& bytes, const std::size_t header_size,
                const int height, const int x, const int y,
                const mir2::client::MapCell cell, const std::uint16_t key = 0) {
  constexpr std::size_t kMapCellSize = 12U;
  const auto source = header_size +
      (static_cast<std::size_t>(x) * static_cast<std::size_t>(height) +
       static_cast<std::size_t>(y)) *
          kMapCellSize;
  write_u16(bytes, source, cell.bk_img ^ key);
  write_u16(bytes, source + 2U, cell.mid_img ^ key);
  write_u16(bytes, source + 4U, cell.fr_img ^ key);
  bytes[source + 6U] = cell.door_index;
  bytes[source + 7U] = cell.door_offset;
  bytes[source + 8U] = cell.ani_frame;
  bytes[source + 9U] = cell.ani_tick;
  bytes[source + 10U] = cell.area;
  bytes[source + 11U] = cell.light;
}

void write_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
}

void expect_cell(const mir2::client::MapCell* cell, const std::uint16_t bk_img,
                 const std::uint16_t mid_img, const std::uint16_t fr_img,
                 const std::uint8_t door_index, const std::uint8_t door_offset,
                 const std::uint8_t ani_frame, const std::uint8_t ani_tick,
                 const std::uint8_t area, const std::uint8_t light) {
  assert(cell != nullptr);
  assert(cell->bk_img == bk_img);
  assert(cell->mid_img == mid_img);
  assert(cell->fr_img == fr_img);
  assert(cell->door_index == door_index);
  assert(cell->door_offset == door_offset);
  assert(cell->ani_frame == ani_frame);
  assert(cell->ani_tick == ani_tick);
  assert(cell->area == area);
  assert(cell->light == light);
}

std::filesystem::path write_alignment_map_root() {
  const auto root = std::filesystem::temp_directory_path() / "mir2_map_render_alignment_smoke";
  std::filesystem::create_directories(root / "Data");
  std::filesystem::create_directories(root / "Map");

  {
    constexpr int width = 3;
    constexpr int height = 2;
    constexpr std::size_t header_size = 52U;
    std::vector<std::uint8_t> bytes(header_size + width * height * 12U);
    write_u16(bytes, 0U, width);
    write_u16(bytes, 2U, height);

    write_cell(bytes, header_size, height, 0, 0, {2, 3, 4, 5, 6, 7, 8, 1, 9});
    write_cell(bytes, header_size, height, 0, 1, {0x8001U, 20, 30, 0, 0, 0, 0, 0, 0});
    write_cell(bytes, header_size, height, 1, 0, {40, 50, 0x8002U, 0, 0, 0, 0, 0, 0});
    write_cell(bytes, header_size, height, 1, 1, {60, 70, 80, 0x80U | 3U, 0, 9, 10, 2, 11});
    write_cell(bytes, header_size, height, 2, 0, {90, 100, 110, 0x80U | 3U, 0x80U, 12, 13, 3, 14});
    write_cell(bytes, header_size, height, 2, 1, {120, 130, 140, 4, 5, 15, 16, 6, 17});
    write_file(root / "Map" / "phase1.map", bytes);
  }

  {
    constexpr int width = 2;
    constexpr int height = 3;
    constexpr std::uint16_t key = 0xAA55U;
    constexpr std::size_t header_size = 64U;
    std::vector<std::uint8_t> bytes(header_size + width * height * 12U);
    write_u16(bytes, 31U, width ^ key);
    write_u16(bytes, 33U, key);
    write_u16(bytes, 35U, height ^ key);

    write_cell(bytes, header_size, height, 0, 0, {11, 12, 13, 14, 15, 16, 17, 1, 18}, key);
    write_cell(bytes, header_size, height, 0, 1, {21, 22, 23, 24, 25, 26, 27, 2, 28}, key);
    write_cell(bytes, header_size, height, 0, 2, {31, 32, 0x8001U, 34, 35, 36, 37, 3, 38}, key);
    write_cell(bytes, header_size, height, 1, 0, {41, 42, 43, 44, 45, 46, 47, 4, 48}, key);
    write_cell(bytes, header_size, height, 1, 1, {51, 52, 53, 0x80U | 5U, 0, 56, 57, 5, 58}, key);
    write_cell(bytes, header_size, height, 1, 2, {61, 62, 63, 0x80U | 5U, 0x80U, 66, 67, 6, 68}, key);
    write_file(root / "Map" / "SNAKE.map", bytes);
  }
  return root;
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
  const auto bounds = legacy_map_render_bounds(viewport);
  assert(bounds.tile_left == 89);
  assert(bounds.tile_top == 190);
  assert(bounds.tile_right == 110);
  assert(bounds.tile_bottom == 209);
  assert(bounds.object_left == 89);
  assert(bounds.object_top == 191);
  assert(bounds.object_right == 111);
  assert(bounds.object_bottom == 243);
  assert(bounds.visible_left == 91);
  assert(bounds.visible_top == 191);
  assert(bounds.visible_right == 109);
  assert(bounds.visible_bottom == 208);

  const auto shifted = make_legacy_map_viewport(100, 200, 12, -8);
  assert(shifted.left == 91);
  assert(shifted.top == 191);
  assert(shifted.draw_origin_x == -78);
  assert(shifted.draw_origin_y == -56);

  const auto edge = make_legacy_map_viewport(4, 5);
  assert(edge.left == -5);
  assert(edge.top == -4);
  const auto edge_bounds = legacy_map_render_bounds(edge);
  assert(edge_bounds.tile_left == -7);
  assert(edge_bounds.tile_top == -5);
  assert(edge_bounds.object_bottom == 48);

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
  assert(legacy_effective_map_extent(400, 700) == 400);
  assert(legacy_effective_map_extent(0, 700) == 700);
  assert(legacy_effective_map_extent(0, -1) == 0);
  assert(legacy_mouse_to_map_clamped(shifted, 99999, 99999, 400, 300, 700, 700) ==
         std::make_pair(399, 299));
  assert(legacy_mouse_to_map_clamped(shifted, 99999, 99999, 0, 0, 700, 700) ==
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

  static_assert(kLegacyMapRowDrawOrder[0] == LegacyMapDrawLayer::large_object);
  static_assert(kLegacyMapRowDrawOrder[1] == LegacyMapDrawLayer::ground_item);
  static_assert(kLegacyMapRowDrawOrder[2] == LegacyMapDrawLayer::actor);
  static_assert(kLegacyMapRowDrawOrder[3] == LegacyMapDrawLayer::actor_overlay);
  static_assert(kLegacyMapRowDrawOrder[4] == LegacyMapDrawLayer::fly_effect);
  assert(legacy_map_draw_layer_name(LegacyMapDrawLayer::large_object) == "large_object");
  assert(legacy_map_draw_layer_name(LegacyMapDrawLayer::background_tiles) ==
         "background_tiles");
  assert(legacy_map_draw_layer_rank(LegacyMapDrawLayer::background_tiles) <
         legacy_map_draw_layer_rank(LegacyMapDrawLayer::middle_tiles));
  assert(legacy_map_draw_layer_rank(LegacyMapDrawLayer::middle_tiles) <
         legacy_map_draw_layer_rank(LegacyMapDrawLayer::small_objects));
  assert(legacy_map_draw_layer_rank(LegacyMapDrawLayer::small_objects) <
         legacy_map_draw_layer_rank(LegacyMapDrawLayer::ground_effects));
  assert(legacy_map_draw_layer_rank(LegacyMapDrawLayer::large_object) <
         legacy_map_draw_layer_rank(LegacyMapDrawLayer::ground_item));
  assert(legacy_map_draw_layer_rank(LegacyMapDrawLayer::ground_item) <
         legacy_map_draw_layer_rank(LegacyMapDrawLayer::actor));
  assert(legacy_map_draw_layer_rank(LegacyMapDrawLayer::actor) <
         legacy_map_draw_layer_rank(LegacyMapDrawLayer::actor_overlay));
  assert(legacy_map_draw_layer_rank(LegacyMapDrawLayer::actor_overlay) <
         legacy_map_draw_layer_rank(LegacyMapDrawLayer::fly_effect));

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

  const auto ground_resource = legacy_ground_tile_resource(2, 4, 0x8002U);
  assert(ground_resource.has_value());
  assert(ground_resource->archive == ArchiveId::tiles);
  assert(ground_resource->index == 1);
  assert(!legacy_ground_tile_resource(1, 4, 2).has_value());

  const auto small_resource = legacy_small_tile_resource(3);
  assert(small_resource.has_value());
  assert(small_resource->archive == ArchiveId::sm_tiles);
  assert(small_resource->index == 2);
  assert(!legacy_small_tile_resource(0).has_value());

  assert(legacy_object_archive_for_area(0) == ArchiveId::objects1);
  assert(legacy_object_archive_for_area(1) == ArchiveId::objects2);
  assert(legacy_object_archive_for_area(6) == ArchiveId::objects7);
  assert(legacy_object_archive_for_area(99) == ArchiveId::objects1);
  const auto object_resource = legacy_map_object_resource(6, 42);
  assert(object_resource.has_value());
  assert(object_resource->archive == ArchiveId::objects7);
  assert(object_resource->index == 42);
  assert(!legacy_map_object_resource(0, -1).has_value());

  AssetManager temp_assets;
  assert(temp_assets.initialize(write_alignment_map_root()));
  const auto phase1_map = temp_assets.load_map("phase1");
  assert(phase1_map != nullptr);
  assert(phase1_map->width == 3);
  assert(phase1_map->height == 2);
  expect_cell(phase1_map->cell(0, 0), 2, 3, 4, 5, 6, 7, 8, 1, 9);
  expect_cell(phase1_map->cell(0, 1), 0x8001U, 20, 30, 0, 0, 0, 0, 0, 0);
  expect_cell(phase1_map->cell(1, 0), 40, 50, 0x8002U, 0, 0, 0, 0, 0, 0);
  expect_cell(phase1_map->cell(1, 1), 60, 70, 80, 0x80U | 3U, 0, 9, 10, 2, 11);
  expect_cell(phase1_map->cell(2, 0), 90, 100, 110, 0x80U | 3U, 0x80U, 12, 13, 3, 14);
  expect_cell(phase1_map->cell(2, 1), 120, 130, 140, 4, 5, 15, 16, 6, 17);
  assert(phase1_map->cell(-1, 0) == nullptr);
  assert(phase1_map->cell(3, 0) == nullptr);
  assert(phase1_map->can_move(0, 0));
  assert(!phase1_map->can_move(1, 0));
  assert(!phase1_map->can_move(0, 1));
  assert(!phase1_map->can_move(1, 1));
  assert(phase1_map->can_move(2, 0));
  assert(phase1_map->can_move(2, 1));

  const auto snake_map = temp_assets.load_map("SNAKE");
  assert(snake_map != nullptr);
  assert(snake_map->width == 2);
  assert(snake_map->height == 3);
  expect_cell(snake_map->cell(0, 0), 11, 12, 13, 14, 15, 16, 17, 1, 18);
  expect_cell(snake_map->cell(0, 1), 21, 22, 23, 24, 25, 26, 27, 2, 28);
  expect_cell(snake_map->cell(0, 2), 31, 32, 0x8001U, 34, 35, 36, 37, 3, 38);
  expect_cell(snake_map->cell(1, 0), 41, 42, 43, 44, 45, 46, 47, 4, 48);
  expect_cell(snake_map->cell(1, 1), 51, 52, 53, 0x80U | 5U, 0, 56, 57, 5, 58);
  expect_cell(snake_map->cell(1, 2), 61, 62, 63, 0x80U | 5U, 0x80U, 66, 67, 6, 68);
  assert(!snake_map->can_move(0, 2));
  assert(!snake_map->can_move(1, 1));
  assert(snake_map->can_move(1, 2));

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
  object_cell.fr_img = 0x8064U;
  object_cell.door_index = 0;
  object_cell.door_offset = 0;
  assert(legacy_map_object_frame(object_cell, 0) == 99);
  object_cell.door_index = 1;
  object_cell.door_offset = 0x02U;
  assert(legacy_map_object_frame(object_cell, 0) == 99);
  object_cell.door_index = 0;
  object_cell.door_offset = 0x82U;
  assert(legacy_map_object_frame(object_cell, 0) == 99);

  if (const auto real_root = optional_asset_root(); !real_root.empty()) {
    AssetManager assets;
    assert(assets.initialize(real_root));
    const auto map0 = assets.load_map("0");
    const auto map3 = assets.load_map("3");
    assert(map0 != nullptr);
    assert(map3 != nullptr);
    assert(map0->width > 0);
    assert(map0->height > 0);
    assert(map3->width > 0);
    assert(map3->height > 0);
    assert(map0->cell(0, 0) != nullptr);
    assert(map3->cell(0, 0) != nullptr);
    assert(first_decodable_frame(assets, ArchiveId::tiles, 32) != nullptr);
    assert(first_decodable_frame(assets, ArchiveId::sm_tiles, 128) != nullptr);
    assert(first_decodable_frame(assets, ArchiveId::objects1, 512) != nullptr);
    assert(first_decodable_frame(assets, ArchiveId::dn_items, 4096) != nullptr);
    assert(first_decodable_frame(assets, ArchiveId::effect, 512) != nullptr);
    assert(assets.get_frame(ArchiveId::tiles, 9999999) == nullptr);
    assert(assets.get_frame(ArchiveId::tiles, 9999999) == nullptr);
    std::cout << "real_maps=0:" << map0->width << "x" << map0->height
              << " 3:" << map3->width << "x" << map3->height << '\n';
  } else {
    std::cout << "real_asset_root=skip set MIR2_ASSET_ROOT to enable real map/resource checks\n";
  }

  std::cout << "phase1_map=" << phase1_map->width << "x" << phase1_map->height
            << " snake_map=" << snake_map->width << "x" << snake_map->height << '\n';
  return 0;
}
