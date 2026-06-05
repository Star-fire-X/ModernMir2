/**
 * @file map_render_math.hpp
 * @brief 传奇地图渲染数学工具模块 —— 与 Delphi 客户端兼容的视口和坐标计算
 *
 * @details 本模块定义了传奇（Mir2）地图渲染所需的所有常量和坐标转换函数。
 *          这些算法从原版 Delphi 客户端精确移植，确保现代 C++ 客户端渲染
 *          的画面与经典客户端像素级一致。
 *
 * 核心概念：
 * - 逻辑地图单元（Logical Map Unit）：游戏世界的坐标单位，1 单元 = 40 像素
 * - 渲染瓦片单元：菱形瓦片的基础尺寸为 48x32 像素
 * - 视口（Viewport）：屏幕上可见的地图区域，以玩家为中心
 * - 渲染边界（Render Bounds）：视口外扩后的实际渲染范围（包含部分屏幕外的物件）
 *
 * 坐标系统：
 * - 地图坐标（map_x, map_y）：游戏世界的逻辑坐标，单位为瓦片
 * - 屏幕坐标（screen_x, screen_y）：渲染后的像素坐标
 * - 鼠标坐标（mouse_x, mouse_y）：屏幕上的鼠标像素位置
 *
 * @note 所有常量和算法必须与原版 Delphi 客户端完全一致，不可修改！
 *       任何数值偏差都会导致渲染画面与经典客户端不一致。
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>

/**
 * @namespace mir2::legacy
 * @brief 旧版传奇兼容层命名空间，包含所有与 Delphi 客户端兼容的算法和数据结构
 */
namespace mir2::legacy {

// ============================================================================
// 基础渲染常量（与 Delphi 客户端中的常量完全一致）
// ============================================================================

/// 逻辑地图单元大小（像素），一个逻辑单元覆盖 40x40 像素区域
constexpr int kLegacyLogicalMapUnit = 40;
/// 瓦片单元宽度（像素），菱形瓦片在屏幕上的水平跨度
constexpr int kLegacyUnitX = 48;
/// 瓦片单元高度（像素），菱形瓦片在屏幕上的垂直跨度
constexpr int kLegacyUnitY = 32;
/// 瓦片半宽（像素），用于坐标居中对齐
constexpr int kLegacyHalfX = 24;
/// 瓦片半高（像素），用于坐标居中对齐
constexpr int kLegacyHalfY = 16;
/// 视口水平半宽（瓦片数），从玩家位置向左右各扩展 9 个瓦片
constexpr int kLegacyViewHalfWidth = 9;
/// 视口上方可见行数（瓦片），从玩家位置向上可见 9 个瓦片行
constexpr int kLegacyViewTopRows = 9;
/// 视口下方可见行数（瓦片），从玩家位置向下可见 8 个瓦片行
constexpr int kLegacyViewBottomRows = 8;
/// 高物件渲染扩展行数（瓦片），用于渲染比普通瓦片更高的物件（如建筑、大树）
constexpr int kLegacyLongHeightRows = 35;
/// 绘制原点 X 偏移（像素），将地图坐标映射到屏幕像素的基准偏移
constexpr int kLegacyDrawOriginX = -66;
/// 绘制原点 Y 偏移（像素）
constexpr int kLegacyDrawOriginY = -64;
/// 鼠标中心点 X（像素），屏幕中央的 X 坐标（用于鼠标→地图坐标变换）
constexpr int kLegacyMouseCenterX = 364;
/// 鼠标中心点 Y（像素），屏幕中央的 Y 坐标
constexpr int kLegacyMouseCenterY = 192;

// ============================================================================
// 数据结构
// ============================================================================

/**
 * @struct LegacyMapViewport
 * @brief 地图视口参数 —— 描述当前可见地图区域的边界和坐标偏移
 *
 * @details 视口以玩家为中心（rx, ry），向四周扩展形成可见矩形区域。
 *          shift_x/shift_y 用于角色在瓦片间平滑移动时的子像素偏移。
 */
struct LegacyMapViewport {
  int rx{0};              ///< 视口中心 X（玩家当前瓦片 X，含插值）
  int ry{0};              ///< 视口中心 Y（玩家当前瓦片 Y，含插值）
  int shift_x{0};         ///< X 方向子像素偏移（用于平滑移动插值）
  int shift_y{0};         ///< Y 方向子像素偏移
  int left{0};            ///< 视口左边界（瓦片坐标）
  int top{0};             ///< 视口上边界（瓦片坐标）
  int right{0};           ///< 视口右边界（瓦片坐标）
  int bottom{0};          ///< 视口下边界（瓦片坐标）
  int draw_origin_x{kLegacyDrawOriginX};  ///< 绘制原点 X（含 shift 调整）
  int draw_origin_y{kLegacyDrawOriginY};  ///< 绘制原点 Y（含 shift 调整）
};

/**
 * @struct LegacyMapRenderBounds
 * @brief 地图渲染边界 —— 描述需要绘制的瓦片和物件的范围
 *
 * @details 渲染边界比视口更大，因为需要绘制部分在屏幕外的物件
 *          （如屏幕边缘只露出一半的大型建筑）。分三层边界：
 *          - tile_*：瓦片层边界（包含所有需要绘制的瓦片）
 *          - object_*：物件层边界（包含可能延伸到屏幕内的高物件）
 *          - visible_*：实际可见边界（用于裁剪优化）
 */
struct LegacyMapRenderBounds {
  int tile_left{0};       ///< 瓦片绘制左边界
  int tile_top{0};        ///< 瓦片绘制上边界
  int tile_right{0};      ///< 瓦片绘制右边界
  int tile_bottom{0};     ///< 瓦片绘制下边界
  int object_left{0};     ///< 物件绘制左边界（比瓦片边界更宽）
  int object_top{0};      ///< 物件绘制上边界
  int object_right{0};    ///< 物件绘制右边界
  int object_bottom{0};   ///< 物件绘制下边界（向下扩展 kLegacyLongHeightRows）
  int visible_left{0};    ///< 实际可见左边界（用于裁剪）
  int visible_top{0};     ///< 实际可见上边界
  int visible_right{0};   ///< 实际可见右边界
  int visible_bottom{0};  ///< 实际可见下边界
};

// ============================================================================
// 核心坐标转换函数
// ============================================================================

/**
 * @brief 向上取整（Delphi 兼容版本）
 *
 * @details 将浮点数向上取整为整数。如果 value 已经是整数则返回自身，
 *          否则返回 truncated + 1。此行为与 Delphi 的 Ceil 函数一致。
 *
 * @param value 需要取整的浮点数值
 * @return 向上取整后的整数值
 */
inline int legacy_up_int(const double value) {
  const auto truncated = static_cast<int>(value);
  return value > static_cast<double>(truncated) ? truncated + 1 : truncated;
}

/**
 * @brief 创建地图视口参数
 *
 * @details 根据玩家位置（rx, ry）计算视口的各边界值和绘制原点。
 *          视口向左右各扩展 kLegacyViewHalfWidth 个瓦片，
 *          向上扩展 kLegacyViewTopRows 行，向下扩展 kLegacyViewBottomRows 行。
 *
 * @param rx 视口中心 X 坐标（瓦片），通常为玩家 X
 * @param ry 视口中心 Y 坐标（瓦片），通常为玩家 Y
 * @param shift_x X 方向子像素偏移（用于平滑移动），默认为 0
 * @param shift_y Y 方向子像素偏移，默认为 0
 * @return 初始化后的视口参数结构
 */
inline LegacyMapViewport make_legacy_map_viewport(const int rx, const int ry,
                                                  const int shift_x = 0,
                                                  const int shift_y = 0) {
  LegacyMapViewport viewport;
  viewport.rx = rx;
  viewport.ry = ry;
  viewport.shift_x = shift_x;
  viewport.shift_y = shift_y;
  viewport.left = rx - kLegacyViewHalfWidth;
  viewport.top = ry - kLegacyViewTopRows;
  viewport.right = rx + kLegacyViewHalfWidth;
  viewport.bottom = ry + kLegacyViewBottomRows;
  viewport.draw_origin_x = kLegacyDrawOriginX - shift_x;
  viewport.draw_origin_y = kLegacyDrawOriginY - shift_y;
  return viewport;
}

/**
 * @brief 根据视口计算渲染边界
 *
 * @details 渲染边界比视口更宽，以容纳屏幕边缘部分可见的大型物件。
 *          - 瓦片层比视口左右各多 2 个瓦片，上下多 1 个瓦片
 *          - 物件层向下额外扩展 kLegacyLongHeightRows 行（容纳高建筑）
 *
 * @param viewport 当前视口参数
 * @return 完整的渲染边界结构
 */
inline LegacyMapRenderBounds legacy_map_render_bounds(const LegacyMapViewport& viewport) {
  return LegacyMapRenderBounds{
      viewport.left - 2,
      viewport.top - 1,
      viewport.right + 1,
      viewport.bottom + 1,
      viewport.left - 2,
      viewport.top,
      viewport.right + 2,
      viewport.bottom + kLegacyLongHeightRows,
      viewport.left,
      viewport.top,
      viewport.right,
      viewport.bottom,
  };
}

/**
 * @brief 地图坐标转换为屏幕像素坐标
 *
 * @details 将游戏世界的地图瓦片坐标转换为屏幕上的像素坐标。
 *          转换公式：(map_coord - viewport_center) * tile_size + mouse_center + half_tile - shift
 *
 * @param viewport 当前视口参数
 * @param map_x 地图瓦片 X 坐标
 * @param map_y 地图瓦片 Y 坐标
 * @return 屏幕像素坐标的 (x, y) 对
 */
inline std::pair<int, int> legacy_screen_from_map(const LegacyMapViewport& viewport,
                                                  const int map_x, const int map_y) {
  return {(map_x - viewport.rx) * kLegacyUnitX + kLegacyMouseCenterX + kLegacyHalfX -
              viewport.shift_x,
          (map_y - viewport.ry) * kLegacyUnitY + kLegacyMouseCenterY + kLegacyHalfY -
              viewport.shift_y};
}

/**
 * @brief 鼠标屏幕坐标转换为地图瓦片坐标
 *
 * @details 将屏幕上的鼠标像素位置反向转换为游戏世界的地图瓦片坐标。
 *          使用 legacy_up_int（等价于 Delphi 的 Ceil）进行取整。
 *
 * @param viewport 当前视口参数
 * @param mouse_x 鼠标屏幕 X 坐标（像素）
 * @param mouse_y 鼠标屏幕 Y 坐标（像素）
 * @return 地图瓦片坐标的 (x, y) 对
 */
inline std::pair<int, int> legacy_mouse_to_map(const LegacyMapViewport& viewport,
                                               const int mouse_x, const int mouse_y) {
  const auto map_x =
      legacy_up_int((mouse_x - kLegacyMouseCenterX + viewport.shift_x - kLegacyUnitX) /
                    static_cast<double>(kLegacyUnitX)) +
      viewport.rx;
  const auto map_y =
      legacy_up_int((mouse_y - kLegacyMouseCenterY + viewport.shift_y - kLegacyUnitY) /
                    static_cast<double>(kLegacyUnitY)) +
      viewport.ry;
  return {map_x, map_y};
}

/**
 * @brief 鼠标坐标转换为地图坐标（带边界裁剪）
 *
 * @details 在 legacy_mouse_to_map 基础上将结果裁剪到 [0, map_width-1] × [0, map_height-1] 范围内，
 *          防止返回无效的地图坐标。
 *
 * @param viewport 当前视口参数
 * @param mouse_x 鼠标屏幕 X 坐标（像素）
 * @param mouse_y 鼠标屏幕 Y 坐标（像素）
 * @param map_width 地图宽度（瓦片数）
 * @param map_height 地图高度（瓦片数）
 * @return 裁剪后的地图瓦片坐标
 */
inline std::pair<int, int> legacy_mouse_to_map_clamped(const LegacyMapViewport& viewport,
                                                       const int mouse_x, const int mouse_y,
                                                       const int map_width,
                                                       const int map_height) {
  auto [x, y] = legacy_mouse_to_map(viewport, mouse_x, mouse_y);
  x = std::clamp(x, 0, std::max(0, map_width - 1));
  y = std::clamp(y, 0, std::max(0, map_height - 1));
  return {x, y};
}

/**
 * @brief 计算有效地图范围
 *
 * @details 优先使用服务端下发的地图尺寸（server_extent），
 *          如果服务端未下发（server_extent <= 0），则回退到实际加载的地图尺寸。
 *
 * @param server_extent 服务端下发的地图尺寸（宽度或高度）
 * @param loaded_extent 实际从文件加载的地图尺寸
 * @return 有效的地图尺寸
 */
inline int legacy_effective_map_extent(const int server_extent, const int loaded_extent) {
  return server_extent > 0 ? server_extent : std::max(0, loaded_extent);
}

/**
 * @brief 鼠标坐标转地图坐标（带边界裁剪，支持双维度分别取有效范围）
 *
 * @details 与 legacy_mouse_to_map_clamped 类似，但允许宽度和高度使用各自的有效范围。
 *          适用于服务端可能只下发了宽度或高度其中之一的情况。
 *
 * @param viewport 当前视口参数
 * @param mouse_x 鼠标屏幕 X 坐标（像素）
 * @param mouse_y 鼠标屏幕 Y 坐标（像素）
 * @param server_width 服务端下发的宽度
 * @param server_height 服务端下发的高度
 * @param loaded_width 实际加载的宽度
 * @param loaded_height 实际加载的高度
 * @return 裁剪后的地图瓦片坐标
 */
inline std::pair<int, int> legacy_mouse_to_map_clamped(
    const LegacyMapViewport& viewport, const int mouse_x, const int mouse_y,
    const int server_width, const int server_height, const int loaded_width,
    const int loaded_height) {
  return legacy_mouse_to_map_clamped(
      viewport, mouse_x, mouse_y, legacy_effective_map_extent(server_width, loaded_width),
      legacy_effective_map_extent(server_height, loaded_height));
}

/**
 * @brief 计算瓦片在屏幕上的绘制 X 坐标
 *
 * @details 将地图 X 坐标转换为屏幕上的像素 X。公式：(map_x - viewport.left) * tile_width + draw_origin_x
 *
 * @param viewport 当前视口参数
 * @param map_x 地图瓦片 X 坐标
 * @return 屏幕像素 X 坐标
 */
inline int legacy_tile_draw_x(const LegacyMapViewport& viewport, const int map_x) {
  return (map_x - viewport.left) * kLegacyUnitX + viewport.draw_origin_x;
}

/**
 * @brief 计算地面背景层的绘制 Y 坐标
 *
 * @details 地面背景（最底层瓦片）的 Y 坐标比中间层高一个瓦片单元。
 *          公式：(map_y - viewport.top) * tile_height + draw_origin_y - tile_height
 *
 * @param viewport 当前视口参数
 * @param map_y 地图瓦片 Y 坐标
 * @return 地面背景层的屏幕像素 Y 坐标
 */
inline int legacy_ground_back_y(const LegacyMapViewport& viewport, const int map_y) {
  return (map_y - viewport.top) * kLegacyUnitY + viewport.draw_origin_y - kLegacyUnitY;
}

/**
 * @brief 计算地面中间层的绘制 Y 坐标
 *
 * @details 公式：(map_y - viewport.top) * tile_height + draw_origin_y
 *
 * @param viewport 当前视口参数
 * @param map_y 地图瓦片 Y 坐标
 * @return 地面中间层的屏幕像素 Y 坐标
 */
inline int legacy_ground_mid_y(const LegacyMapViewport& viewport, const int map_y) {
  return (map_y - viewport.top) * kLegacyUnitY + viewport.draw_origin_y;
}

/**
 * @brief 计算物件行绘制 Y 坐标
 *
 * @details 物件（包括角色、地面物品）的 Y 坐标从背景层开始计算。
 *
 * @param viewport 当前视口参数
 * @param map_y 地图瓦片 Y 坐标
 * @return 物件行屏幕像素 Y 坐标
 */
inline int legacy_object_row_y(const LegacyMapViewport& viewport, const int map_y) {
  return legacy_ground_back_y(viewport, map_y);
}

/**
 * @brief 计算角色的屏幕基准 X 坐标
 *
 * @details 角色 X = 瓦片 X 的屏幕坐标 + 角色自身的 X 偏移（用于平滑移动）
 *
 * @param viewport 当前视口参数
 * @param actor_rx 角色瓦片 X 坐标
 * @param actor_shift_x 角色 X 方向子像素偏移
 * @return 角色在屏幕上的基准 X 像素坐标
 */
inline int legacy_actor_base_x(const LegacyMapViewport& viewport, const int actor_rx,
                               const int actor_shift_x) {
  return legacy_tile_draw_x(viewport, actor_rx) + actor_shift_x;
}

/**
 * @brief 计算角色的屏幕基准 Y 坐标
 *
 * @details 角色 Y = 物件行 Y 坐标 + 角色自身的 Y 偏移
 *
 * @param viewport 当前视口参数
 * @param actor_ry 角色瓦片 Y 坐标
 * @param actor_shift_y 角色 Y 方向子像素偏移
 * @return 角色在屏幕上的基准 Y 像素坐标
 */
inline int legacy_actor_base_y(const LegacyMapViewport& viewport, const int actor_ry,
                               const int actor_shift_y) {
  return legacy_object_row_y(viewport, actor_ry) + actor_shift_y;
}

/**
 * @brief 计算角色绘制归属行（用于遮挡排序）
 *
 * @details "绘制归属行"决定角色被绘制到哪一个瓦片行中，影响前后的遮挡关系。
 *          公式：actor_ry - down_draw_level。down_draw_level 越大，
 *          角色越"低"（绘制在更靠前的行，被更少的物件遮挡）。
 *
 * @param actor_ry 角色瓦片 Y 坐标
 * @param down_draw_level 下移绘制级别（通常为 0 或来自体型/动作参数）
 * @return 角色的绘制归属行号
 */
inline int legacy_actor_draw_row(const int actor_ry, const int down_draw_level) {
  return actor_ry - down_draw_level;
}

/**
 * @brief 计算地面物品的绘制 X 坐标
 *
 * @details 地面物品的 X 坐标居中于瓦片单元格。
 *          公式：tile_draw_x + half_tile_width - frame_width / 2
 *
 * @param viewport 当前视口参数
 * @param map_x 物品所在瓦片 X 坐标
 * @param frame_width 物品精灵帧的像素宽度
 * @return 地面物品的屏幕像素 X 坐标
 */
inline int legacy_ground_item_draw_x(const LegacyMapViewport& viewport, const int map_x,
                                     const int frame_width) {
  return legacy_tile_draw_x(viewport, map_x) + kLegacyHalfX - frame_width / 2;
}

/**
 * @brief 计算地面物品的绘制 Y 坐标
 *
 * @details 地面物品的 Y 坐标居中于瓦片单元格。
 *          公式：object_row_y + half_tile_height - frame_height / 2
 *
 * @param viewport 当前视口参数
 * @param map_y 物品所在瓦片 Y 坐标
 * @param frame_height 物品精灵帧的像素高度
 * @return 地面物品的屏幕像素 Y 坐标
 */
inline int legacy_ground_item_draw_y(const LegacyMapViewport& viewport, const int map_y,
                                     const int frame_height) {
  return legacy_object_row_y(viewport, map_y) + kLegacyHalfY - frame_height / 2;
}

/**
 * @brief 计算地面大瓦片（2x2 拼接）的帧索引
 *
 * @details 传奇地图支持 2x2 的大瓦片（由 4 个普通瓦片拼接而成）。
 *          只有当 map_x 和 map_y 都是偶数时，该单元格才是大瓦片的左上角。
 *          帧索引 = (bk_img & 0x7FFF) - 1，其中低 15 位存储图片编号。
 *
 * @param map_x 瓦片 X 坐标
 * @param map_y 瓦片 Y 坐标
 * @param bk_img 背景图片标识值（从地图文件读取的 16 位值）
 * @return 大瓦片帧索引。如果不是有效的左上角位置或图片编号无效，返回 -1
 */
inline int legacy_ground_tile_frame_index(const int map_x, const int map_y,
                                          const std::uint16_t bk_img) {
  const auto index = static_cast<int>(bk_img & 0x7FFFU);
  if (index <= 0 || (map_x % 2) != 0 || (map_y % 2) != 0) {
    return -1;
  }
  return index - 1;
}

/**
 * @brief 计算小物件（中间层瓦片）的帧索引
 *
 * @details 中间层物件（mid_img）使用与地面大瓦片相同的图片编号编码。
 *          帧索引 = mid_img - 1，当 mid_img 为 0 时表示该位置无物件。
 *
 * @param mid_img 中间层图片标识值
 * @return 小物件帧索引。如果 mid_img 为 0（无物件），返回 -1
 */
inline int legacy_small_tile_frame_index(const std::uint16_t mid_img) {
  return mid_img == 0U ? -1 : static_cast<int>(mid_img) - 1;
}

/**
 * @brief 判断地图坐标是否在门的交互范围内
 *
 * @details 门有两种状态：开启和关闭，交互范围略有不同。
 *          开启的门范围更大（±10），关闭的门范围稍小（±8）。
 *          此函数用于判断鼠标点击或角色位置是否与门交互。
 *
 * @param open 门是否开启
 * @param door_x 门的 X 坐标
 * @param door_y 门的 Y 坐标
 * @param x 待检测的 X 坐标
 * @param y 待检测的 Y 坐标
 * @return 如果坐标在门的范围内返回 true，否则返回 false
 */
inline bool legacy_map_door_state_reaches(const bool open, const int door_x,
                                          const int door_y, const int x,
                                          const int y) {
  const auto left = open ? door_x - 10 : door_x - 8;
  const auto right = door_x + 10;
  const auto top = open ? door_y - 10 : door_y - 8;
  const auto bottom = door_y + 10;
  return x >= left && x <= right && y >= top && y <= bottom;
}

}  // namespace mir2::legacy
