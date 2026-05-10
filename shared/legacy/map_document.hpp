// ============================================================
// Mir2 旧版地图文档结构
// 职责：定义 .map 文件的内存表示（MapCell 和 MapDocument），
//       提供文件解码函数和碰撞检测
// 文件格式：52 字节头部 + 12 字节/单元格，列优先存储
// ============================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

namespace mir2::legacy {

/// 地图单元格：对应 .map 文件中 12 字节的数据单元
struct MapCell {
  std::uint16_t bk_img{0};     ///< 背景图索引（高 bit 为阻挡标志）
  std::uint16_t mid_img{0};    ///< 中间层图索引
  std::uint16_t fr_img{0};     ///< 前景图索引（高 bit 为阻挡标志）
  std::uint8_t door_index{0};  ///< 门索引
  std::uint8_t door_offset{0}; ///< 门偏移量
  std::uint8_t ani_frame{0};   ///< 动画帧序号（高位为混合标志）
  std::uint8_t ani_tick{0};    ///< 动画速度
  std::uint8_t area{0};        ///< 区域编号（选择对应的 Objects 归档）
  std::uint8_t light{0};       ///< 光照值
};

/// 地图文档：从 .map 文件解析得到的地图数据
struct MapDocument {
  int width{0};                ///< 地图宽度（瓦片数）
  int height{0};               ///< 地图高度（瓦片数）
  std::vector<MapCell> cells{};///< 单元格数组（行优先存储）

  /// 获取单元格指针，越界返回 nullptr
  [[nodiscard]] const MapCell* cell(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) {
      return nullptr;
    }
    return &cells[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                  static_cast<std::size_t>(x)];
  }

  /// 碰撞检测：检查 bk_img 和 fr_img 的高 bit（0x8000）是否未设置
  /// 高位为 0 表示可通过，为 1 表示阻挡
  [[nodiscard]] bool can_move(int x, int y) const {
    const auto* target = cell(x, y);
    if (target == nullptr) {
      return false;
    }
    if ((target->bk_img & 0x8000U) != 0U || (target->fr_img & 0x8000U) != 0U) {
      return false;
    }
    return ((target->door_index & 0x80U) == 0U) || ((target->door_offset & 0x80U) != 0U);
  }
};

namespace detail {

constexpr std::size_t kMapHeaderSize = 52;  ///< .map 文件头部大小
constexpr std::size_t kMapCellSize = 12;    ///< 每个单元格字节数

/// 小端读取 uint16
inline std::uint16_t read_u16(const std::vector<std::uint8_t>& buffer, std::size_t offset) {
  return static_cast<std::uint16_t>(buffer[offset]) |
         (static_cast<std::uint16_t>(buffer[offset + 1]) << 8U);
}

/// 读取文件全部字节到 vector
inline std::vector<std::uint8_t> read_file_bytes(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  file.seekg(0, std::ios::end);
  const auto size = file.tellg();
  if (size <= 0) {
    return {};
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  file.seekg(0, std::ios::beg);
  file.read(reinterpret_cast<char*>(bytes.data()), size);
  return bytes;
}

}  // namespace detail

/// 从 .map 文件解码地图文档
/// 列优先存储（先遍历 X，再遍历 Y）
inline std::shared_ptr<MapDocument> decode_map_file(const std::filesystem::path& path) {
  if (path.empty() || !std::filesystem::exists(path)) {
    return nullptr;
  }

  const auto bytes = detail::read_file_bytes(path);
  if (bytes.size() < detail::kMapHeaderSize) {
    return nullptr;
  }

  auto map = std::make_shared<MapDocument>();
  map->width = static_cast<int>(detail::read_u16(bytes, 0));
  map->height = static_cast<int>(detail::read_u16(bytes, 2));
  if (map->width <= 0 || map->height <= 0) {
    return nullptr;
  }

  const auto expected_size = detail::kMapHeaderSize +
      static_cast<std::size_t>(map->width) * static_cast<std::size_t>(map->height) *
          detail::kMapCellSize;
  if (bytes.size() < expected_size) {
    return nullptr;
  }

  map->cells.resize(static_cast<std::size_t>(map->width) * static_cast<std::size_t>(map->height));
  for (int x = 0; x < map->width; ++x) {
    for (int y = 0; y < map->height; ++y) {
      const auto source = detail::kMapHeaderSize +
          (static_cast<std::size_t>(x) * static_cast<std::size_t>(map->height) +
           static_cast<std::size_t>(y)) * detail::kMapCellSize;
      auto& cell = map->cells[static_cast<std::size_t>(y) *
                                  static_cast<std::size_t>(map->width) +
                              static_cast<std::size_t>(x)];
      cell.bk_img = detail::read_u16(bytes, source);
      cell.mid_img = detail::read_u16(bytes, source + 2U);
      cell.fr_img = detail::read_u16(bytes, source + 4U);
      cell.door_index = bytes[source + 6U];
      cell.door_offset = bytes[source + 7U];
      cell.ani_frame = bytes[source + 8U];
      cell.ani_tick = bytes[source + 9U];
      cell.area = bytes[source + 10U];
      cell.light = bytes[source + 11U];
    }
  }
  return map;
}

}  // namespace mir2::legacy
