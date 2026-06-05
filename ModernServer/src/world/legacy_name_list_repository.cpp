/**
 * @file legacy_name_list_repository.cpp
 * @brief 管理员/GM名单管理仓库实现
 * @details 实现了基于文件的名称列表管理，支持自动加载、持久化和
 *          名称标准化（小写不敏感）。文件名中的特殊字符使用
 *          URL编码（%XX）转义以确保跨平台兼容性。
 */

#include "world/legacy_name_list_repository.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <vector>

#include "util/string_utils.hpp"

namespace mir2 {

/**
 * @brief 匿名命名空间，定义内部辅助函数
 */
namespace {

/**
 * @brief 转义文件路径组件中的特殊字符
 * @details 将非字母数字的字符使用URL编码（%XX）转义，
 *          确保文件名在不同文件系统上均有效。
 *          保留的字符：字母、数字、'-'、'_'、'.'
 *          如果结果为空返回"default"。
 * @param value 原始字符串
 * @return 转义后的文件名组件
 */
std::string escape_file_component(std::string_view value) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string escaped;
  for (const auto ch : value) {
    const auto byte = static_cast<unsigned char>(ch);
    if (std::isalnum(byte) != 0 || ch == '-' || ch == '_' || ch == '.') {
      escaped.push_back(static_cast<char>(ch));
      continue;
    }
    escaped.push_back('%');
    escaped.push_back(kHex[(byte >> 4) & 0x0f]);
    escaped.push_back(kHex[byte & 0x0f]);
  }
  return escaped.empty() ? std::string{"default"} : escaped;
}

}  // namespace

/**
 * @brief 构造函数
 * @param root 名单文件存储根目录，默认为空路径（不持久化）
 */
LegacyNameListRepository::LegacyNameListRepository(std::filesystem::path root)
    : root_(std::move(root)) {}

/**
 * @brief 检查列表中是否包含指定条目
 * @details 如果列表尚未在内存中加载，自动从磁盘文件加载。
 *          比较时使用标准化后的名称（小写、去除首尾空格）。
 * @param list_name 列表名称
 * @param subject 要检查的条目
 * @return true 存在，false 不存在
 */
bool LegacyNameListRepository::contains(std::string_view list_name, std::string_view subject) {
  const auto key = normalize_key(list_name);
  const auto wanted = normalize_subject(subject);
  const auto& list = mutable_list(key);
  return list.find(wanted) != list.end();
}

/**
 * @brief 向列表中添加条目
 * @details 添加后自动将列表持久化到磁盘文件。
 * @param list_name 列表名称
 * @param subject 要添加的条目
 * @return 添加后的列表总大小
 */
std::size_t LegacyNameListRepository::add(std::string_view list_name, std::string_view subject) {
  const auto key = normalize_key(list_name);
  auto& list = mutable_list(key);
  list.insert(normalize_subject(subject));
  save(key);
  return list.size();
}

/**
 * @brief 从列表中移除条目
 * @details 移除后自动将列表持久化到磁盘文件。
 * @param list_name 列表名称
 * @param subject 要移除的条目
 * @return 移除后的列表总大小
 */
std::size_t LegacyNameListRepository::remove(std::string_view list_name, std::string_view subject) {
  const auto key = normalize_key(list_name);
  auto& list = mutable_list(key);
  list.erase(normalize_subject(subject));
  save(key);
  return list.size();
}

/**
 * @brief 获取列表中的条目数量
 * @param list_name 列表名称
 * @return 条目数量
 */
std::size_t LegacyNameListRepository::size(std::string_view list_name) {
  const auto key = normalize_key(list_name);
  return mutable_list(key).size();
}

/**
 * @brief 标准化列表键名
 * @details 转为小写并去除首尾空格，使列表名大小写不敏感。
 * @param value 原始键名
 * @return 标准化后的键名
 */
std::string LegacyNameListRepository::normalize_key(std::string_view value) {
  return util::lower_copy(util::trim(std::string(value)));
}

/**
 * @brief 标准化条目名称
 * @details 转为小写并去除首尾空格，使名称比较大小写不敏感。
 * @param value 原始条目
 * @return 标准化后的条目
 */
std::string LegacyNameListRepository::normalize_subject(std::string_view value) {
  return util::lower_copy(util::trim(std::string(value)));
}

/**
 * @brief 获取列表对应的文件路径
 * @details 路径格式：{root}/{转义后的键名}.txt
 * @param key 标准化后的键名
 * @return 文件路径
 */
std::filesystem::path LegacyNameListRepository::list_path(std::string_view key) const {
  return root_ / (escape_file_component(key) + ".txt");
}

/**
 * @brief 获取可变列表引用
 * @details 如果列表尚未加载，自动从磁盘加载。
 * @param key 列表键名
 * @return 列表的 unordered_set 引用
 */
std::unordered_set<std::string>& LegacyNameListRepository::mutable_list(std::string_view key) {
  const auto normalized = normalize_key(key);
  if (loaded_.find(normalized) == loaded_.end()) {
    load(normalized);
  }
  return lists_[normalized];
}

/**
 * @brief 从磁盘加载列表
 * @details 读取文件，每行作为一个条目。
 *          条目自动标准化（小写、去除首尾空格），空行跳过。
 *          如果 root_ 为空（未配置存储目录），跳过加载。
 * @param key 列表键名
 */
void LegacyNameListRepository::load(std::string_view key) {
  const auto normalized = normalize_key(key);
  loaded_.insert(normalized);
  if (root_.empty()) {
    return;
  }
  std::ifstream file(list_path(normalized), std::ios::binary);
  if (!file) {
    return;
  }
  auto& list = lists_[normalized];
  std::string line;
  while (std::getline(file, line)) {
    auto subject = normalize_subject(line);
    if (!subject.empty()) {
      list.insert(std::move(subject));
    }
  }
}

/**
 * @brief 将列表保存到磁盘
 * @details 将列表内容按字母顺序排序后写入文件，每行一个条目。
 *          如果 root_ 为空（未配置存储目录），跳过保存。
 * @param key 列表键名
 */
void LegacyNameListRepository::save(std::string_view key) const {
  const auto normalized = normalize_key(key);
  if (root_.empty()) {
    return;
  }
  std::error_code ignored;
  std::filesystem::create_directories(root_, ignored);
  std::ofstream file(list_path(normalized), std::ios::binary | std::ios::trunc);
  if (!file) {
    return;
  }
  const auto list_it = lists_.find(normalized);
  if (list_it == lists_.end()) {
    return;
  }
  // 排序后输出，确保文件内容稳定可读
  std::vector<std::string> subjects(list_it->second.begin(), list_it->second.end());
  std::sort(subjects.begin(), subjects.end());
  for (const auto& subject : subjects) {
    file << subject << '\n';
  }
}

}  // namespace mir2
