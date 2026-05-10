// ============================================================
// Mir2 现代客户端 — 资源管理器声明
// 职责：管理 WIL/WIX 精灵归档和 .map 地图文件的加载与缓存，
//       提供精灵帧（SpriteFrame）和地图文档（MapDocument）的访问
//
// 传奇资源文件格式说明：
// 经典传奇客户端使用 WIL（Wemade Image Library）格式存储所有的
// 2D 精灵资源。每个 .wil 文件对应一个精灵归档（如 Hum.wil 包含
// 角色身体各方向帧），配套的 .wix 文件是帧偏移索引表。
//
// WIL 文件结构：
//   [头部 60 字节] 包含图片数量、颜色数、版本标识
//   [调色板 1024 字节] 仅 8 位模式使用（256 色 * 4 字节 BGRA）
//   [帧数据] 每帧 8-12 字节头 + 像素数据
//
// WIX 文件结构：
//   [头部 48-52 字节]
//   [偏移表] 每个帧在 .wil 文件中的偏移量（4 字节 * 帧数）
//
// 地图文件 (.map) 结构：
//   [头部 52 字节] 包含地图宽/高
//   [单元格数组] 每单元格 12 字节，列优先存储（先 X 后 Y）
//   每个单元格包含：背景图索引、中间层图索引、前景图索引、
//   门信息、动画信息、区域编号、光照值
// ============================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mir2::client {

/// WIL 归档枚举：对应 Data/ 目录下的 .wil 文件
/// 每个枚举值唯一对应一个 WIL 归档文件
enum class ArchiveId : std::uint8_t {
  tiles,      ///< Tiles.wil（地面砖块）
  sm_tiles,   ///< SmTiles.wil（小地面贴图）
  objects1,   ///< Objects.wil（地面物件（如树木/房屋/装饰））
  objects2, objects3, objects4, objects5, objects6, objects7,
  prguse,     ///< Prguse.wil（UI 界面元素如按钮/窗口/对话框）
  chr_sel,    ///< ChrSel.wil（选角界面）
  hum,        ///< Hum.wil（人类角色身体各方向帧）
  hair,       ///< Hair.wil（头发精灵，叠在 hum 之上）
  weapon,     ///< Weapon.wil（武器精灵，叠在 hum 之上）
  magic, magic2, magic3,  ///< 魔法效果精灵
  mag_icon,   ///< MagIcon.wil（魔法图标）
  npc,        ///< Npc.wil（NPC 精灵）
  effect,     ///< Effect.wil（特效精灵）
  prguse2,    ///< Prguse2.wil（UI 扩展）
  mmap,       ///< mmap.wil（小地图）
  items,      ///< Items.wil（物品图标，用于背包显示）
  state_item, ///< StateItem.wil（装备栏物品显示）
  dn_items,   ///< DnItems.wil（地面掉落物品显示）
  mon1, mon2, mon3, mon4, mon5, mon6, mon7, mon8, mon9, mon10,
  mon11, mon12, mon13, mon14, mon15, mon16, mon17, mon18, mon19, mon20, mon21, ///< 怪物精灵（1-21）
};

/// 精灵帧：从 WIL 归档解码得到的位图帧
/// 包含尺寸、热点偏移和 32 位 BGRA 像素数据
/// 热点（hotspot）用于精灵绘制时的锚点定位，
/// 例如角色脚底的位置取决于热点偏移
struct SpriteFrame {
  int width{0};
  int height{0};
  int hotspot_x{0};  ///< X 轴热点偏移（绘制锚点）
  int hotspot_y{0};  ///< Y 轴热点偏移（通常为脚底位置）
  std::vector<std::uint32_t> pixels{};  ///< 32 位 BGRA 像素，行优先存储

  [[nodiscard]] bool empty() const { return width <= 0 || height <= 0 || pixels.empty(); }
};

/// 地图单元格：对应 .map 文件中的 12 字节数据结构
/// 每单元格包含三层图像（背景/中间/前景）和各种属性
struct MapCell {
  std::uint16_t bk_img{0};    ///< 背景图索引（高 bit 0x8000 为阻挡标志）
  std::uint16_t mid_img{0};   ///< 中间层图索引（如地面装饰）
  std::uint16_t fr_img{0};    ///< 前景图索引（高 bit 0x8000 为阻挡标志，如墙壁）
  std::uint8_t door_index{0}; ///< 门索引（与门开关动画关联）
  std::uint8_t door_offset{0};///< 门偏移量（门打开/关闭的状态）
  std::uint8_t ani_frame{0};  ///< 动画帧序号（用于物件的自动动画）
  std::uint8_t ani_tick{0};   ///< 动画速度
  std::uint8_t area{0};       ///< 区域编号（0-6，选择对应的 Objects 归档）
  std::uint8_t light{0};      ///< 光照值（影响周围亮度的计算方式）
};

/// 地图文档：从 .map 文件解析的地图数据
/// 列优先存储（与经典客户端一致），提供碰撞检测
struct MapDocument {
  int width{0};
  int height{0};
  std::vector<MapCell> cells{};  ///< 行优先存储的单元格数组

  /// 获取指定坐标的单元格指针，越界返回 nullptr
  [[nodiscard]] const MapCell* cell(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) {
      return nullptr;
    }
    return &cells[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                  static_cast<std::size_t>(x)];
  }

  /// 碰撞检测：检查 (x, y) 处单元格是否可通过
  /// bk_img 和 fr_img 的高 bit（0x8000）作为阻挡标记
  /// 0 = 可通过，非0 = 不可通过（墙壁/水域/装饰物等）
  [[nodiscard]] bool can_move(int x, int y) const {
    const auto* target = cell(x, y);
    if (target == nullptr) {
      return false;  // 越界视为不可通过
    }
    if ((target->bk_img & 0x8000U) != 0U || (target->fr_img & 0x8000U) != 0U) {
      return false;
    }
    return ((target->door_index & 0x80U) == 0U) || ((target->door_offset & 0x80U) != 0U);
  }
};

/// 资源管理器：负责所有 WIL/WIX 归档和 .map 地图文件的加载/缓存/释放
/// 使用 LRU 式缓存策略（当前为永久缓存，直到场景切换释放地图）
class AssetManager {
 public:
  ~AssetManager();

  /// 初始化：设置资源根目录
  /// @param root_path 需包含 Data/（WIL/WIX 文件）和 Map/（.map 文件）子目录
  bool initialize(const std::filesystem::path& root_path);
  /// 释放场景资源（清空地图缓存），切换场景时调用
  void release_scene_assets();

  /// 获取指定归档中指定索引的精灵帧（自动缓存）
  /// @param archive_id 归档类型
  /// @param index 帧索引
  /// @return 精灵帧共享指针，失败返回 nullptr
  [[nodiscard]] std::shared_ptr<const SpriteFrame> get_frame(ArchiveId archive_id, int index);
  /// 加载并缓存指定 ID 的地图文档
  [[nodiscard]] std::shared_ptr<const MapDocument> load_map(const std::string& map_id);
  /// 获取当前资源根目录
  [[nodiscard]] const std::filesystem::path& root_path() const { return root_path_; }

 private:
  /// 内部 WIL 归档结构（文件数据、调色板、帧偏移索引）
  struct WilArchive;

  /// 获取或加载归档（自动缓存；加载失败会记录到 failed_archives_）
  [[nodiscard]] WilArchive& require_archive(ArchiveId archive_id);
  /// 从归档中解码指定索引的精灵帧
  [[nodiscard]] std::shared_ptr<SpriteFrame> decode_wil_frame(WilArchive& archive, int index);
  /// 从 .map 文件解码地图文档
  [[nodiscard]] std::shared_ptr<MapDocument> decode_map(const std::string& map_id) const;
  /// 根据 ArchiveId 返回对应的 .wil 文件路径
  [[nodiscard]] std::filesystem::path archive_path(ArchiveId archive_id) const;

  std::filesystem::path root_path_{};
  std::unordered_map<int, std::shared_ptr<WilArchive>> archives_{};    ///< 已加载的归档缓存
  std::unordered_map<int, bool> failed_archives_{};                    ///< 加载失败记录（避免重复尝试）
  std::unordered_map<std::uint64_t, std::shared_ptr<SpriteFrame>> frame_cache_{};  ///< 精灵帧缓存（key = ArchiveId<<32 | index）
  std::unordered_map<std::uint64_t, bool> missing_frame_cache_{};       ///< 缺失/空帧记录（避免重复解码）
  std::unordered_map<std::string, std::shared_ptr<MapDocument>> map_cache_{};      ///< 地图文档缓存
};

}  // namespace mir2::client
