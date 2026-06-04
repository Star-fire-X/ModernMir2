/**
 * @file string_utils.hpp
 * @brief 基础字符串工具集
 * @details 提供字符串处理的常用工具函数，包括去除首尾空白、
 *          按分隔符分割、前缀检查和转换为小写等操作。
 *          所有函数均为 inline 实现，适合作为轻量级头文件库使用。
 * @note 这些函数适用于处理服务端配置文件解析和命令参数规范化等场景。
 */

#pragma once

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace mir2::util {

/**
 * @brief 去除字符串首尾的空白字符
 * @details 使用 std::isspace 判断空白字符（空格、制表符、换行、回车、换页、垂直制表符），
 *          同时去除字符串开头和结尾的空白。函数接受值参数，
 *          内部直接修改副本并返回，支持临时字符串的高效传递（移动语义）。
 * @param value 待处理的字符串（按值传递，允许调用处直接传入临时对象）
 * @return 去除首尾空白后的字符串
 * @note std::isspace 的行为对负值 char 是未定义的，因此内部使用 unsigned char
 *       进行类型转换以确保安全。
 */
inline std::string trim(std::string value) {
  auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
  // 去除开头的空白字符：找到第一个非空白字符的位置，删除之前的所有字符
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char ch) {
                return !is_space(static_cast<unsigned char>(ch));
              }));
  // 去除结尾的空白字符：找到最后一个非空白字符的位置，删除之后的所有字符
  value.erase(std::find_if(value.rbegin(), value.rend(), [&](char ch) {
                return !is_space(static_cast<unsigned char>(ch));
              }).base(),
              value.end());
  return value;
}

/**
 * @brief 按指定分隔符分割字符串
 * @details 使用 std::getline 按字符分隔符进行分割，
 *          每个分割后的片段会自动去除首尾空白。
 * @param text      待分割的字符串
 * @param delimiter 分隔符字符（通常为逗号、冒号、竖线等）
 * @return 分割后的字符串片段向量
 * @note 连续的分隔符会产生空字符串片段（经 trim 后为空字符串）。
 *       分割结果中不包含分隔符本身。
 */
inline std::vector<std::string> split(std::string_view text, char delimiter) {
  std::vector<std::string> parts;
  std::stringstream stream{std::string(text)};
  std::string item;
  while (std::getline(stream, item, delimiter)) {
    parts.push_back(trim(item));
  }
  return parts;
}

/**
 * @brief 检查字符串是否以指定前缀开头
 * @details 通过比较 text 的前 prefix.size() 个字符与 prefix 是否相等来判断。
 *          时间复杂度为 O(prefix.size())。
 * @param text   待检查的字符串
 * @param prefix 要匹配的前缀
 * @return true 如果 text 以 prefix 开头；否则返回 false
 * @note 如果 prefix 为空字符串，函数始终返回 true（空字符串是任意字符串的前缀）。
 */
inline bool starts_with(std::string_view text, std::string_view prefix) {
  return text.substr(0, prefix.size()) == prefix;
}

/**
 * @brief 将字符串转换为小写形式
 * @details 使用 std::tolower 对每个字符进行转换。
 *          支持 unsigned char 以避免 char 符号性导致的未定义行为。
 *          返回一个新字符串，不修改原始输入。
 * @param text 原始字符串
 * @return 全部转换为小写后的新字符串
 * @note std::tolower 的输入需要为非负值，否则行为未定义。
 *       通过将 char 显式转换为 unsigned char 来避免此问题。
 * @warning 此函数仅处理 ASCII 字符集的小写转换（如 'A' -> 'a'）。
 *          对于非 ASCII 字符（如带重音的拉丁字母），std::tolower 的行为
 *          依赖于当前 locale，可能不符合预期。服务端配置场景下通常够用。
 */
inline std::string lower_copy(std::string_view text) {
  std::string lowered{text};
  std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return lowered;
}

}  // namespace mir2::util
