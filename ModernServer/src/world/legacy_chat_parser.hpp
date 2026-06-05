/**
 * @file legacy_chat_parser.hpp
 * @brief 传统聊天输入解析器头文件
 * @details 定义聊天输入的分类和解析结果结构，以及解析函数。
 *          支持普通聊天、私聊、组队聊天、公会聊天、喊话和GM命令等多种模式。
 *          同时支持GM广播的特殊前缀解析。
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace mir2 {

/**
 * @enum LegacyChatInputKind
 * @brief 聊天输入类型枚举
 * @details 根据输入前缀区分的聊天类型，决定消息的投递范围和格式。
 */
enum class LegacyChatInputKind {
  empty,   ///< 空输入（无内容）
  normal,  ///< 普通聊天（无特殊前缀）
  whisper, ///< 私聊（以'/'开头，格式：/目标 消息）
  group,   ///< 组队聊天（以'!!'开头）
  guild,   ///< 公会聊天（以'!~'开头）
  shout,   ///< 喊话（以'!'开头，非'!!'和'!~'）
  command  ///< 命令（以'@'开头）
};

/**
 * @enum LegacyGmBroadcastKind
 * @brief GM广播类型枚举
 * @details GM使用特殊前缀进行不同范围的广播：
 *          @! 跨服全局广播，@$ 本服全局广播，@# 地图广播
 */
enum class LegacyGmBroadcastKind {
  none,                      ///< 非GM广播
  sysop_global_interserver,  ///< 跨服全局广播 (@!)
  sysop_global_local,        ///< 本服全局广播 (@$)
  sysop_map                  ///< 当前地图广播 (@#)
};

/**
 * @struct LegacyTokenParse
 * @brief 分词解析结果
 * @details 从字符串中提取第一个token和剩余部分。
 */
struct LegacyTokenParse {
  std::string token{}; ///< 提取到的第一个token
  std::string rest{};  ///< 剩余未解析的字符串
};

/**
 * @struct LegacyChatParseResult
 * @brief 聊天解析结果
 * @details 包含聊天输入的所有解析信息，包括类型、原始文本、消息体、
 *          目标名称、命令名称和参数等。
 */
struct LegacyChatParseResult {
  LegacyChatInputKind kind{LegacyChatInputKind::empty}; ///< 聊天类型
  LegacyGmBroadcastKind gm_broadcast{LegacyGmBroadcastKind::none}; ///< GM广播类型
  std::string original_text{};  ///< 原始输入文本
  std::string body_text{};      ///< 去除前缀后的主体文本
  std::string message_text{};   ///< 最终消息内容（部分类型与body_text相同）
  std::string target_name{};    ///< 目标名称（私聊时使用）
  std::string command_name{};   ///< 命令名称（命令模式时使用）
  std::vector<std::string> command_args{}; ///< 命令参数列表
  std::string broadcast_text{}; ///< 广播文本（GM广播时使用）
};

/**
 * @brief 从文本中提取第一个有效token（兼容传奇3的GetValidStr3函数）
 * @details 根据指定的分隔符集，从文本中解析出第一个非分隔符token及其剩余部分。
 *          行为与传奇3客户端的 GetValidStr3 函数兼容。
 *          - 忽略前导分隔符
 *          - 遇到分隔符时停止并返回已收集的token
 *          - 文本长度超过缓冲区大小时返回空结果
 * @param text 待解析的输入文本
 * @param dividers 分隔符字符集
 * @return LegacyTokenParse 包含token和剩余部分
 */
[[nodiscard]] LegacyTokenParse legacy_get_valid_str3(std::string_view text,
                                                     std::string_view dividers);

/**
 * @brief 解析传统聊天输入
 * @details 根据消息首字符判断聊天类型，并提取相应字段：
 *          - @: 命令模式，进一步检查第二个字符判断是否为GM广播
 *          - /: 私聊模式，提取目标和消息内容
 *          - !: 喊话模式，检查是否为 !!（组队）或 !~（公会）
 *          - 其他: 普通聊天
 * @param text 原始聊天输入
 * @return LegacyChatParseResult 包含完整的解析结果
 */
[[nodiscard]] LegacyChatParseResult parse_legacy_chat_input(std::string_view text);

}  // namespace mir2
