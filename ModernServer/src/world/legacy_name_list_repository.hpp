/**
 * @file legacy_name_list_repository.hpp
 * @brief 管理员/GM名单管理仓库头文件
 * @details 定义LegacyNameListRepository类，用于管理各种类型的名单列表。
 *          支持将名单持久化到文件系统，每个列表存储为一个txt文件。
 *          所有名称均标准化（小写、去除首尾空格）后存储和比较。
 *          常用于管理黑名单、管理员名单、封禁名单等。
 */

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace mir2 {

/**
 * @class LegacyNameListRepository
 * @brief 名称列表仓库
 * @details 基于文件的名称列表管理系统，核心特性：
 *
 *          - 延迟加载：首次访问列表时从磁盘加载
 *          - 自动持久化：每次修改后自动写回磁盘
 *          - 大小写不敏感：所有名称标准化为小写存储
 *          - 文件编码兼容：特殊字符使用URL编码转义
 *          - 线程安全未保证：应在单线程环境中使用
 *
 *          典型用途：
 *          - 封禁IP列表
 *          - 禁止登录的账号列表
 *          - GM/管理员名单
 *          - 聊天过滤词列表
 *          - 白名单系统
 *
 *          文件存储格式：每个列表一个txt文件，每行一个名称，
 *          名称已标准化（小写），按字母顺序排序。
 */
class LegacyNameListRepository {
 public:
  /**
   * @brief 构造函数
   * @param root 名单文件存储根目录，为空时不进行持久化
   */
  explicit LegacyNameListRepository(std::filesystem::path root = {});

  /**
   * @brief 检查列表中是否包含指定条目
   * @details 如果列表尚未加载，自动从磁盘加载。
   * @param list_name 列表名称
   * @param subject 要检查的条目
   * @return true 包含，false 不包含
   */
  [[nodiscard]] bool contains(std::string_view list_name, std::string_view subject);

  /**
   * @brief 向列表中添加条目
   * @details 添加后自动持久化到磁盘。
   * @param list_name 列表名称
   * @param subject 要添加的条目
   * @return 添加后的列表大小
   */
  [[nodiscard]] std::size_t add(std::string_view list_name, std::string_view subject);

  /**
   * @brief 从列表中移除条目
   * @details 移除后自动持久化到磁盘。
   * @param list_name 列表名称
   * @param subject 要移除的条目
   * @return 移除后的列表大小
   */
  [[nodiscard]] std::size_t remove(std::string_view list_name, std::string_view subject);

  /**
   * @brief 获取列表的大小
   * @param list_name 列表名称
   * @return 列表中的条目数量
   */
  [[nodiscard]] std::size_t size(std::string_view list_name);

 private:
  /**
   * @brief 标准化键名：小写 + 去除首尾空格
   * @param value 原始键名
   * @return 标准化后的键名
   */
  [[nodiscard]] static std::string normalize_key(std::string_view value);

  /**
   * @brief 标准化条目：小写 + 去除首尾空格
   * @param value 原始条目
   * @return 标准化后的条目
   */
  [[nodiscard]] static std::string normalize_subject(std::string_view value);

  /**
   * @brief 获取列表对应的文件路径
   * @details 文件名 = 转义后的键名 + ".txt"
   * @param key 标准化后的列表键名
   * @return 完整的文件路径
   */
  [[nodiscard]] std::filesystem::path list_path(std::string_view key) const;

  /**
   * @brief 获取可变列表引用（自动加载）
   * @param key 列表键名
   * @return 列表的引用
   */
  std::unordered_set<std::string>& mutable_list(std::string_view key);

  /**
   * @brief 从磁盘加载列表
   * @param key 列表键名
   */
  void load(std::string_view key);

  /**
   * @brief 将列表保存到磁盘
   * @param key 列表键名
   */
  void save(std::string_view key) const;

  std::filesystem::path root_{};                                    ///< 文件存储根目录
  std::unordered_map<std::string, std::unordered_set<std::string>> lists_{}; ///< 内存中的列表缓存
  std::unordered_set<std::string> loaded_{};                        ///< 已从磁盘加载的列表标记
};

}  // namespace mir2
