/**
 * @file legacy_chat_parser.cpp
 * @brief 传统聊天输入解析器实现
 * @details 实现了兼容传奇3客户端的聊天输入解析逻辑。
 *          支持多种聊天模式（普通、私聊、组队、公会、喊话、命令）和GM广播。
 *          分词功能兼容传奇3的 GetValidStr3 函数行为。
 */

#include "world/legacy_chat_parser.hpp"

#include <utility>

namespace mir2 {

/**
 * @brief 匿名命名空间，定义内部辅助常量和函数
 */
namespace {

/** @brief GetValidStr3 的最大缓冲区大小（与传奇3兼容） */
constexpr std::size_t kGetValidStr3BufferSize = 20480;

/**
 * @brief 判断字符是否为分隔符
 * @param ch 待判断字符
 * @param dividers 分隔符字符集
 * @return true 是分隔符，false 不是
 */
bool is_divider(char ch, std::string_view dividers) {
  return dividers.find(ch) != std::string_view::npos;
}

/**
 * @brief 解析命令文本
 * @details 从命令主体中提取命令名称和最多6个参数。
 *          使用空格、逗号、冒号作为分隔符。
 *          此行为与传奇3服务器端命令解析兼容。
 * @param body 去除'@'前缀后的命令文本
 * @param result 输���参数，填写command_name和command_args字段
 */
void parse_legacy_command(std::string_view body, LegacyChatParseResult& result) {
  // 提取命令名称
  auto parsed = legacy_get_valid_str3(body, " ,:");
  result.command_name = std::move(parsed.token);

  // 提取第一个参数
  parsed = legacy_get_valid_str3(parsed.rest, " ,:");
  if (!parsed.token.empty()) {
    result.command_args.push_back(std::move(parsed.token));
  }

  // 继续提取最多6个参数
  for (int index = 0; index < 6 && !parsed.rest.empty(); ++index) {
    parsed = legacy_get_valid_str3(parsed.rest, " ,:");
    if (!parsed.token.empty()) {
      result.command_args.push_back(std::move(parsed.token));
    }
  }
}

}  // namespace

/**
 * @brief 从文本中提取第一个有效token
 * @details 扫描文本，跳过前导分隔符，收集字符直到遇到下一个分隔符或文本末尾。
 *          如果文本长度超过缓冲区限制（20479字符），返回空结果。
 * @param text 待解析文本
 * @param dividers 分隔符集
 * @return 包含第一个token和剩余部分的解析结果
 */
LegacyTokenParse legacy_get_valid_str3(std::string_view text, std::string_view dividers) {
  LegacyTokenParse result;
  if (text.size() >= kGetValidStr3BufferSize - 1) {
    return result;
  }

  std::string token;
  token.reserve(text.size());
  for (std::size_t index = 0; index < text.size(); ++index) {
    const auto ch = text[index];
    if (is_divider(ch, dividers)) {
      if (!token.empty()) {
        result.token = std::move(token);
        result.rest = std::string{text.substr(index + 1)};
        return result;
      }
      continue; // 跳过前导分隔符
    }
    token.push_back(ch);
  }

  // 没有遇到分隔符，全部内容作为一个token
  result.token = std::move(token);
  return result;
}

/**
 * @brief 解析传统聊天输入
 * @details 根据首字符分类处理：
 *
 *          @ 前缀命令模式：
 *            - @! : GM跨服全局广播
 *            - @$ : GM本服全局广播
 *            - @# : GM地图广播
 *            - @命令 : 普通GM命令
 *
 *          / 前缀私聊模式：
 *            - /玩家名 消息内容
 *
 *          ! 前缀广播/组队/公会模式：
 *            - !!内容 : 组队聊天
 *            - !~内容 : 公会聊天
 *            - !内容  : 喊话
 *
 *          无特殊前缀：普通聊天
 *
 * @param text 原始聊天输入
 * @return 完全解析的聊天结果结构
 */
LegacyChatParseResult parse_legacy_chat_input(std::string_view text) {
  LegacyChatParseResult result;
  result.original_text = std::string{text};
  if (text.empty()) {
    return result;
  }

  const auto first = text.front();
  // 命令模式：以@开头
  if (first == '@') {
    result.kind = LegacyChatInputKind::command;
    result.body_text = std::string{text.substr(1)};
    // 检查GM广播前缀
    if (text.size() > 2) {
      switch (text[1]) {
        case '!':
          result.gm_broadcast = LegacyGmBroadcastKind::sysop_global_interserver;
          result.broadcast_text = std::string{text.substr(2)};
          break;
        case '$':
          result.gm_broadcast = LegacyGmBroadcastKind::sysop_global_local;
          result.broadcast_text = std::string{text.substr(2)};
          break;
        case '#':
          result.gm_broadcast = LegacyGmBroadcastKind::sysop_map;
          result.broadcast_text = std::string{text.substr(2)};
          break;
        default:
          break;
      }
    }
    parse_legacy_command(result.body_text, result);
    return result;
  }

  // 私聊模式：以/开头
  if (first == '/') {
    result.kind = LegacyChatInputKind::whisper;
    result.body_text = std::string{text.substr(1)};
    // 提取目标玩家名（第一个空格前的部分）
    const auto parsed = legacy_get_valid_str3(result.body_text, " ");
    result.target_name = parsed.token;
    result.message_text = parsed.rest;
    return result;
  }

  // 广播/组队/公会模式：以!开头
  if (first == '!') {
    if (text.size() >= 2 && text[1] == '!') {
      // 组队聊天（!!）
      result.kind = LegacyChatInputKind::group;
      result.body_text = std::string{text.substr(2)};
      result.message_text = result.body_text;
      return result;
    }
    if (text.size() >= 2 && text[1] == '~') {
      // 公会聊天（!~）
      result.kind = LegacyChatInputKind::guild;
      result.body_text = std::string{text.substr(2)};
      result.message_text = result.body_text;
      return result;
    }
    // 普通喊话（!）
    result.kind = LegacyChatInputKind::shout;
    result.body_text = std::string{text.substr(1)};
    result.message_text = result.body_text;
    return result;
  }

  // 普通聊天
  result.kind = LegacyChatInputKind::normal;
  result.body_text = std::string{text};
  result.message_text = result.body_text;
  return result;
}

}  // namespace mir2
