// ============================================================
// Mir2 现代客户端 — 资源管理器实现
// 职责：WIL/WIX 精灵归档的加载与解码、.map 地图文件解析、
//       精灵帧和地图文档的缓存管理
//
// WIL 解码说明：
// WIL 格式支持多种位深度：8 位（调色板）、16 位（RGB 565）、
// 24 位（RGB）、32 位（BGRA）。本实现按位深度分离解码逻辑。
// 所有解码路径都会将图像上下翻转，因为经典传奇客户端的
// 精灵以倒置 Y 轴方向存储（与 DirectDraw 表面格式兼容）。
//
// 透明色处理：
// - 调色板模式：RGB(0,0,0) 映射为 Alpha=0（透明）
// - 16 位模式：color(0,0,0) 映射为 Alpha=0
// - 32 位模式：直接使用 Alpha 通道
// ============================================================

#include "assets/asset_manager.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace mir2::client {

namespace {

// WIL/WIX/Map 文件格式常量
// 这些偏移值基于对经典传奇客户端资源文件的反向分析
constexpr std::size_t kWilHeaderSize = 60;     ///< WIL 文件头大小
constexpr std::size_t kWilTitleBytes = 40;     ///< WIL 标题字符串长度
constexpr std::size_t kWixHeaderSize = 48;     ///< WIX 索引文件头大小
constexpr std::size_t kMapHeaderSize = 52;     ///< 地图文件头大小
constexpr int kMapCellSize = 12;               ///< 每个地图单元格 12 字节

// ---- 小端字节序读取工具函数 ----
// WIL/WIX/Map 文件均使用小端字节序
// 这些函数从字节数组中按小端序读取各类型数值

std::uint16_t read_u16(const std::vector<std::uint8_t>& buffer, std::size_t offset) {
  return static_cast<std::uint16_t>(buffer[offset]) |
         (static_cast<std::uint16_t>(buffer[offset + 1]) << 8U);
}

std::int16_t read_i16(const std::vector<std::uint8_t>& buffer, std::size_t offset) {
  return static_cast<std::int16_t>(read_u16(buffer, offset));
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& buffer, std::size_t offset) {
  return static_cast<std::uint32_t>(buffer[offset]) |
         (static_cast<std::uint32_t>(buffer[offset + 1]) << 8U) |
         (static_cast<std::uint32_t>(buffer[offset + 2]) << 16U) |
         (static_cast<std::uint32_t>(buffer[offset + 3]) << 24U);
}

// 将调色板颜色（BGR 各 8 位）转换为 32 位 BGRA 像素
// 经典传奇客户端约定：纯黑色（0,0,0）为透明色
// 故当 R=G=B=0 时返回 Alpha=0，否则 Alpha=255
std::uint32_t rgba_from_palette(const std::uint8_t blue, const std::uint8_t green,
                                const std::uint8_t red) {
  const auto alpha =
      (red == 0 && green == 0 && blue == 0) ? 0U : 0xFF000000U;
  return alpha | (static_cast<std::uint32_t>(red) << 16U) |
         (static_cast<std::uint32_t>(green) << 8U) | blue;
}

// 将 16 位 RGB 565 格式转换为 32 位 BGRA
// 黑色（565 值为 0）保持透明（Alpha=0）
// 通过 5/6 位到 8 位的缩放：value * 255 / max
std::uint32_t rgba_from_565(const std::uint16_t color) {
  if (color == 0) {
    return 0U;  // 黑色透明
  }
  const auto red = static_cast<std::uint8_t>(((color >> 11U) & 0x1FU) * 255U / 31U);
  const auto green = static_cast<std::uint8_t>(((color >> 5U) & 0x3FU) * 255U / 63U);
  const auto blue = static_cast<std::uint8_t>((color & 0x1FU) * 255U / 31U);
  return 0xFF000000U | (static_cast<std::uint32_t>(red) << 16U) |
         (static_cast<std::uint32_t>(green) << 8U) | blue;
}

// 生成帧缓存键：将 ArchiveId（高 32 位）与 index（低 32 位）组合
std::uint64_t make_frame_key(const ArchiveId archive_id, const int index) {
  return (static_cast<std::uint64_t>(archive_id) << 32U) |
         static_cast<std::uint32_t>(index);
}

// 根据颜色数返回对应的位深度
// WIL 头部中的 color_count 字段决定了图像的存储格式
int bit_count_from_color_count(const std::uint32_t color_count) {
  switch (color_count) {
    case 256:
      return 8;      // 8 位调色板索引
    case 65536:
      return 16;     // 16 位 RGB 565
    case 16777216:
      return 24;     // 24 位 RGB（无 Alpha）
    default:
      return 32;     // 32 位 BGRA（含 Alpha）
  }
}

// 读取整个文件到字节数组（二进制模式）
// 用于将 WIL/WIX/Map 文件一次性加载到内存中
std::vector<std::uint8_t> read_file_bytes(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("failed_to_open_file");
  }
  stream.seekg(0, std::ios::end);
  const auto size = static_cast<std::size_t>(stream.tellg());
  stream.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> bytes(size);
  if (size > 0) {
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
  }
  return bytes;
}

}  // namespace

// ---- WilArchive 内部结构 ----
// 每个 WilArchive 对应一个已加载的 .wil + .wix 文件对
// 在 require_archive() 中加载并解析
struct AssetManager::WilArchive {
  std::filesystem::path data_path{};       ///< .wil 文件路径
  std::vector<std::uint8_t> file_bytes{};  ///< 完整文件数据（读取到内存）
  std::vector<std::uint32_t> offsets{};    ///< 帧偏移表（从 .wix 索引文件加载）
  std::array<std::uint32_t, 256> palette{};  ///< 调色板（仅 8 位模式使用）
  int image_count{0};                       ///< 图片总数
  int bit_count{8};                         ///< 位深度：8/16/24/32
  bool legacy_variant{false};               ///< 旧版格式标志（影响帧头和偏移计算）
  std::size_t palette_offset{kWilHeaderSize};    ///< 调色板在文件中的偏移
  std::size_t frame_header_size{8U};             ///< 每帧头部大小（8 或 12 字节）
  std::size_t index_header_size{kWixHeaderSize}; ///< 索引文件头部大小
};

AssetManager::~AssetManager() = default;

// 初始化资源管理器：设置资源根目录并验证关键目录的存在性
bool AssetManager::initialize(const std::filesystem::path& root_path) {
  root_path_ = root_path;
  // 清空所有缓存，确保重新加载
  archives_.clear();
  failed_archives_.clear();
  frame_cache_.clear();
  missing_frame_cache_.clear();
  map_cache_.clear();
  // 验证资源目录结构
  return std::filesystem::exists(root_path_ / "Data") &&
         std::filesystem::exists(root_path_ / "Map");
}

// 释放场景资源：切换场景时清空地图缓存（地图数据体积较大）
// 精灵帧缓存保留，因为 WIL 归档在场景间共享
void AssetManager::release_scene_assets() { map_cache_.clear(); }

// 获取精灵帧：先查缓存，未命中则解码并缓存
// 如果归档已标记为加载失败，直接返回 nullptr 避免重复尝试
std::shared_ptr<const SpriteFrame> AssetManager::get_frame(const ArchiveId archive_id,
                                                           const int index) {
  if (index < 0) {
    return nullptr;
  }

  const auto key = make_frame_key(archive_id, index);
  if (const auto cached = frame_cache_.find(key); cached != frame_cache_.end()) {
    return cached->second;
  }
  if (missing_frame_cache_.find(key) != missing_frame_cache_.end()) {
    return nullptr;
  }

  // 如果归档已经确认加载失败，直接返回 nullptr
  // 避免每帧都尝试加载同一个不存在的文件
  const auto archive_key = static_cast<int>(archive_id);
  if (failed_archives_.find(archive_key) != failed_archives_.end()) {
    return nullptr;
  }

  WilArchive* archive = nullptr;
  try {
    archive = &require_archive(archive_id);
  } catch (const std::exception&) {
    failed_archives_[archive_key] = true;
    return nullptr;
  }

  std::shared_ptr<SpriteFrame> frame;
  try {
    frame = decode_wil_frame(*archive, index);
  } catch (const std::exception&) {
    missing_frame_cache_[key] = true;
    return nullptr;
  }
  if (frame == nullptr || frame->empty()) {
    missing_frame_cache_[key] = true;
    return nullptr;
  }
  return frame_cache_.emplace(key, std::move(frame)).first->second;
}

// 加载地图：先查缓存，未命中则解码并缓存
std::shared_ptr<const MapDocument> AssetManager::load_map(const std::string& map_id) {
  if (const auto cached = map_cache_.find(map_id); cached != map_cache_.end()) {
    return cached->second;
  }
  auto map = decode_map(map_id);
  if (map == nullptr) {
    return nullptr;
  }
  return map_cache_.emplace(map_id, std::move(map)).first->second;
}

// 获取或加载归档：先查缓存，未命中则从 .wil/.wix 文件加载
// 加载过程：
//   1. 读取 .wil 文件到内存
//   2. 解析头部获取图片数量、颜色数、版本标志
//   3. 读取调色板（8 位模式）
//   4. 读取 .wix 索引文件获取帧偏移表
AssetManager::WilArchive& AssetManager::require_archive(const ArchiveId archive_id) {
  const auto key = static_cast<int>(archive_id);
  if (const auto found = archives_.find(key); found != archives_.end()) {
    return *found->second;
  }

  auto archive = std::make_shared<WilArchive>();
  archive->data_path = archive_path(archive_id);
  archive->file_bytes = read_file_bytes(archive->data_path);
  if (archive->file_bytes.size() < kWilHeaderSize) {
    throw std::runtime_error("wil_header_too_small");
  }

  // 解析 WIL 头部各字段：
  // 偏移 44：图片数量
  // 偏移 48：颜色数（决定位深度）
  // 偏移 56：版本标志（影响帧头大小和格式变体）
  archive->image_count = static_cast<int>(read_u32(archive->file_bytes, 44));
  const auto color_count = read_u32(archive->file_bytes, 48);
  const auto ver_flag = read_u32(archive->file_bytes, 56);
  archive->bit_count = bit_count_from_color_count(color_count);
  // 旧版格式检测：版本标志为 0 或颜色数为 65536 时使用旧版偏置
  archive->legacy_variant = ver_flag == 0 || color_count == 65536;
  archive->palette_offset = archive->legacy_variant ? (kWilHeaderSize - 4U) : kWilHeaderSize;
  archive->frame_header_size = archive->legacy_variant ? 8U : 12U;
  archive->index_header_size = archive->legacy_variant ? (kWixHeaderSize) : (kWixHeaderSize + 4U);

  // 读取调色板（256 色，每色 4 字节 BGRA）
  for (std::size_t index = 0; index < archive->palette.size(); ++index) {
    const auto palette_offset = archive->palette_offset + index * 4U;
    archive->palette[index] = rgba_from_palette(archive->file_bytes[palette_offset],
                                                archive->file_bytes[palette_offset + 1],
                                                archive->file_bytes[palette_offset + 2]);
  }

  // 读取对应的 .wix 索引文件
  // WIX 文件包含每帧在 WIL 文件中的起始偏移
  auto index_path = archive->data_path;
  index_path.replace_extension(".WIX");
  // 尝试大小写兼容（部分旧资源使用小写扩展名）
  if (!std::filesystem::exists(index_path)) {
    index_path.replace_extension(".wix");
  }
  const auto index_bytes = read_file_bytes(index_path);
  if (index_bytes.size() < archive->index_header_size) {
    throw std::runtime_error("wix_header_too_small");
  }
  const auto index_count = static_cast<int>(read_u32(index_bytes, 44));
  archive->offsets.resize(static_cast<std::size_t>(index_count));
  for (int index = 0; index < index_count; ++index) {
    archive->offsets[static_cast<std::size_t>(index)] =
        read_u32(index_bytes, archive->index_header_size + static_cast<std::size_t>(index) * 4U);
  }

  return *archives_.emplace(key, std::move(archive)).first->second;
}

// 解码 WIL 帧：根据位深度选择对应的解码路径
// 所有解码路径都会将图像上下翻转（经典客户端中精灵为倒置存储，
// 因为 DirectDraw 表面使用自上而下的坐标系统）
//
// 帧头结构（8 字节旧版 / 12 字节新版）：
//   +0: width  (int16)
//   +2: height (int16)
//   +4: hotspot_x (int16)
//   +6: hotspot_y (int16)
//   +8: [新版额外 4 字节，用途未知]
std::shared_ptr<SpriteFrame> AssetManager::decode_wil_frame(WilArchive& archive,
                                                            const int index) {
  if (index < 0 || index >= static_cast<int>(archive.offsets.size())) {
    return nullptr;
  }

  const auto offset = static_cast<std::size_t>(archive.offsets[static_cast<std::size_t>(index)]);
  if (offset == 0 || offset + archive.frame_header_size > archive.file_bytes.size()) {
    return nullptr;
  }

  auto frame = std::make_shared<SpriteFrame>();
  frame->width = read_i16(archive.file_bytes, offset);
  frame->height = read_i16(archive.file_bytes, offset + 2U);
  frame->hotspot_x = read_i16(archive.file_bytes, offset + 4U);
  frame->hotspot_y = read_i16(archive.file_bytes, offset + 6U);
  if (frame->width <= 0 || frame->height <= 0) {
    return frame;  // 空帧：有效但无像素数据
  }

  const auto pixel_offset = offset + archive.frame_header_size;
  const auto pixel_count = static_cast<std::size_t>(frame->width) *
                           static_cast<std::size_t>(frame->height);
  frame->pixels.resize(pixel_count, 0U);

  // === 8 位调色板模式 ===
  // 每像素 1 字节调色板索引，行按 4 字节对齐
  if (archive.bit_count == 8) {
    const auto row_stride = static_cast<std::size_t>((frame->width + 3) & ~3);  // 4 字节对齐
    const auto data_size = row_stride * static_cast<std::size_t>(frame->height);
    if (pixel_offset + data_size > archive.file_bytes.size()) {
      return nullptr;
    }

    for (int y = 0; y < frame->height; ++y) {
      const auto row_offset = pixel_offset + static_cast<std::size_t>(y) * row_stride;
      const auto dest_y = frame->height - 1 - y;  // 翻转 Y 轴
      for (int x = 0; x < frame->width; ++x) {
        const auto palette_index = archive.file_bytes[row_offset + static_cast<std::size_t>(x)];
        frame->pixels[static_cast<std::size_t>(dest_y) * static_cast<std::size_t>(frame->width) +
                      static_cast<std::size_t>(x)] = archive.palette[palette_index];
      }
    }
    return frame;
  }

  // === 16 位 RGB 565 模式 ===
  // 每像素 2 字节，RGB 各 5/6/5 位
  if (archive.bit_count == 16) {
    const auto data_size = pixel_count * sizeof(std::uint16_t);
    if (pixel_offset + data_size > archive.file_bytes.size()) {
      return nullptr;
    }

    for (int y = 0; y < frame->height; ++y) {
      const auto dest_y = frame->height - 1 - y;
      for (int x = 0; x < frame->width; ++x) {
        const auto pixel = static_cast<std::size_t>(y) * static_cast<std::size_t>(frame->width) +
                           static_cast<std::size_t>(x);
        const auto color = read_u16(archive.file_bytes, pixel_offset + pixel * 2U);
        frame->pixels[static_cast<std::size_t>(dest_y) * static_cast<std::size_t>(frame->width) +
                      static_cast<std::size_t>(x)] = rgba_from_565(color);
      }
    }
    return frame;
  }

  // === 24 位 RGB 模式 ===
  // 每像素 3 字节 RGB，无 Alpha，黑色(0,0,0)透明
  if (archive.bit_count == 24) {
    const auto data_size = pixel_count * 3U;
    if (pixel_offset + data_size > archive.file_bytes.size()) {
      return nullptr;
    }
    for (int y = 0; y < frame->height; ++y) {
      const auto dest_y = frame->height - 1 - y;
      for (int x = 0; x < frame->width; ++x) {
        const auto pixel = static_cast<std::size_t>(y) * static_cast<std::size_t>(frame->width) +
                           static_cast<std::size_t>(x);
        const auto source = pixel_offset + pixel * 3U;
        frame->pixels[static_cast<std::size_t>(dest_y) * static_cast<std::size_t>(frame->width) +
                      static_cast<std::size_t>(x)] =
            rgba_from_palette(archive.file_bytes[source], archive.file_bytes[source + 1],
                              archive.file_bytes[source + 2]);
      }
    }
    return frame;
  }

  // === 32 位 BGRA 模式（默认/后备） ===
  // 每像素 4 字节：B, G, R, A，直接使用 Alpha 通道
  const auto data_size = pixel_count * 4U;
  if (pixel_offset + data_size > archive.file_bytes.size()) {
    return nullptr;
  }
  for (int y = 0; y < frame->height; ++y) {
    const auto dest_y = frame->height - 1 - y;
    for (int x = 0; x < frame->width; ++x) {
      const auto pixel = static_cast<std::size_t>(y) * static_cast<std::size_t>(frame->width) +
                         static_cast<std::size_t>(x);
      const auto source = pixel_offset + pixel * 4U;
      const auto blue = archive.file_bytes[source];
      const auto green = archive.file_bytes[source + 1];
      const auto red = archive.file_bytes[source + 2];
      const auto alpha = archive.file_bytes[source + 3];
      frame->pixels[static_cast<std::size_t>(dest_y) * static_cast<std::size_t>(frame->width) +
                    static_cast<std::size_t>(x)] =
          (static_cast<std::uint32_t>(alpha) << 24U) |
          (static_cast<std::uint32_t>(red) << 16U) |
          (static_cast<std::uint32_t>(green) << 8U) | blue;
    }
  }
  return frame;
}

// 解码 .map 地图文件
// 文件结构：52 字节头部 + width * height * 12 字节单元格数据
// 存储顺序：列优先（外层循环 X，内层循环 Y）
// 与经典传奇客户端的渲染引擎读取方式一致
//
// 地图单元格 12 字节布局：
//   +0-1: bk_img（背景图索引，bit15 为阻挡标记）
//   +2-3: mid_img（中间层图索引）
//   +4-5: fr_img（前景图索引，bit15 为阻挡标记）
//   +6:   door_index
//   +7:   door_offset
//   +8:   ani_frame
//   +9:   ani_tick
//   +10:  area
//   +11:  light
std::shared_ptr<MapDocument> AssetManager::decode_map(const std::string& map_id) const {
  const auto map_path = root_path_ / "Map" / (map_id + ".map");
  if (!std::filesystem::exists(map_path)) {
    return nullptr;
  }

  const auto bytes = read_file_bytes(map_path);
  if (bytes.size() < kMapHeaderSize) {
    return nullptr;
  }

  auto map = std::make_shared<MapDocument>();
  map->width = static_cast<int>(read_u16(bytes, 0));
  map->height = static_cast<int>(read_u16(bytes, 2));
  if (map->width <= 0 || map->height <= 0) {
    return nullptr;
  }

  const auto expected_size = kMapHeaderSize +
      static_cast<std::size_t>(map->width) * static_cast<std::size_t>(map->height) * kMapCellSize;
  if (bytes.size() < expected_size) {
    return nullptr;
  }

  // 列优先读取：外层循环 X，内层循环 Y
  // 源数据列优先存储，目标 MapDocument.cells 行优先存储
  map->cells.resize(static_cast<std::size_t>(map->width) * static_cast<std::size_t>(map->height));
  for (int x = 0; x < map->width; ++x) {
    for (int y = 0; y < map->height; ++y) {
      const auto source = kMapHeaderSize +
          (static_cast<std::size_t>(x) * static_cast<std::size_t>(map->height) +
           static_cast<std::size_t>(y)) * kMapCellSize;
      auto& cell = map->cells[static_cast<std::size_t>(y) * static_cast<std::size_t>(map->width) +
                              static_cast<std::size_t>(x)];
      cell.bk_img = read_u16(bytes, source);
      cell.mid_img = read_u16(bytes, source + 2U);
      cell.fr_img = read_u16(bytes, source + 4U);
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

// 根据 ArchiveId 返回对应的 .wil 文件路径
// 文件路径相对于 Data/ 目录
std::filesystem::path AssetManager::archive_path(const ArchiveId archive_id) const {
  auto path = root_path_ / "Data";
  switch (archive_id) {
    case ArchiveId::tiles:      return path / "Tiles.wil";
    case ArchiveId::sm_tiles:   return path / "SmTiles.wil";
    case ArchiveId::objects1:   return path / "Objects.wil";
    case ArchiveId::objects2:   return path / "Objects2.wil";
    case ArchiveId::objects3:   return path / "Objects3.wil";
    case ArchiveId::objects4:   return path / "Objects4.wil";
    case ArchiveId::objects5:   return path / "Objects5.wil";
    case ArchiveId::objects6:   return path / "Objects6.wil";
    case ArchiveId::objects7:   return path / "Objects7.wil";
    case ArchiveId::prguse:     return path / "Prguse.wil";
    case ArchiveId::chr_sel:    return path / "ChrSel.wil";
    case ArchiveId::hum:        return path / "Hum.wil";
    case ArchiveId::hair:       return path / "Hair.wil";
    case ArchiveId::weapon:     return path / "Weapon.wil";
    case ArchiveId::magic:      return path / "Magic.wil";
    case ArchiveId::magic2:     return path / "Magic2.wil";
    case ArchiveId::magic3:     return path / "Magic3.wil";
    case ArchiveId::mag_icon:   return path / "MagIcon.wil";
    case ArchiveId::npc:        return path / "Npc.wil";
    case ArchiveId::effect:     return path / "Effect.wil";
    case ArchiveId::prguse2:    return path / "Prguse2.wil";
    case ArchiveId::mmap:       return path / "mmap.wil";
    case ArchiveId::items:      return path / "Items.wil";
    case ArchiveId::state_item: return path / "StateItem.wil";
    case ArchiveId::dn_items:   return path / "DnItems.wil";
    case ArchiveId::mon1:       return path / "Mon1.wil";
    case ArchiveId::mon2:       return path / "Mon2.wil";
    case ArchiveId::mon3:       return path / "Mon3.wil";
    case ArchiveId::mon4:       return path / "Mon4.wil";
    case ArchiveId::mon5:       return path / "Mon5.wil";
    case ArchiveId::mon6:       return path / "Mon6.wil";
    case ArchiveId::mon7:       return path / "Mon7.wil";
    case ArchiveId::mon8:       return path / "Mon8.wil";
    case ArchiveId::mon9:       return path / "Mon9.wil";
    case ArchiveId::mon10:      return path / "Mon10.wil";
    case ArchiveId::mon11:      return path / "Mon11.wil";
    case ArchiveId::mon12:      return path / "Mon12.wil";
    case ArchiveId::mon13:      return path / "Mon13.wil";
    case ArchiveId::mon14:      return path / "Mon14.wil";
    case ArchiveId::mon15:      return path / "Mon15.wil";
    case ArchiveId::mon16:      return path / "Mon16.wil";
    case ArchiveId::mon17:      return path / "Mon17.wil";
    case ArchiveId::mon18:      return path / "Mon18.wil";
    case ArchiveId::mon19:      return path / "Mon19.wil";
    case ArchiveId::mon20:      return path / "Mon20.wil";
    case ArchiveId::mon21:      return path / "Mon21.wil";
  }
  return path / "Tiles.wil";
}

}  // namespace mir2::client
