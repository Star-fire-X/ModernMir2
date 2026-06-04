/**
 * @file legacy_map_environment.cpp
 * @brief 地图环境管理器实现
 * @details 实现了LegacyMapEnvironment的全部功能，包括地图对象管理、
 *          通行检测、门系统控制、物品堆叠与合并等。
 *          兼容传奇3原版地图的逻辑。
 */

#include "world/legacy_map_environment.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace mir2 {

/**
 * @brief 构造函数
 * @param width 地图宽度
 * @param height 地图高度
 * @param movement_map 静态通行地图数据
 */
LegacyMapEnvironment::LegacyMapEnvironment(
    std::int32_t width, std::int32_t height,
    std::shared_ptr<const legacy::MapDocument> movement_map) {
  reset(width, height, std::move(movement_map));
}

/**
 * @brief 重置地图环境
 * @details 清除所有已存在的动态对象和门数据，重新初始化地图尺寸和通行数据。
 * @param width 地图宽度
 * @param height 地图高度
 * @param movement_map 静态通行地图数据
 */
void LegacyMapEnvironment::reset(std::int32_t width, std::int32_t height,
                                 std::shared_ptr<const legacy::MapDocument> movement_map) {
  width_ = width;
  height_ = height;
  movement_map_ = std::move(movement_map);
  cells_.clear();
  door_cores_.clear();
  door_tiles_.clear();
  load_doors_from_map();
}

/**
 * @brief 判断坐标是否在地图范围内
 * @details 如果有 movement_map，使用其 cell() 方法判断。
 *          否则使用 width/height 范围判断。
 *          如果 width/height 都 <= 0（未初始化），认为任何坐标都在范围内。
 * @param x X坐标
 * @param y Y坐标
 * @return true 在范围内，false 不在
 */
bool LegacyMapEnvironment::in_bounds(std::int32_t x, std::int32_t y) const {
  if (movement_map_ != nullptr) {
    return movement_map_->cell(x, y) != nullptr;
  }
  if (width_ <= 0 || height_ <= 0) {
    return true;
  }
  return x >= 0 && y >= 0 && x < width_ && y < height_;
}

/**
 * @brief 判断静态地形是否可通行
 * @details 使用 legacy::MapDocument::terrain_blocks_move() 判断，
 *          不���虑动态对象的影响。
 * @param x X坐标
 * @param y Y坐标
 * @return true 可通行，false 不可通行
 */
bool LegacyMapEnvironment::static_can_move(std::int32_t x, std::int32_t y) const {
  if (!in_bounds(x, y)) {
    return false;
  }
  if (movement_map_ != nullptr) {
    const auto* target = movement_map_->cell(x, y);
    return target != nullptr && !legacy::MapDocument::terrain_blocks_move(*target);
  }
  return true;
}

/**
 * @brief 判断静态地形是否可飞行
 * @details 检查地图格子的 fr_img 标志位（0x8000 表示不可飞行）。
 * @param x X坐标
 * @param y Y坐标
 * @return true 可飞行，false 不可飞行
 */
bool LegacyMapEnvironment::static_can_fly(std::int32_t x, std::int32_t y) const {
  if (!in_bounds(x, y)) {
    return false;
  }
  if (movement_map_ != nullptr) {
    const auto* target = movement_map_->cell(x, y);
    return target != nullptr && (target->fr_img & 0x8000U) == 0U;
  }
  return true;
}

/**
 * @brief 判断是否可以走到指定位置
 * @details 综合考虑静态地形和动态对象的影响：
 *          1. 静态地形必须可通行
 *          2. 如果 allow_dup 为 true，允许重叠
 *          3. 否则检查该位置是否有阻挡行走的对象：
 *             - event_object 且 blocks_walk -> 阻挡
 *             - moving_object 且非幽灵/非死亡/非隐身/非GM模式 -> 阻挡
 * @param x X坐标
 * @param y Y坐标
 * @param allow_dup 是否允许多人在同一格（重叠）
 * @return true 可走到，false 不可走到
 */
bool LegacyMapEnvironment::can_walk(std::int32_t x, std::int32_t y, bool allow_dup) const {
  if (!static_can_move(x, y)) {
    return false;
  }
  if (allow_dup) {
    return true;
  }

  const auto* target = cell(x, y);
  if (target == nullptr) {
    return true;
  }
  // 检查动态对象的阻挡情况
  for (const auto& object : target->obj_list) {
    if (object.shape == LegacyMapObjectShape::event_object && object.blocks_walk) {
      return false;
    }
    if (object.shape == LegacyMapObjectShape::moving_object) {
      if (!object.moving.ghost && object.moving.hold_place && !object.moving.death &&
          !object.moving.hide_mode && !object.moving.supervisor_mode) {
        return false;
      }
    }
  }
  return true;
}

/**
 * @brief 检查两点间是否可直线飞行（用于移动）
 * @details 沿直线从起点到终点逐步检查每个格子的静态地形可通行性。
 *          使用逐像素逼近算法（Bresenham-like），最多32步。
 * @param from_x 起点X
 * @param from_y 起点Y
 * @param to_x 终点X
 * @param to_y 终点Y
 * @return true 可直线到达，false 中途有阻挡
 */
bool LegacyMapEnvironment::can_fly_line(std::int32_t from_x, std::int32_t from_y,
                                        std::int32_t to_x, std::int32_t to_y) const {
  if (!in_bounds(from_x, from_y) || !in_bounds(to_x, to_y)) {
    return false;
  }
  auto x = from_x;
  auto y = from_y;
  for (std::int32_t step = 0; step < 32; ++step) {
    if (x == to_x && y == to_y) {
      return true;
    }
    const auto dx = to_x == x ? 0 : (to_x > x ? 1 : -1);
    const auto dy = to_y == y ? 0 : (to_y > y ? 1 : -1);
    x += dx;
    y += dy;
    if (!static_can_move(x, y)) {
      return false;
    }
    if (x == to_x && y == to_y) {
      return true;
    }
  }
  return false;
}

/**
 * @brief 检查两点间是否可直线飞行（用于远程攻击）
 * @details 使用 static_can_fly 代替 static_can_move 进行检测，
 *          允许飞越某些不可行走但可飞越的地形。
 * @param from_x 起点X
 * @param from_y 起点Y
 * @param to_x 终点X
 * @param to_y 终点Y
 * @return true 可直线攻击，false 中途有阻挡
 */
bool LegacyMapEnvironment::can_fire_fly_line(std::int32_t from_x, std::int32_t from_y,
                                             std::int32_t to_x, std::int32_t to_y) const {
  if (!in_bounds(from_x, from_y) || !in_bounds(to_x, to_y)) {
    return false;
  }
  auto x = from_x;
  auto y = from_y;
  for (std::int32_t step = 0; step < 32; ++step) {
    if (x == to_x && y == to_y) {
      return true;
    }
    const auto dx = to_x == x ? 0 : (to_x > x ? 1 : -1);
    const auto dy = to_y == y ? 0 : (to_y > y ? 1 : -1);
    x += dx;
    y += dy;
    if (!static_can_fly(x, y)) {
      return false;
    }
    if (x == to_x && y == to_y) {
      return true;
    }
  }
  return false;
}

/**
 * @brief 添加移动对象到地图
 * @param x X坐标
 * @param y Y坐标
 * @param object_id 对象ID
 * @param now_ms 当前时间戳
 * @param state 移动对象状态
 * @return true 添加成功，false 失败（静态地形不可通行）
 */
bool LegacyMapEnvironment::add_moving_object(std::int32_t x, std::int32_t y,
                                             std::uint64_t object_id,
                                             std::uint64_t now_ms,
                                             LegacyMovingObjectState state) {
  if (!static_can_move(x, y)) {
    return false;
  }
  auto* target = mutable_cell(x, y);
  if (target == nullptr) {
    return false;
  }

  LegacyMapObject object;
  object.shape = LegacyMapObjectShape::moving_object;
  object.object_id = object_id;
  object.a_time_ms = now_ms;
  object.moving = state;
  target->obj_list.push_back(object);
  return true;
}

/**
 * @brief 移动对象到新位置
 * @details 从旧位置移除对象，添加到新位置。
 *          返回值说明：
 *          - 1: 成功移动
 *          - 0: 新位置不可通行
 *          - -1: 坐标越界或添加失败
 * @param old_x 旧位置X
 * @param old_y 旧位置Y
 * @param object_id 对象ID
 * @param new_x 新位置X
 * @param new_y 新位置Y
 * @param allow_dup 是否允许重叠
 * @param now_ms 当前时间戳
 * @param state 移动对象状态
 * @return 移动结果（1=成功, 0=不可通行, -1=越界/失败）
 */
int LegacyMapEnvironment::move_to_moving_object(std::int32_t old_x, std::int32_t old_y,
                                                std::uint64_t object_id,
                                                std::int32_t new_x, std::int32_t new_y,
                                                bool allow_dup, std::uint64_t now_ms,
                                                LegacyMovingObjectState state) {
  // 不允许重叠时检查新位置的可通行性
  if (!allow_dup) {
    if (!in_bounds(new_x, new_y) || !static_can_move(new_x, new_y)) {
      return -1;
    }
    if (!can_walk(new_x, new_y, false)) {
      return 0;
    }
  }

  if (!in_bounds(new_x, new_y) || !static_can_move(new_x, new_y)) {
    return -1;
  }

  // 从旧位置移除对象
  const CellKey old_key{old_x, old_y};
  if (auto old_it = cells_.find(old_key); old_it != cells_.end()) {
    auto& list = old_it->second.obj_list;
    for (auto it = list.begin(); it != list.end();) {
      if (it->shape == LegacyMapObjectShape::moving_object && it->object_id == object_id) {
        it = list.erase(it);
      } else {
        ++it;
      }
    }
    erase_cell_if_empty(old_key);
  }

  // 添加到新位置
  return add_moving_object(new_x, new_y, object_id, now_ms, state) ? 1 : -1;
}

/**
 * @brief 添加物品对象到地图
 * @details 如果是金币，尝试与同位置已有的金币堆叠。
 *          堆叠上限为 kLegacyBagGold（5000万）。
 *          每个格子最多放置5个物品。
 * @param x X坐标
 * @param y Y坐标
 * @param object_id 对象ID
 * @param item 物品状态
 * @param now_ms 当前时间戳
 * @return 添加结果（包含是否成功、是否合并等信息）
 */
LegacyMapAddResult LegacyMapEnvironment::add_item_object(std::int32_t x, std::int32_t y,
                                                         std::uint64_t object_id,
                                                         LegacyMapItemState item,
                                                         std::uint64_t now_ms) {
  LegacyMapAddResult result;
  if (!static_can_move(x, y)) {
    return result;
  }

  auto* target = mutable_cell(x, y);
  if (target == nullptr) {
    return result;
  }

  // 金币合并逻辑：找到同位置的金币堆进行合并
  if (item.is_gold) {
    for (auto& object : target->obj_list) {
      if (object.shape != LegacyMapObjectShape::item_object || !object.item.is_gold) {
        continue;
      }
      const auto merged_amount = object.item.gold_amount + item.gold_amount;
      if (merged_amount <= kLegacyBagGold) {
        object.item.gold_amount = merged_amount;
        object.a_time_ms = now_ms;
        result.ok = true;
        result.merged = true;
        result.object_id = object.object_id;
        result.merged_gold_amount = merged_amount;
        return result;
      }
    }
  }

  // 检查堆叠上限（每个格子最多5个物品）
  if (target->obj_list.size() >= 5) {
    return result;
  }

  // 创建新的物品对象
  LegacyMapObject object;
  object.shape = LegacyMapObjectShape::item_object;
  object.object_id = object_id;
  object.a_time_ms = now_ms;
  object.item = item;
  target->obj_list.push_back(object);

  result.ok = true;
  result.object_id = object_id;
  return result;
}

/**
 * @brief 添加占位对象
 * @details 用于事件对象等非移动非物品对象。
 *          根据 placement_policy 选择放置在可通行或不可通行的位置。
 * @param x X坐标
 * @param y Y坐标
 * @param shape 对象形状
 * @param object_id 对象ID
 * @param now_ms 当前时间戳
 * @param placement_policy 放置策略
 * @param blocks_walk 是否阻挡行走
 * @return true 添加成功，false 失败
 */
bool LegacyMapEnvironment::add_placeholder_object(std::int32_t x, std::int32_t y,
                                                  LegacyMapObjectShape shape,
                                                  std::uint64_t object_id,
                                                  std::uint64_t now_ms,
                                                  LegacyMapPlacementPolicy placement_policy,
                                                  bool blocks_walk) {
  if (placement_policy == LegacyMapPlacementPolicy::passable_only) {
    if (!static_can_move(x, y)) {
      return false;
    }
  } else {
    if (!in_bounds(x, y) || static_can_move(x, y)) {
      return false;
    }
  }
  auto* target = mutable_cell(x, y);
  if (target == nullptr) {
    return false;
  }
  LegacyMapObject object;
  object.shape = shape;
  object.object_id = object_id;
  object.a_time_ms = now_ms;
  object.blocks_walk = blocks_walk;
  target->obj_list.push_back(object);
  return true;
}

/**
 * @brief 添加门对象
 * @param x X坐标
 * @param y Y坐标
 * @param object_id 对象ID
 * @param gate 门状态（目标地图、坐标、开门条件）
 * @param now_ms 当前时间戳
 * @return true 添加成功，false 失败
 */
bool LegacyMapEnvironment::add_gate_object(std::int32_t x, std::int32_t y,
                                           std::uint64_t object_id,
                                           LegacyMapGateState gate,
                                           std::uint64_t now_ms) {
  if (!in_bounds(x, y)) {
    return false;
  }
  auto* target = mutable_cell(x, y);
  if (target == nullptr) {
    return false;
  }
  LegacyMapObject object;
  object.shape = LegacyMapObjectShape::gate_object;
  object.object_id = object_id;
  object.a_time_ms = now_ms;
  object.gate = std::move(gate);
  target->obj_list.push_back(std::move(object));
  return true;
}

/**
 * @brief 从地图删除对象
 * @details 在指定位置查找并删除匹配形状和ID的对象。
 *          返回值说明：
 *          - 1: 成功删除
 *          - 0: 坐标越界
 *          - -1: 对象不存在
 *          - -2: 该位置没有对象列表
 * @param x X坐标
 * @param y Y坐标
 * @param shape 对象形状
 * @param object_id 对象ID
 * @return 删除结果
 */
int LegacyMapEnvironment::delete_from_map(std::int32_t x, std::int32_t y,
                                          LegacyMapObjectShape shape,
                                          std::uint64_t object_id) {
  if (!in_bounds(x, y)) {
    return 0;
  }

  const CellKey key{x, y};
  auto cell_it = cells_.find(key);
  if (cell_it == cells_.end() || cell_it->second.obj_list.empty()) {
    return -2;
  }

  int result = -1;
  auto& list = cell_it->second.obj_list;
  for (auto it = list.begin(); it != list.end();) {
    if (it->shape == shape && it->object_id == object_id) {
      it = list.erase(it);
      result = 1;
    } else {
      ++it;
    }
  }
  erase_cell_if_empty(key);
  return result;
}

/**
 * @brief 验证并更新对象活跃时间
 * @details 用于确认对象是否存在，并更新其最后活跃时间戳以防超时被清理。
 * @param x X坐标
 * @param y Y坐标
 * @param object_id 对象ID
 * @param now_ms 当前时间戳
 * @return true 找到并更新，false 未找到
 */
bool LegacyMapEnvironment::verify_map_time(std::int32_t x, std::int32_t y,
                                           std::uint64_t object_id,
                                           std::uint64_t now_ms) {
  auto cell_it = cells_.find(CellKey{x, y});
  if (cell_it == cells_.end()) {
    return false;
  }
  for (auto& object : cell_it->second.obj_list) {
    if (object.shape == LegacyMapObjectShape::moving_object &&
        object.object_id == object_id) {
      object.a_time_ms = now_ms;
      return true;
    }
  }
  return false;
}

/**
 * @brief 获取指定位置的门对象
 * @param x X坐标
 * @param y Y坐标
 * @return 门对象指针，不存在返回 nullptr
 */
const LegacyMapObject* LegacyMapEnvironment::gate_at(std::int32_t x, std::int32_t y) const {
  const auto* target = cell(x, y);
  if (target == nullptr) {
    return nullptr;
  }
  for (const auto& object : target->obj_list) {
    if (object.shape == LegacyMapObjectShape::gate_object) {
      return &object;
    }
  }
  return nullptr;
}

/**
 * @brief 检查周围的门是否全部打开
 * @details 检查(3x3)范围内所有门核心，任何一个未打开则返回false。
 *          如果没有门核心则返回true。
 * @param x X坐标
 * @param y Y坐标
 * @return true 所有门已打开或没有门，false 有门未打开
 */
bool LegacyMapEnvironment::around_door_opened(std::int32_t x, std::int32_t y) const {
  auto found = false;
  for (std::int32_t dy = -1; dy <= 1; ++dy) {
    for (std::int32_t dx = -1; dx <= 1; ++dx) {
      const auto* core = door_core_at(x + dx, y + dy);
      if (core == nullptr) {
        continue;
      }
      found = true;
      if (!core->open) {
        return false;
      }
    }
  }
  return true;
}

/**
 * @brief 打开周围的门
 * @details 搜索(3x3)范围内的门瓦片，打开对应的门核心。
 *          同一核心的多个瓦片只打开一次。
 *          已打开或已锁定的门跳过。
 * @param x X坐标
 * @param y Y坐标
 * @param now_ms 当前时间戳
 * @return 被打开的门瓦片坐标列表
 */
std::vector<std::pair<std::int32_t, std::int32_t>> LegacyMapEnvironment::open_doors_around(
    std::int32_t x, std::int32_t y, std::uint64_t now_ms) {
  std::vector<CellKey> opened;
  std::vector<std::size_t> opened_cores;
  for (std::int32_t dy = -1; dy <= 1; ++dy) {
    for (std::int32_t dx = -1; dx <= 1; ++dx) {
      const auto it = door_tiles_.find(CellKey{x + dx, y + dy});
      if (it == door_tiles_.end()) {
        continue;
      }
      const auto core_index = it->second.core_index;
      // 跳过已处理的门核心（避免重复打开同一扇门）
      if (std::find(opened_cores.begin(), opened_cores.end(), core_index) != opened_cores.end()) {
        continue;
      }
      if (core_index >= door_cores_.size()) {
        continue;
      }
      auto& core = door_cores_[core_index];
      if (core.locked || core.open) {
        continue;
      }
      // 打开门
      core.open = true;
      core.open_time_ms = now_ms;
      opened_cores.push_back(core_index);
      opened.insert(opened.end(), core.tiles.begin(), core.tiles.end());
    }
  }
  return opened;
}

/**
 * @brief 关闭到期的门
 * @details 检查所有门核心，关闭打开时间已超过 ttl_ms 的门。
 * @param now_ms 当前时间戳
 * @param ttl_ms 门保持打开的最长时间
 * @return 被关闭的门瓦片坐标列表
 */
std::vector<std::pair<std::int32_t, std::int32_t>> LegacyMapEnvironment::close_expired_doors(
    std::uint64_t now_ms, std::uint64_t ttl_ms) {
  std::vector<CellKey> closed;
  for (auto& core : door_cores_) {
    if (!core.open || core.open_time_ms == 0 || now_ms <= core.open_time_ms + ttl_ms) {
      continue;
    }
    core.open = false;
    core.open_time_ms = 0;
    closed.insert(closed.end(), core.tiles.begin(), core.tiles.end());
  }
  return closed;
}

/**
 * @brief 检查指定位置的门是否打开
 * @param x X坐标
 * @param y Y坐标
 * @return true 门打开或不存在，false 门关闭
 */
bool LegacyMapEnvironment::door_is_open(std::int32_t x, std::int32_t y) const {
  const auto* core = door_core_at(x, y);
  return core != nullptr && core->open;
}

/**
 * @brief 获取指定位置的最新物品ID
 * @details 倒序遍历对象列表（最新添加的在末尾），返回第一个物品对象的ID。
 * @param x X坐标
 * @param y Y坐标
 * @return 物品ID的optional值
 */
std::optional<std::uint64_t> LegacyMapEnvironment::first_item_object_id(std::int32_t x,
                                                                        std::int32_t y) const {
  const auto* target = cell(x, y);
  if (target == nullptr) {
    return std::nullopt;
  }
  // 倒序遍历，最新的物品在列表末尾
  for (auto it = target->obj_list.rbegin(); it != target->obj_list.rend(); ++it) {
    const auto& object = *it;
    if (object.shape == LegacyMapObjectShape::item_object) {
      return object.object_id;
    }
  }
  return std::nullopt;
}

/**
 * @brief 获取指定位置的物品数量
 * @param x X坐标
 * @param y Y坐标
 * @return 物品数量的optional值，不可通行时返回std::nullopt
 */
std::optional<std::size_t> LegacyMapEnvironment::item_object_count(std::int32_t x,
                                                                   std::int32_t y) const {
  if (!static_can_move(x, y)) {
    return std::nullopt;
  }
  const auto* target = cell(x, y);
  if (target == nullptr) {
    return 0;
  }
  return static_cast<std::size_t>(
      std::count_if(target->obj_list.begin(), target->obj_list.end(),
                    [](const LegacyMapObject& object) {
                      return object.shape == LegacyMapObjectShape::item_object;
                    }));
}

/**
 * @brief 按顺序获取地图上所有物品的ID
 * @details 遍历所有非空格子，收集所有物品对象的ID。
 * @return 物品ID列表
 */
std::vector<std::uint64_t> LegacyMapEnvironment::item_object_ids_in_order() const {
  std::vector<std::uint64_t> ids;
  for (const auto& [_, target] : cells_) {
    for (const auto& object : target.obj_list) {
      if (object.shape == LegacyMapObjectShape::item_object) {
        ids.push_back(object.object_id);
      }
    }
  }
  return ids;
}

/**
 * @brief 获取指定位置的格子（const版本）
 * @param x X坐标
 * @param y Y坐标
 * @return 格子指针，该位置无对象时返回 nullptr
 */
const LegacyMapEnvironment::Cell* LegacyMapEnvironment::cell(std::int32_t x,
                                                             std::int32_t y) const {
  const auto it = cells_.find(CellKey{x, y});
  return it != cells_.end() ? &it->second : nullptr;
}

/**
 * @brief 获取指定位置的可变格子指针
 * @details 如果该位置还没有格子，会自动创建一个空格子。
 * @param x X坐标
 * @param y Y坐标
 * @return 格子指针，越界时返回 nullptr
 */
LegacyMapEnvironment::Cell* LegacyMapEnvironment::mutable_cell(std::int32_t x,
                                                               std::int32_t y) {
  if (!in_bounds(x, y)) {
    return nullptr;
  }
  return &cells_[CellKey{x, y}];
}

/**
 * @brief 如果格子为空则删除
 * @param key 格子坐标键
 */
void LegacyMapEnvironment::erase_cell_if_empty(CellKey key) {
  const auto it = cells_.find(key);
  if (it != cells_.end() && it->second.obj_list.empty()) {
    cells_.erase(it);
  }
}

/**
 * @brief 从地图文档加载门数据
 * @details 遍历地图的所有格子，读取 door_index 标志：
 *          - 0x80位：标记该格子是否为门的一部分
 *          - 0x7F：门编号
 *
 *          门核心分组逻辑：
 *          1. 扫描已有门核心，找相同编号且距离 <= 10 的核心加入
 *          2. 如果找不到匹配的核心，创建新的核心
 *          3. 将门瓦片注册到对应的核心和 tile 映射表中
 *
 *          门核心用于控制一组门瓦片的统一开关。
 */
void LegacyMapEnvironment::load_doors_from_map() {
  if (movement_map_ == nullptr) {
    return;
  }

  for (std::int32_t y = 0; y < movement_map_->height; ++y) {
    for (std::int32_t x = 0; x < movement_map_->width; ++x) {
      const auto* map_cell = movement_map_->cell(x, y);
      if (map_cell == nullptr || (map_cell->door_index & 0x80u) == 0) {
        continue;
      }
      const auto door_number = static_cast<std::int32_t>(map_cell->door_index & 0x7fu);
      if (door_number <= 0) {
        continue;
      }

      // 查找匹配的门核心（相同编号且距离<=10）
      std::optional<std::size_t> core_index;
      for (std::size_t index = 0; index < door_cores_.size(); ++index) {
        const auto& core = door_cores_[index];
        if (core.number != door_number) {
          continue;
        }
        // 检查与现有核心的距离
        const auto near_existing = std::any_of(core.tiles.begin(), core.tiles.end(),
                                               [&](const CellKey& tile) {
                                                 return std::abs(tile.first - x) <= 10 &&
                                                        std::abs(tile.second - y) <= 10;
                                               });
        if (near_existing) {
          core_index = index;
          break;
        }
      }
      // 创建新的门核心
      if (!core_index.has_value()) {
        DoorCore core;
        core.number = door_number;
        door_cores_.push_back(std::move(core));
        core_index = door_cores_.size() - 1;
      }
      door_cores_[*core_index].tiles.push_back(CellKey{x, y});
      door_tiles_[CellKey{x, y}] = DoorTile{door_number, *core_index};
    }
  }
}

/**
 * @brief 获取指定位置的门核心（可变版本）
 * @param x X坐标
 * @param y Y坐标
 * @return 门核心指针，该位置没有门或核心索引无效时返回 nullptr
 */
LegacyMapEnvironment::DoorCore* LegacyMapEnvironment::door_core_at(std::int32_t x,
                                                                   std::int32_t y) {
  const auto it = door_tiles_.find(CellKey{x, y});
  if (it == door_tiles_.end() || it->second.core_index >= door_cores_.size()) {
    return nullptr;
  }
  return &door_cores_[it->second.core_index];
}

/**
 * @brief 获取指定位置的门核心（const版本）
 * @param x X坐标
 * @param y Y坐标
 * @return 门核心指针，该位置没有门或核心索引无效时返回 nullptr
 */
const LegacyMapEnvironment::DoorCore* LegacyMapEnvironment::door_core_at(
    std::int32_t x, std::int32_t y) const {
  const auto it = door_tiles_.find(CellKey{x, y});
  if (it == door_tiles_.end() || it->second.core_index >= door_cores_.size()) {
    return nullptr;
  }
  return &door_cores_[it->second.core_index];
}

}  // namespace mir2
