#include "shared/legacy/map_document.hpp"

#include <cassert>
#include <cstddef>
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

std::size_t cell_offset(std::size_t header_size, int height, int x, int y) {
  return header_size +
      (static_cast<std::size_t>(x) * static_cast<std::size_t>(height) +
       static_cast<std::size_t>(y)) *
          mir2::legacy::detail::kMapCellSize;
}

void expect_cell(const mir2::legacy::MapCell* cell, std::uint16_t bk_img,
                 std::uint16_t mid_img, std::uint16_t fr_img, std::uint8_t door_index,
                 std::uint8_t door_offset, std::uint8_t ani_frame,
                 std::uint8_t ani_tick, std::uint8_t area, std::uint8_t light) {
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
    constexpr int width = 3;
    constexpr int height = 2;
    std::vector<std::uint8_t> bytes(
        mir2::legacy::detail::kMapHeaderSize +
        width * height * mir2::legacy::detail::kMapCellSize);
    write_u16(bytes, 0, width);
    write_u16(bytes, 2, height);

    write_cell(bytes, cell_offset(mir2::legacy::detail::kMapHeaderSize, height, 0, 0),
               mir2::legacy::MapCell{1, 2, 3, 4, 5, 6, 7, 1, 8});
    write_cell(bytes, cell_offset(mir2::legacy::detail::kMapHeaderSize, height, 0, 1),
               mir2::legacy::MapCell{0x8001U, 20, 30, 0, 0, 0, 0, 0, 0});
    write_cell(bytes, cell_offset(mir2::legacy::detail::kMapHeaderSize, height, 1, 0),
               mir2::legacy::MapCell{40, 50, 0x8002U, 0, 0, 0, 0, 0, 0});
    write_cell(bytes, cell_offset(mir2::legacy::detail::kMapHeaderSize, height, 1, 1),
               mir2::legacy::MapCell{60, 70, 80, 0x80U | 3U, 0, 9, 10, 2, 11});
    write_cell(bytes, cell_offset(mir2::legacy::detail::kMapHeaderSize, height, 2, 0),
               mir2::legacy::MapCell{90, 100, 110, 0x80U | 3U, 0x80U, 12, 13, 3, 14});
    write_cell(bytes, cell_offset(mir2::legacy::detail::kMapHeaderSize, height, 2, 1),
               mir2::legacy::MapCell{120, 130, 140, 4, 5, 15, 16, 6, 17});

    const auto path = root / "0.map";
    write_file(path, bytes);
    const auto map = mir2::legacy::decode_map_file(path);
    assert(map != nullptr);
    assert(!map->anti_hack);
    assert(map->check_key == 0);
    assert(map->width == width);
    assert(map->height == height);
    expect_cell(map->cell(0, 0), 1, 2, 3, 4, 5, 6, 7, 1, 8);
    expect_cell(map->cell(0, 1), 0x8001U, 20, 30, 0, 0, 0, 0, 0, 0);
    expect_cell(map->cell(1, 0), 40, 50, 0x8002U, 0, 0, 0, 0, 0, 0);
    expect_cell(map->cell(1, 1), 60, 70, 80, 0x80U | 3U, 0, 9, 10, 2, 11);
    expect_cell(map->cell(2, 0), 90, 100, 110, 0x80U | 3U, 0x80U, 12, 13, 3, 14);
    expect_cell(map->cell(2, 1), 120, 130, 140, 4, 5, 15, 16, 6, 17);
    assert(map->cell(-1, 0) == nullptr);
    assert(map->cell(3, 0) == nullptr);
    assert(map->can_move(0, 0));
    assert(!map->can_move(0, 1));
    assert(!map->can_move(1, 0));
    assert(!map->can_move(1, 1));
    assert(map->can_move(2, 0));
    assert(map->can_move(2, 1));
    assert(map->terrain_can_move(1, 1));
    assert(mir2::legacy::MapDocument::door_blocks_move(*map->cell(1, 1)));
    assert(!map->can_fly(1, 0));
    assert(!map->can_fly(1, 1));
    assert(map->can_fly(2, 0));
  }

  {
    constexpr int width = 2;
    constexpr int height = 3;
    constexpr std::uint16_t key = 0xAA55U;
    std::vector<std::uint8_t> bytes(
        mir2::legacy::detail::kAntiHackMapHeaderSize +
        width * height * mir2::legacy::detail::kMapCellSize);
    write_u16(bytes, 31U, width ^ key);
    write_u16(bytes, 33U, key);
    write_u16(bytes, 35U, height ^ key);

    write_cell(bytes, cell_offset(mir2::legacy::detail::kAntiHackMapHeaderSize, height, 0, 0),
               mir2::legacy::MapCell{11, 12, 13, 14, 15, 16, 17, 1, 18}, key);
    write_cell(bytes, cell_offset(mir2::legacy::detail::kAntiHackMapHeaderSize, height, 0, 1),
               mir2::legacy::MapCell{21, 22, 23, 24, 25, 26, 27, 2, 28}, key);
    write_cell(bytes, cell_offset(mir2::legacy::detail::kAntiHackMapHeaderSize, height, 0, 2),
               mir2::legacy::MapCell{31, 32, 0x8001U, 34, 35, 36, 37, 3, 38}, key);
    write_cell(bytes, cell_offset(mir2::legacy::detail::kAntiHackMapHeaderSize, height, 1, 0),
               mir2::legacy::MapCell{41, 42, 43, 44, 45, 46, 47, 4, 48}, key);
    write_cell(bytes, cell_offset(mir2::legacy::detail::kAntiHackMapHeaderSize, height, 1, 1),
               mir2::legacy::MapCell{51, 52, 53, 0x80U | 5U, 0, 56, 57, 5, 58}, key);
    write_cell(bytes, cell_offset(mir2::legacy::detail::kAntiHackMapHeaderSize, height, 1, 2),
               mir2::legacy::MapCell{61, 62, 63, 0x80U | 5U, 0x80U, 66, 67, 6, 68}, key);

    const auto path = root / "SNAKE.map";
    write_file(path, bytes);
    const auto map = mir2::legacy::decode_map_file(path);
    assert(map != nullptr);
    assert(map->anti_hack);
    assert(map->check_key == key);
    assert(map->width == width);
    assert(map->height == height);
    expect_cell(map->cell(0, 0), 11, 12, 13, 14, 15, 16, 17, 1, 18);
    expect_cell(map->cell(0, 1), 21, 22, 23, 24, 25, 26, 27, 2, 28);
    expect_cell(map->cell(0, 2), 31, 32, 0x8001U, 34, 35, 36, 37, 3, 38);
    expect_cell(map->cell(1, 0), 41, 42, 43, 44, 45, 46, 47, 4, 48);
    expect_cell(map->cell(1, 1), 51, 52, 53, 0x80U | 5U, 0, 56, 57, 5, 58);
    expect_cell(map->cell(1, 2), 61, 62, 63, 0x80U | 5U, 0x80U, 66, 67, 6, 68);
    assert(!map->can_move(0, 2));
    assert(!map->can_move(1, 1));
    assert(map->can_move(1, 2));
  }

  {
    const auto path = root / "short.map";
    write_file(path, std::vector<std::uint8_t>(10));
    assert(mir2::legacy::decode_map_file(path) == nullptr);
  }

  return 0;
}
