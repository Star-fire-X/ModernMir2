/**
 * @file make_index_allocator.hpp
 * @brief 唯一ID/MakeIndex 分配器头文件
 * @details 定义MakeIndexAllocator类，用于分配和管理游戏中的唯一物品索引（MakeIndex）。
 *          MakeIndex 是传奇3中用于唯一标识每个物品实例的整数ID。
 *          分配器通过观察现有物品的MakeIndex来确定下一个可用ID，
 *          避免与已有物品的ID冲突。
 */

#pragma once

#include <cstdint>

#include "core/messages.hpp"

namespace mir2 {

/**
 * @class MakeIndexAllocator
 * @brief 物品唯一索引（MakeIndex）分配器
 * @details 负责分配和追踪游戏中所有物品实例的唯一标识符（MakeIndex）。
 *
 *          核心功能：
 *          - 观察（observe）：扫描现有物品、角色、商人的记录，
 *            记录已使用的 MakeIndex，确保新分配的 ID 不冲突
 *          - 分配（allocate）：返回下一个可用的 MakeIndex 并自增
 *          - 重置（reset）：重置分配器到指定起始值
 *
 *          MakeIndex 范围说明：
 *          - < 200000：保留给数据库/配置文件中的静态物品
 *          - >= 200000（kLegacyRuntimeFloor）：运行时动态分配的物品
 *          - 最大值限制：std::numeric_limits<std::int32_t>::max()
 *
 *          使用示例：
 *          @code
 *          MakeIndexAllocator allocator;
 *          allocator.observe(saved_character);
 *          int32_t new_id = allocator.allocate();  // 分配新ID
 *          @endcode
 */
class MakeIndexAllocator {
 public:
  /** @brief 运行时分配的起始值（所有动态物品的MakeIndex >= 此值） */
  static constexpr std::int32_t kLegacyRuntimeFloor = 200000;

  /**
   * @brief 重置分配器的起始值
   * @param floor 新的起始值，不能低于 kLegacyRuntimeFloor
   */
  void reset(std::int32_t floor = kLegacyRuntimeFloor);

  /**
   * @brief 观察一个 MakeIndex 值
   * @details 记录已使用的 MakeIndex，确保后续分配不会重复。
   *          如果 make_index >= next_make_index_ 则更新 next_make_index_。
   * @param make_index 要观察的索引值
   */
  void observe(std::int32_t make_index);

  /**
   * @brief 观察一个物品的 MakeIndex
   * @details 如果是非空物品，记录其 MakeIndex。
   * @param item 用户物品数据
   */
  void observe(const LegacyUserItem& item);

  /**
   * @brief 观察角色的所有物品
   * @details 扫描角色的装备栏、背包和仓库中的所有物品。
   * @param character 角色记录
   */
  void observe(const CharacterRecord& character);

  /**
   * @brief 观察商人的所有商品和升级记录
   * @details 扫描商人的商品列表和武器升级记录中的物品。
   * @param merchant_state 商人状态记录
   */
  void observe(const MerchantStateRecord& merchant_state);

  /**
   * @brief 分配一个新的 MakeIndex
   * @details 返回当前 next_make_index_ 值并递增。
   *          如果已达最大值，返回最大值不再递增。
   * @return 新的唯一 MakeIndex
   */
  [[nodiscard]] std::int32_t allocate();

  /**
   * @brief 获取下一个可用的 MakeIndex 值（不分配）
   * @return 下一个可用值
   */
  [[nodiscard]] std::int32_t next_value() const { return next_make_index_; }

 private:
  std::int32_t next_make_index_{kLegacyRuntimeFloor}; ///< 下一个可分配的索引值
};

}  // namespace mir2
