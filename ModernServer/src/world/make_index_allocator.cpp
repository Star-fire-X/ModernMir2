/**
 * @file make_index_allocator.cpp
 * @brief 唯一ID/MakeIndex 分配器实现
 * @details 实现了物品唯一索引的分配和管理功能。
 *          通过观察现有物品来维护可分配的最小ID，
 *          确保新分配的ID不与任何已有ID冲突。
 */

#include "world/make_index_allocator.hpp"

#include <algorithm>
#include <limits>

#include "protocol/legacy_types.hpp"

namespace mir2 {

/**
 * @brief 匿名命名空间，定义内部辅助模板
 */
namespace {

/**
 * @brief 遍历容器中的所有物品并观察其MakeIndex
 * @tparam Items 容器类型（如 std::vector<LegacyUserItem>）
 * @param allocator MakeIndex分配器
 * @param items 物品容器
 */
template <typename Items>
void observe_items(MakeIndexAllocator& allocator, const Items& items) {
  for (const auto& item : items) {
    allocator.observe(item);
  }
}

}  // namespace

/**
 * @brief 重置分配器
 * @details 将下一个可用MakeIndex设为指定值。
 *          floor 不能低于 kLegacyRuntimeFloor（200000），
 *          确保运行时分配的ID与数据库静态ID范围不重叠。
 * @param floor 起始值
 */
void MakeIndexAllocator::reset(std::int32_t floor) {
  next_make_index_ = std::max(floor, kLegacyRuntimeFloor);
}

/**
 * @brief 观察一个 MakeIndex 值
 * @details 如果 make_index 合法（> 0），更新 next_make_index_ 为
 *          max(next_make_index_, make_index + 1)。
 *          特殊处理：如果 make_index 达到 int32_max，将 next_make_index_ 设为 int32_max
 *          （之后 allocate 将始终返回最大值）。
 * @param make_index 已使用的索引值
 */
void MakeIndexAllocator::observe(std::int32_t make_index) {
  if (make_index <= 0) {
    return;
  }
  if (make_index >= std::numeric_limits<std::int32_t>::max()) {
    next_make_index_ = std::numeric_limits<std::int32_t>::max();
    return;
  }
  next_make_index_ = std::max(next_make_index_, make_index + 1);
}

/**
 * @brief 观察物品的 MakeIndex
 * @details 忽略空物品（is_empty检查）。
 * @param item 用户物品数据
 */
void MakeIndexAllocator::observe(const LegacyUserItem& item) {
  if (!is_empty(item)) {
    observe(item.make_index);
  }
}

/**
 * @brief 观察角色的所有物品
 * @details 扫描装备栏、背包和仓库中的全部物品。
 * @param character 角色记录
 */
void MakeIndexAllocator::observe(const CharacterRecord& character) {
  observe_items(*this, character.equipped_items);
  observe_items(*this, character.bag_items);
  observe_items(*this, character.storage_items);
}

/**
 * @brief 观察商人的所有商品和升级记录
 * @details 扫描商人的商品列表以及武器升级记录中的物品。
 * @param merchant_state 商人状态记录
 */
void MakeIndexAllocator::observe(const MerchantStateRecord& merchant_state) {
  observe_items(*this, merchant_state.goods);
  for (const auto& record : merchant_state.weapon_upgrades) {
    observe(record.item);
  }
}

/**
 * @brief 分配一个新的 MakeIndex
 * @details 返回当前 next_make_index_ 值并递增。
 *          如果已达到 int32_max，保持返回最大值（不再递增）。
 * @return 新的唯一索引值
 */
std::int32_t MakeIndexAllocator::allocate() {
  const auto result = next_make_index_;
  if (next_make_index_ < std::numeric_limits<std::int32_t>::max()) {
    ++next_make_index_;
  }
  return result;
}

}  // namespace mir2
