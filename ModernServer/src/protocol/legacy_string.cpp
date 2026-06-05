/**
 * @file legacy_string.cpp
 * @brief 遗留协议字符串编解码工具实现
 *
 * @details 实现了 LegacyString、LegacyStringView 的成员函数
 * 以及相关的辅助函数。
 *
 * is_valid_legacy_account_id() 的验证逻辑：
 * 1. 允许 ASCII 可见字符（48-122 范围，涵盖 0-9, A-Z, a-z 及中间符号）
 * 2. 允许 GBK 编码的中文双字节（首字节 0xB0-0xC8，次字节 0xA1-0xFE）
 * 3. 其他字符均视为非法
 *
 * is_valid_legacy_character_name() 的验证逻辑：
 * 1. 长度限制为 3-14 字节
 * 2. 不允许控制字符（0x00-0x1F）和 DEL（0x7F）
 * 3. ASCII 部分只允许字母数字
 * 4. 非 ASCII 字节（GBK 中文）通过检查
 *
 * @see legacy_string.hpp
 */

#include "protocol/legacy_string.hpp"

#include <array>
#include <utility>

namespace mir2 {

namespace {

/**
 * @brief 检查字符是否为 ASCII 字母或数字
 * @param ch 要检查的字符
 * @return true 如果是字母或数字
 */
bool is_ascii_alnum(unsigned char ch) {
  return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') ||
         (ch >= 'a' && ch <= 'z');
}

}  // namespace

LegacyString::LegacyString(std::string bytes) : bytes_(std::move(bytes)) {}

LegacyString::LegacyString(std::string_view bytes) : bytes_(bytes) {}

LegacyStringView LegacyString::view() const noexcept { return LegacyStringView{bytes_}; }

std::string_view LegacyString::bytes() const noexcept { return bytes_; }

std::size_t LegacyString::byte_size() const noexcept { return bytes_.size(); }

bool LegacyString::empty() const noexcept { return bytes_.empty(); }

const std::string& LegacyString::str() const noexcept { return bytes_; }

std::string LegacyString::take_bytes() && noexcept { return std::move(bytes_); }

bool operator==(LegacyStringView lhs, LegacyStringView rhs) noexcept {
  return lhs.bytes() == rhs.bytes();
}

bool operator!=(LegacyStringView lhs, LegacyStringView rhs) noexcept { return !(lhs == rhs); }

std::string copy_legacy_bytes(std::string_view bytes) { return std::string(bytes); }

std::string legacy_debug_hex(LegacyStringView value) {
  static constexpr std::array<char, 16> kHex = {'0', '1', '2', '3', '4', '5', '6', '7',
                                                 '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
  std::string output;
  output.reserve(value.byte_size() * 2);
  for (const unsigned char ch : value.bytes()) {
    output.push_back(kHex[(ch >> 4U) & 0x0FU]);
    output.push_back(kHex[ch & 0x0FU]);
  }
  return output;
}

std::vector<LegacyString> split_legacy_fields(LegacyStringView value, char delimiter) {
  std::vector<LegacyString> fields;
  const auto bytes = value.bytes();
  std::size_t start = 0;
  while (start <= bytes.size()) {
    const auto end = bytes.find(delimiter, start);
    if (end == std::string_view::npos) {
      // 最后一段
      fields.emplace_back(bytes.substr(start));
      break;
    }
    fields.emplace_back(bytes.substr(start, end - start));
    start = end + 1;
  }
  return fields;
}

bool is_valid_legacy_account_id(LegacyStringView account_id) {
  const auto bytes = account_id.bytes();
  if (bytes.empty()) {
    return false;
  }

  for (std::size_t index = 0; index < bytes.size(); ++index) {
    const auto ch = static_cast<unsigned char>(bytes[index]);
    // 允许 ASCII 可见字符范围
    if (ch >= 48 && ch <= 122) {
      continue;
    }
    // 允许 GBK 编码的双字节中文字符
    // 首字节范围 0xB0-0xC8，次字节范围 0xA1-0xFE
    if (ch >= 0xB0 && ch <= 0xC8 && index + 1 < bytes.size()) {
      const auto next = static_cast<unsigned char>(bytes[index + 1]);
      if (next >= 0xA1 && next <= 0xFE) {
        ++index;  // 跳过双字节的第二字节
        continue;
      }
    }
    return false;
  }
  return true;
}

bool is_valid_legacy_character_name(LegacyStringView name) {
  const auto bytes = name.bytes();
  // 角色名长度限制：3-14 字节
  if (bytes.size() < 3 || bytes.size() > 14) {
    return false;
  }
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    const auto ch = static_cast<unsigned char>(bytes[index]);
    // 不允许控制字符
    if (ch < 0x20 || ch == 0x7F) {
      return false;
    }
    // 非 ASCII 字符（如中文）直接通过
    if (ch >= 0x80) {
      if (ch >= 0xB0 && ch <= 0xC8 && index + 1 < bytes.size()) {
        const auto next = static_cast<unsigned char>(bytes[index + 1]);
        if (next >= 0xA1 && next <= 0xFE) {
          ++index;
          continue;
        }
      }
      return false;
    }
    // ASCII 部分只允许字母数字
    if (!is_ascii_alnum(ch)) {
      return false;
    }
  }
  return true;
}

}  // namespace mir2
