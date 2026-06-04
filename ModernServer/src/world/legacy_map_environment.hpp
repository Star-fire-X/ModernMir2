/**
 * @file legacy_map_environment.hpp
 * @brief 地图环境管理器头文件
 * @details 定义了地图环境中的对象类型和LegacyMapEnvironment类，
 *          用于管理地图上的移动对象（角色/怪物）、物品、门、事件等。
 *          提供移动、碰撞检测、门开关控制、物品堆叠等功能。
 *          兼容传奇3原版地图的通行判断逻辑和门系统。
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "shared/legacy/map_document.hpp"

namespace mir2 {

/** @brief 地图上可放置的最大金币数量（5000万） */
constexpr std::int32_t kLegacyBagGold = 50000000;

/**
 * @enum LegacyMapObjectShape
 * @brief 地图对象形状类型枚举
 * @details 区分地图上不同类型的对象，影响碰撞检测和交互逻辑。
 */
enum class LegacyMapObjectShape : std::uint8_t {
  moving_object, ///< 移动对象（角色、怪物、宠物等）
  item_object,   ///< 物品对象（掉落在地上的物品/金币）
  gate_object,   ///< 门/传送门对象
  event_object   ///< 事件对象（火墙、陷阱等区域效果）
};

/**
 * @enum LegacyMapPlacementPolicy
 * @brief 地图对象放置策略枚举
 * @details 控制对象只能放在可通行位置或不可通行位置。
 */
enum class LegacyMapPlacementPolicy : std::uint8_t {
  passable_only, ///< 仅放置在可通行的位置
  blocked_only   ///< 仅放置在不可通行的位置
};

/**
 * @struct LegacyMovingObjectState
 * @brief 移动对象状态
 * @details 描述移动对象（角色/怪物）的当前状态，影响碰撞检测和其他交互逻辑。
 */
struct LegacyMovingObjectState {
  bool ghost{false};            ///< 幽灵模式（不产生碰撞）
  bool hold_place{true};        ///< 占据位置（阻挡他人通过）
  bool death{false};            ///< 死亡状态（不阻挡）
  bool hide_mode{false};        ///< 隐藏模式（隐身）
  bool supervisor_mode{false};  ///< 管理员模式（GM）
};

/**
 * @struct LegacyMapItemState
 * @brief 地图物品状态
 * @details 描述地面上物品的状态，包括是否为金币及金币数量。
 */
struct LegacyMapItemState {
  bool is_gold{false};      ///< 是否为金币
  std::int32_t gold_amount{0}; ///< 金币数量
};

/**
 * @struct LegacyMapGateState
 * @brief 地图门/传送门状态
 * @details 描述一个门的传送目标和开门条件。
 */
struct LegacyMapGateState {
  std::string target_map_id{}; ///< 目标地图ID
  std::int32_t target_x{0};    ///< 目标X坐标
  std::int32_t target_y{0};    ///< 目标Y坐标
  bool require_doors_open{true}; ///< 是否需要门打开才能通过
};

/**
 * @struct LegacyMapObject
 * @brief 地图对象通用结构
 * @details 描述地图上的一个对象，包含形状、ID、时间戳和各类状态。
 */
struct LegacyMapObject {
  LegacyMapObjectShape shape{LegacyMapObjectShape::moving_object}; ///< 对象形状类型
  std::uint64_t object_id{0};   ///< 对象唯一ID
  std::uint64_t a_time_ms{0};   ///< 最后活跃时间戳（毫秒）
  bool blocks_walk{false};      ///< 是否阻挡行走
  LegacyMovingObjectState moving{}; ///< 移动对象状态
  LegacyMapItemState item{};    ///< 物品状态
  LegacyMapGateState gate{};    ///< 门状态
};

/**
 * @struct LegacyMapAddResult
 * @brief 添加地图对象的结果
 * @details 包含添加操作的结果信息，如是否成功、是否合并、对象ID和合并后的金币数。
 */
struct LegacyMapAddResult {
  bool ok{false};              ///< 操作是否成功
  bool merged{false};          ///< 是否与现有物品合并（仅金币）
  std::uint64_t object_id{0};  ///< 对象ID（合并时为被合并对象的ID）
  std::int32_t merged_gold_amount{0}; ///< 合并后的金币总数
};

/**
 * @class LegacyMapEnvironment
 * @brief 地图环境管理器
 * @details 管理单个地图实例的运行时环境，核心功能：
 *
 *          - 对象管理：在网格上添加/移动/删除各类地图对象
 *          - 移动检测：检查位置是否可通行（考虑静态地形和动态对象）
 *          - 飞行线检测：检查两点之间是否可直线飞行（用于远程攻击和技能）
 *          - 门系统：管理门的开关状态、核心分组、开门/关门
 *          - 物品管理：物品掉落、金币合并、堆叠限制（最多5个）
 *          - 时间验证：更新对象活跃时间，用于超时清理
 *
 *          地图使用惰性网格（sparse grid）存储，只在实际有对象的坐标创建格子。
 *          静态通行数据由 legacy::MapDocument 提供。
 */
class LegacyMapEnvironment {
 public:
  /**
   * @struct Cell
   * @brief 地图单元格
   * @details 每个格子包含一个对象列表，支持一个位置上有多个对象共存。
   */
  struct Cell {
    std::vector<LegacyMapObject> obj_list{}; ///< 该位置上的对象列表
  };

  LegacyMapEnvironment() = default;

  /**
   * @brief 构造函数
   * @param width 地图宽度
   * @param height 地图高度
   * @param movement_map 静态通行地图数据（共享指针）
   */
  LegacyMapEnvironment(std::int32_t width, std::int32_t height,
                       std::shared_ptr<const legacy::MapDocument> movement_map = {});

  /**
   * @brief 重置地图环境
   * @details 清除所有动态对象并重新加载门数据。
   * @param width 地图宽度
   * @param height 地图高度
   * @param movement_map 静态通行地图数据
   */
  void reset(std::int32_t width, std::int32_t height,
             std::shared_ptr<const legacy::MapDocument> movement_map = {});

  // @{ 边界和通行检测
  [[nodiscard]] bool in_bounds(std::int32_t x, std::int32_t y) const;                     ///< 坐标是否在地图范围内
  [[nodiscard]] bool static_can_move(std::int32_t x, std::int32_t y) const;               ///< 静态地形是否可通行
  [[nodiscard]] bool static_can_fly(std::int32_t x, std::int32_t y) const;                ///< 静态地形是否可飞行
  [[nodiscard]] bool can_walk(std::int32_t x, std::int32_t y, bool allow_dup) const;     ///< 是否可以走到该格
  [[nodiscard]] bool can_fly_line(std::int32_t from_x, std::int32_t from_y,               ///< 两点间是否可直线飞行（移动用）
                                  std::int32_t to_x, std::int32_t to_y) const;
  [[nodiscard]] bool can_fire_fly_line(std::int32_t from_x, std::int32_t from_y,          ///< 两点间是否可直线飞行（远程攻击用）
                                       std::int32_t to_x, std::int32_t to_y) const;
  // @}

  // @{ 对象增删改查
  [[nodiscard]] bool add_moving_object(std::int32_t x, std::int32_t y, std::uint64_t object_id,
                                       std::uint64_t now_ms,
                                       LegacyMovingObjectState state = {});         ///< 添加移动对象
  [[nodiscard]] int move_to_moving_object(std::int32_t old_x, std::int32_t old_y,   ///< 移动对象到新位置
                                          std::uint64_t object_id, std::int32_t new_x,
                                          std::int32_t new_y, bool allow_dup,
                                          std::uint64_t now_ms,
                                          LegacyMovingObjectState state = {});
  [[nodiscard]] LegacyMapAddResult add_item_object(std::int32_t x, std::int32_t y,   ///< 添加物品对象
                                                   std::uint64_t object_id,
                                                   LegacyMapItemState item,
                                                   std::uint64_t now_ms);
  [[nodiscard]] bool add_placeholder_object(std::int32_t x, std::int32_t y,         ///< 添加占位对象（事件等）
                                            LegacyMapObjectShape shape,
                                            std::uint64_t object_id,
                                            std::uint64_t now_ms,
                                            LegacyMapPlacementPolicy placement_policy =
                                                LegacyMapPlacementPolicy::passable_only,
                                            bool blocks_walk = false);
  [[nodiscard]] bool add_gate_object(std::int32_t x, std::int32_t y,                ///< 添加门对象
                                     std::uint64_t object_id,
                                     LegacyMapGateState gate,
                                     std::uint64_t now_ms);
  [[nodiscard]] int delete_from_map(std::int32_t x, std::int32_t y,                 ///< 从地图删除对象
                                    LegacyMapObjectShape shape, std::uint64_t object_id);
  bool verify_map_time(std::int32_t x, std::int32_t y, std::uint64_t object_id,     ///< 验证并更新对象活跃时间
                       std::uint64_t now_ms);
  [[nodiscard]] const LegacyMapObject* gate_at(std::int32_t x, std::int32_t y) const; ///< 获取指定位置的门
  // @}

  // @{ 门系统
  [[nodiscard]] bool around_door_opened(std::int32_t x, std::int32_t y) const;              ///< 周围的门是否全部打开
  [[nodiscard]] std::vector<std::pair<std::int32_t, std::int32_t>> open_doors_around(       ///< 打开周围的门
      std::int32_t x, std::int32_t y, std::uint64_t now_ms);
  [[nodiscard]] std::vector<std::pair<std::int32_t, std::int32_t>> close_expired_doors(     ///< 关闭到期的门
      std::uint64_t now_ms, std::uint64_t ttl_ms);
  [[nodiscard]] std::size_t door_core_count() const { return door_cores_.size(); }          ///< 获取门核心数量
  [[nodiscard]] bool door_is_open(std::int32_t x, std::int32_t y) const;                    ///< 指定位置的门是否打开
  // @}

  // @{ 物品查询
  [[nodiscard]] std::optional<std::uint64_t> first_item_object_id(std::int32_t x,           ///< 获取该位置最新掉落的物品ID
                                                                  std::int32_t y) const;
  [[nodiscard]] std::optional<std::size_t> item_object_count(std::int32_t x,                ///< 获取该位置的物品数量
                                                             std::int32_t y) const;
  [[nodiscard]] std::vector<std::uint64_t> item_object_ids_in_order() const;                ///< 按顺序获取所有物品ID
  [[nodiscard]] const Cell* cell(std::int32_t x, std::int32_t y) const;                     ///< 获取指定位置的格子
  // @}

 private:
  /** @brief 格子坐标键类型 */
  using CellKey = std::pair<std::int32_t, std::int32_t>;

  /**
   * @struct DoorCore
   * @brief 门核心
   * @details 一组相关联的门瓦片共享一个核心，核心控制开关状态。
   */
  struct DoorCore {
    std::int32_t number{0};            ///< 门编号
    bool open{false};                  ///< 是否打开
    bool locked{false};                ///< 是否锁定
    std::int32_t lock_key{0};          ///< 锁的钥匙ID
    std::uint64_t open_time_ms{0};     ///< 打开时间戳
    std::vector<CellKey> tiles{};      ///< 门包含的所有瓦片坐标
  };

  /**
   * @struct DoorTile
   * @brief 门瓦片
   * @details 记录单个瓦片所属的门编号和核心索引。
   */
  struct DoorTile {
    std::int32_t number{0};    ///< 门编号
    std::size_t core_index{0}; ///< 在 door_cores_ 中的索引
  };

  // @{ 私有辅助方法
  [[nodiscard]] Cell* mutable_cell(std::int32_t x, std::int32_t y);             ///< 获取可变格子指针，不存在时创建
  void erase_cell_if_empty(CellKey key);                                        ///< 如果格子为空则删除
  void load_doors_from_map();                                                   ///< 从地图文档加载门数据
  [[nodiscard]] DoorCore* door_core_at(std::int32_t x, std::int32_t y);         ///< 获取指定位置的门核心（可变）
  [[nodiscard]] const DoorCore* door_core_at(std::int32_t x, std::int32_t y) const; ///< 获取指定位置的门核心（const）
  // @}

  std::int32_t width_{0};                                          ///< 地图宽度
  std::int32_t height_{0};                                         ///< 地图高度
  std::shared_ptr<const legacy::MapDocument> movement_map_{};      ///< 静态通行地图数据
  std::map<CellKey, Cell> cells_{};                                ///< 动态对象网格（惰性分配）
  std::vector<DoorCore> door_cores_{};                             ///< 门核心列表
  std::map<CellKey, DoorTile> door_tiles_{};                       ///< 门瓦片映射
};

}  // namespace mir2
