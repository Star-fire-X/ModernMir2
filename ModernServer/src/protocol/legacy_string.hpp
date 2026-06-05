/**
 * @file legacy_string.hpp
 * @brief 遗留协议字符串编解码工具
 *
 * @details 本文件定义了 Legacy 协议中使用的字符串类型和辅助函数。
 * LegacyString 和 LegacyStringView 封装了 Legacy 协议特有的字节序列表示，
 * 它们与标准 UTF-8 字符串的主要区别在于：
 * 1. 使用 Legacy 编码（基于 GBK 编码的中文支持）
 * 2. 处理的是原始字节序列而非 Unicode 字符串
 *
 * 主要功能：
 * - LegacyStringView：非拥有式的 Legacy 字符串视图
 * - LegacyString：拥有式的 Legacy 字符串
 * - copy_legacy_bytes()：复制 Legacy 编码的字节
 * - legacy_debug_hex()：将 Legacy 字符串以十六进制格式输出（调试用）
 * - split_legacy_fields()：按分隔符分割 Legacy 字符串
 * - is_valid_legacy_account_id()：验证 Legacy 账号 ID 的合法性
 * - is_valid_legacy_character_name()：验证 Legacy 角色名的合法性
 */

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace mir2 {

/**
 * @class LegacyStringView
 * @brief 非拥有式的 Legacy 字符串视图
 *
 * @details 类似 std::string_view，但表示的是 Legacy 编码的字节序列。
 * 提供基本的只读接口：bytes()、byte_size()、empty()、compare()。
 * 设计为轻量级参数传递类型，避免不必要的拷贝。
 */
class LegacyStringView {
 public:
  constexpr LegacyStringView() = default;
  constexpr explicit LegacyStringView(std::string_view bytes) : bytes_(bytes) {}

  /** @brief 获取底层字节视图 */
  [[nodiscard]] constexpr std::string_view bytes() const noexcept { return bytes_; }

  /** @brief 获取字节长度 */
  [[nodiscard]] constexpr std::size_t byte_size() const noexcept { return bytes_.size(); }

  /** @brief 是否为空 */
  [[nodiscard]] constexpr bool empty() const noexcept { return bytes_.empty(); }

  /**
   * @brief 比较两个 LegacyStringView
   * @param other 要比较的另一个字符串视图
   * @return 0 相等，<0 小于，>0 大于
   */
  [[nodiscard]] constexpr int compare(LegacyStringView other) const noexcept {
    return bytes_.compare(other.bytes_);
  }

 private:
  std::string_view bytes_{};  ///< 底层字节数据视图
};

/**
 * @class LegacyString
 * @brief 拥有式的 Legacy 字符串
 *
 * @details 类似 std::string，但表示的是 Legacy 编码的字节序列。
 * 提供 view() 方法获取非拥有式视图，take_bytes() 获取底层字符串的所有权。
 */
class LegacyString {
 public:
  LegacyString() = default;
  explicit LegacyString(std::string bytes);
  explicit LegacyString(std::string_view bytes);

  /** @brief 获取非拥有式视图 */
  [[nodiscard]] LegacyStringView view() const noexcept;

  /** @brief 获取底层字节视图 */
  [[nodiscard]] std::string_view bytes() const noexcept;

  /** @brief 获取字节长度 */
  [[nodiscard]] std::size_t byte_size() const noexcept;

  /** @brief 是否为空 */
  [[nodiscard]] bool empty() const noexcept;

  /** @brief 获取底层 const 字符串引用 */
  [[nodiscard]] const std::string& str() const noexcept;

  /** @brief 获取底层字符串的所有权（右值操作） */
  [[nodiscard]] std::string take_bytes() && noexcept;

 private:
  std::string bytes_{};  ///< 底层字节数据
};

/** @brief LegacyStringView 相等比较 */
[[nodiscard]] bool operator==(LegacyStringView lhs, LegacyStringView rhs) noexcept;

/** @brief LegacyStringView 不等比较 */
[[nodiscard]] bool operator!=(LegacyStringView lhs, LegacyStringView rhs) noexcept;

/**
 * @brief 复制 Legacy 编码的字节
 * @details 将 std::string_view 简单复制为 std::string，不进行编码转换
 * @param bytes Legacy 编码的字节视图
 * @return std::string 复制后的字符串
 */
[[nodiscard]] std::string copy_legacy_bytes(std::string_view bytes);

/**
 * @brief 将 LegacyStringView 以十六进制格式输出（调试用）
 * @param value Legacy 字符串视图
 * @return std::string 十六进制表示的字符串
 */
[[nodiscard]] std::string legacy_debug_hex(LegacyStringView value);

/**
 * @brief 按分隔符分割 Legacy 字符串
 * @param value 要分割的 Legacy 字符串视图
 * @param delimiter 分隔符
 * @return std::vector<LegacyString> 分割后的字段列表
 */
[[nodiscard]] std::vector<LegacyString> split_legacy_fields(LegacyStringView value,
                                                            char delimiter);

/**
 * @brief 验证 Legacy 编码的账号 ID 是否合法
 * @details 允许 ASCII 字母数字（0-9, A-Z, a-z）以及 GBK 编码的中文字符
 *         （0xB0-0xC8 区段 + 0xA1-0xFE 尾部）。
 * @param account_id Legacy 编码的账号 ID
 * @return true 合法, false 非法
 */
[[nodiscard]] bool is_valid_legacy_account_id(LegacyStringView account_id);

/**
 * @brief 验证 Legacy 编码的角色名是否合法
 * @details 长度范围 3-14 字节，不允许控制字符（< 0x20）和 DEL（0x7F），
 *          ASCII 部分只允许字母数字，非 ASCII 部分（GBK 中文）不做
 *          详细校验。
 * @param name Legacy 编码的角色名
 * @return true 合法, false 非法
 */
[[nodiscard]] bool is_valid_legacy_character_name(LegacyStringView name);

/**
 * @brief 验证 Legacy 编码的账号 ID（std::string_view 重载）
 * @param account_id UTF-8 或 Legacy 编码的账号 ID
 * @return true 合法, false 非法
 */
[[nodiscard]] inline bool is_valid_legacy_account_id(std::string_view account_id) {
  return is_valid_legacy_account_id(LegacyStringView{account_id});
}

/**
 * @brief 验证 Legacy 编码的角色名（std::string_view 重载）
 * @param name UTF-8 或 Legacy 编码的角色名
 * @return true 合法, false 非法
 */
[[nodiscard]] inline bool is_valid_legacy_character_name(std::string_view name) {
  return is_valid_legacy_character_name(LegacyStringView{name});
}

}  // namespace mir2
