#include "world/legacy_chat_parser.hpp"

#include <utility>

namespace mir2 {

namespace {

constexpr std::size_t kGetValidStr3BufferSize = 20480;

bool is_divider(char ch, std::string_view dividers) {
  return dividers.find(ch) != std::string_view::npos;
}

void parse_legacy_command(std::string_view body, LegacyChatParseResult& result) {
  auto parsed = legacy_get_valid_str3(body, " ,:");
  result.command_name = std::move(parsed.token);

  parsed = legacy_get_valid_str3(parsed.rest, " ,:");
  if (!parsed.token.empty()) {
    result.command_args.push_back(std::move(parsed.token));
  }

  for (int index = 0; index < 6 && !parsed.rest.empty(); ++index) {
    parsed = legacy_get_valid_str3(parsed.rest, " ,:");
    if (!parsed.token.empty()) {
      result.command_args.push_back(std::move(parsed.token));
    }
  }
}

}  // namespace

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
      continue;
    }
    token.push_back(ch);
  }

  result.token = std::move(token);
  return result;
}

LegacyChatParseResult parse_legacy_chat_input(std::string_view text) {
  LegacyChatParseResult result;
  result.original_text = std::string{text};
  if (text.empty()) {
    return result;
  }

  const auto first = text.front();
  if (first == '@') {
    result.kind = LegacyChatInputKind::command;
    result.body_text = std::string{text.substr(1)};
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

  if (first == '/') {
    result.kind = LegacyChatInputKind::whisper;
    result.body_text = std::string{text.substr(1)};
    const auto parsed = legacy_get_valid_str3(result.body_text, " ");
    result.target_name = parsed.token;
    result.message_text = parsed.rest;
    return result;
  }

  if (first == '!') {
    if (text.size() >= 2 && text[1] == '!') {
      result.kind = LegacyChatInputKind::group;
      result.body_text = std::string{text.substr(2)};
      result.message_text = result.body_text;
      return result;
    }
    if (text.size() >= 2 && text[1] == '~') {
      result.kind = LegacyChatInputKind::guild;
      result.body_text = std::string{text.substr(2)};
      result.message_text = result.body_text;
      return result;
    }
    result.kind = LegacyChatInputKind::shout;
    result.body_text = std::string{text.substr(1)};
    result.message_text = result.body_text;
    return result;
  }

  result.kind = LegacyChatInputKind::normal;
  result.body_text = std::string{text};
  result.message_text = result.body_text;
  return result;
}

}  // namespace mir2
