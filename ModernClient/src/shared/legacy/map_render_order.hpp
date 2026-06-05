/**
 * @file map_render_order.hpp
 * @brief 传奇地图绘制层级排序模块
 *
 * @details 定义了地图渲染的绘制层级枚举和排序规则。
 *          传奇地图使用严格的绘制顺序（Painter's Algorithm），
 *          从远到近逐层绘制，确保正确的遮挡关系。
 *
 * 绘制层级（从下到上）：
 * 1. 背景瓦片（background_tiles）—— 最底层的地面
 * 2. 中间层瓦片（middle_tiles）—— 地面装饰物件
 * 3. 小物件（small_objects）—— 石头、花草等小型场景物件
 * 4. 地面特效（ground_effects）—— 火墙、毒云等地表魔法效果
 * 5. 大型物件（large_object）—— 建筑、树木等
 * 6. 地面物品（ground_item）—— 掉落在地上的道具
 * 7. 角色（actor）—— 玩家、怪物、NPC
 * 8. 飞行特效（fly_effect）—— 火球、雷电等飞行中的魔法效果
 * 9. 选择混合层（selection_blend）—— 选中目标的半透明高亮
 * 10. 角色叠加层（actor_overlay）—— 角色身上的护盾等附着特效
 * 11. 调试叠加层（debug_overlay）—— 开发调试信息
 * 12. UI 叠加特效（overlay_effects）—— UI 层级的特效
 * 13. 角色屏幕叠加层（actor_screen_overlay）—— 屏幕空间的角色特效
 *
 * @note 逐行渲染时，同一行的物件按 layer rank 从小到大依次绘制
 */

#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace mir2::legacy {

/**
 * @enum LegacyMapDrawLayer
 * @brief 地图绘制层级枚举 —— 定义地图渲染的 13 个绘制层
 *
 * @details 每一帧渲染时，按照此枚举的顺序从底层到顶层依次绘制。
 *          层级顺序决定了物件之间的遮挡关系：高层级物件遮挡低层级物件。
 */
enum class LegacyMapDrawLayer : std::uint8_t {
  background_tiles,       ///< 背景瓦片层 —— 地面纹理（最底层）
  middle_tiles,           ///< 中间瓦片层 —— 地面装饰（如草地上的小花）
  small_objects,          ///< 小物件层 —— 小型场景物件（石头、灌木）
  ground_effects,         ///< 地面特效层 —— 地表魔法效果（火墙、毒云）
  large_object,           ///< 大型物件层 —— 建筑、大树等大型障碍物
  ground_item,            ///< 地面物品层 —— 掉落的道具、金币
  actor,                  ///< 角色层 —— 玩家、怪物、NPC
  fly_effect,             ///< 飞行特效层 —— 飞行中的魔法弹道（火球、雷电）
  selection_blend,        ///< 选择混合层 —— 选中目标的高亮光标
  actor_overlay,          ///< 角色叠加层 —— 附着在角色上的特效（护盾、隐身）
  debug_overlay,          ///< 调试叠加层 —— 开发用的可视化调试信息
  overlay_effects,        ///< UI 叠加特效层 —— UI 层级的地面特效
  actor_screen_overlay,   ///< 角色屏幕叠加层 —— 屏幕空间坐标的角色特效
};

/**
 * @brief 逐行渲染时的层级绘制顺序
 *
 * @details 在逐行渲染循环中，每一行按此顺序绘制 large_object → ground_item →
 *          actor → fly_effect。背景瓦片和中间层在逐行循环之前已绘制完毕，
 *          叠加层在逐行循环之后绘制。
 */
constexpr std::array<LegacyMapDrawLayer, 4> kLegacyMapRowDrawOrder{
    LegacyMapDrawLayer::large_object,
    LegacyMapDrawLayer::ground_item,
    LegacyMapDrawLayer::actor,
    LegacyMapDrawLayer::fly_effect,
};

/**
 * @brief 获取绘制层级的名称字符串
 *
 * @details 将枚举值转换为可读的字符串，用于日志输出和调试。
 *
 * @param layer 绘制层级枚举值
 * @return 层级名称字符串（如 "actor"、"ground_item" 等）。非法值返回 "unknown"
 */
inline std::string_view legacy_map_draw_layer_name(const LegacyMapDrawLayer layer) {
  switch (layer) {
    case LegacyMapDrawLayer::background_tiles:
      return "background_tiles";
    case LegacyMapDrawLayer::middle_tiles:
      return "middle_tiles";
    case LegacyMapDrawLayer::small_objects:
      return "small_objects";
    case LegacyMapDrawLayer::ground_effects:
      return "ground_effects";
    case LegacyMapDrawLayer::large_object:
      return "large_object";
    case LegacyMapDrawLayer::ground_item:
      return "ground_item";
    case LegacyMapDrawLayer::actor:
      return "actor";
    case LegacyMapDrawLayer::actor_overlay:
      return "actor_overlay";
    case LegacyMapDrawLayer::fly_effect:
      return "fly_effect";
    case LegacyMapDrawLayer::selection_blend:
      return "selection_blend";
    case LegacyMapDrawLayer::debug_overlay:
      return "debug_overlay";
    case LegacyMapDrawLayer::overlay_effects:
      return "overlay_effects";
    case LegacyMapDrawLayer::actor_screen_overlay:
      return "actor_screen_overlay";
  }
  return "unknown";
}

/**
 * @brief 获取绘制层级的排序权重
 *
 * @details 返回层级的整数编号，用于在同一次渲染中进行排序比较。
 *          数值越小，绘制越早（越底层）。
 *
 * @param layer 绘制层级枚举值
 * @return 层级排序权重（0-12）。非法值返回 -1
 */
inline int legacy_map_draw_layer_rank(const LegacyMapDrawLayer layer) {
  switch (layer) {
    case LegacyMapDrawLayer::background_tiles:
      return 0;   // 最底层
    case LegacyMapDrawLayer::middle_tiles:
      return 1;
    case LegacyMapDrawLayer::small_objects:
      return 2;
    case LegacyMapDrawLayer::ground_effects:
      return 3;
    case LegacyMapDrawLayer::large_object:
      return 4;
    case LegacyMapDrawLayer::ground_item:
      return 5;
    case LegacyMapDrawLayer::actor:
      return 6;
    case LegacyMapDrawLayer::fly_effect:
      return 7;
    case LegacyMapDrawLayer::selection_blend:
      return 8;
    case LegacyMapDrawLayer::actor_overlay:
      return 9;
    case LegacyMapDrawLayer::debug_overlay:
      return 10;
    case LegacyMapDrawLayer::overlay_effects:
      return 11;
    case LegacyMapDrawLayer::actor_screen_overlay:
      return 12;  // 最顶层
  }
  return -1;
}

}  // namespace mir2::legacy
