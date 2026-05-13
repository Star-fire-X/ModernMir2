#include "shared/legacy/map_document.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

void write_u16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
  bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void write_cell(std::vector<std::uint8_t>& bytes, std::size_t offset,
                mir2::legacy::MapCell cell, std::uint16_t key = 0) {
  write_u16(bytes, offset, cell.bk_img ^ key);
  write_u16(bytes, offset + 2U, cell.mid_img ^ key);
  write_u16(bytes, offset + 4U, cell.fr_img ^ key);
  bytes[offset + 6U] = cell.door_index;
  bytes[offset + 7U] = cell.door_offset;
  bytes[offset + 8U] = cell.ani_frame;
  bytes[offset + 9U] = cell.ani_tick;
  bytes[offset + 10U] = cell.area;
  bytes[offset + 11U] = cell.light;
}

void write_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
}

std::filesystem::path temp_dir() {
  auto path = std::filesystem::temp_directory_path() / "mir2_legacy_map_document_smoke";
  std::filesystem::create_directories(path);
  return path;
}

}  // namespace

int main() {
  const auto root = temp_dir();

  {
    constexpr int width = 2;
    constexpr int height = 2;
    std::vector<std::uint8_t> bytes(
        mir2::legacy::detail::kMapHeaderSize +
        width * height * mir2::legacy::detail::kMapCellSize);
    write_u16(bytes, 0, width);
    write_u16(bytes, 2, height);

    write_cell(bytes, mir2::legacy::detail::kMapHeaderSize + 0U,
               mir2::legacy::MapCell{1, 2, 3, 0, 0, 4, 5, 6, 7});
    write_cell(bytes, mir2::legacy::detail::kMapHeaderSize + 12U,
               mir2::legacy::MapCell{0x8001U, 20, 30, 0, 0, 0, 0, 0, 0});
    write_cell(bytes, mir2::legacy::detail::kMapHeaderSize + 24U,
               mir2::legacy::MapCell{100, 200, 300, 0x80U | 3U, 0, 0, 0, 0, 0});
    write_cell(bytes, mir2::legacy::detail::kMapHeaderSize + 36U,
               mir2::legacy::MapCell{101, 201, 301, 0x80U | 3U, 0x80U, 0, 0, 0, 0});

    const auto path = root / "0.map";
    write_file(path, bytes);
    const auto map = mir2::legacy::decode_map_file(path);
    assert(map != nullptr);
    assert(!map->anti_hack);
    assert(map->check_key == 0);
    assert(map->width == width);
    assert(map->height == height);
    assert(map->cell(0, 0)->bk_img == 1);
    assert(map->cell(0, 1)->bk_img == 0x8001U);
    assert(map->cell(1, 0)->fr_img == 300);
    assert(map->cell(1, 1)->door_offset == 0x80U);
    assert(map->can_move(0, 0));
    assert(!map->can_move(0, 1));
    assert(!map->can_move(1, 0));
    assert(map->can_move(1, 1));
    assert(map->terrain_can_move(1, 0));
    assert(mir2::legacy::MapDocument::door_blocks_move(*map->cell(1, 0)));
    assert(!map->can_fly(1, 0));
    assert(map->can_fly(1, 1));
  }

  {
    constexpr int width = 2;
    constexpr int height = 1;
    constexpr std::uint16_t key = 0xAA55U;
    std::vector<std::uint8_t> bytes(
        mir2::legacy::detail::kAntiHackMapHeaderSize +
        width * height * mir2::legacy::detail::kMapCellSize);
    write_u16(bytes, 31U, width ^ key);
    write_u16(bytes, 33U, key);
    write_u16(bytes, 35U, height ^ key);

    write_cell(bytes, mir2::legacy::detail::kAntiHackMapHeaderSize + 0U,
               mir2::legacy::MapCell{11, 12, 13, 0, 0, 14, 15, 16, 17}, key);
    write_cell(bytes, mir2::legacy::detail::kAntiHackMapHeaderSize + 12U,
               mir2::legacy::MapCell{21, 22, 0x8001U, 0, 0, 0, 0, 0, 0}, key);

    const auto path = root / "SNAKE.map";
    write_file(path, bytes);
    const auto map = mir2::legacy::decode_map_file(path);
    assert(map != nullptr);
    assert(map->anti_hack);
    assert(map->check_key == key);
    assert(map->width == width);
    assert(map->height == height);
    assert(map->cell(0, 0)->bk_img == 11);
    assert(map->cell(0, 0)->mid_img == 12);
    assert(map->cell(0, 0)->fr_img == 13);
    assert(map->cell(1, 0)->fr_img == 0x8001U);
    assert(!map->can_move(1, 0));
  }

  {
    const auto path = root / "short.map";
    write_file(path, std::vector<std::uint8_t>(10));
    assert(mir2::legacy::decode_map_file(path) == nullptr);
  }

  return 0;
}
