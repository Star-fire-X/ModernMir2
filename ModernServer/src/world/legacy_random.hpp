/**
 * @file legacy_random.hpp
 * @brief 传统兼容随机数生成器
 * @details 实现了与Delphi/传奇3兼容的伪随机数生成器。
 *          使用线性同余法（Linear Congruential Generator, LCG），
 *          乘数为 0x08088405，增量为 1，模数为 2^32。
 *          生成的随机数范围：[0, range)（不包括 range）。
 *
 * @note 此随机数生成器必须与Delphi原版行为完全一致，
 *       以确保游戏逻辑的兼容性（如掉落、升级、伤害计算等）。
 *       不可替换为标准库的随机数生成器。
 */

#pragma once

#include <cstdint>

namespace mir2 {

/**
 * @class LegacyRandom
 * @brief 传统兼容伪随机数生成器
 * @details 封装了传奇3原版使用的LCG随机数算法，提供种子设置和状态查询。
 *
 *         算法：seed = seed * 0x08088405 + 1
 *         random(range) = (uint64_t(seed) * range) >> 32
 *
 *         该算法等价于Delphi的 Random(Range) 函数行为。
 *         种子初始值默认为1。
 *
 *         使用示例：
 *         @code
 *         LegacyRandom rng(12345);
 *         int value = rng.random(100);  // 返回 0-99
 *         @endcode
 */
class LegacyRandom {
 public:
  /**
   * @brief 构造函数
   * @param seed 随机数种子，默认值为1
   */
  explicit LegacyRandom(std::uint32_t seed = 1) : seed_(seed) {}

  /**
   * @brief 设置随机数种子
   * @param value 新种子值
   */
  void seed(std::uint32_t value) { seed_ = value; }

  /**
   * @brief 获取当前随机数生成器状态（种子值）
   * @return 当前种子值
   */
  [[nodiscard]] std::uint32_t state() const { return seed_; }

  /**
   * @brief 生成 [0, range) 范围内的随机整数
   * @details 算法步骤：
   *          1. 更新种子：seed = seed * 0x08088405 + 1
   *          2. 返回：(uint64_t(seed) * range) >> 32
   *
   *          高位截断法确保均匀分布（当 range 较小时）。
   *          如果 range <= 0，直接返回 0。
   *
   * @param range 范围上限（不包含）
   * @return [0, range) 内的随机整数
   */
  [[nodiscard]] std::int32_t random(std::int32_t range) {
    if (range <= 0) {
      return 0;
    }
    // LCG迭代：经典的 Delphi/传奇3 随机数算法
    seed_ = seed_ * 0x08088405u + 1u;
    // 使用高位截断法（multiply-shift）生成均匀分布
    return static_cast<std::int32_t>((static_cast<std::uint64_t>(seed_) *
                                      static_cast<std::uint32_t>(range)) >>
                                     32u);
  }

 private:
  std::uint32_t seed_{1}; ///< 当前种子值（内部状态）
};

}  // namespace mir2
