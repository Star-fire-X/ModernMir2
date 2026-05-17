#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace mir2 {

enum class LegacyChatInputKind {
  empty,
  normal,
  whisper,
  group,
  guild,
  shout,
  command
};

enum class LegacyGmBroadcastKind {
  none,
  sysop_global_interserver,
  sysop_global_local,
  sysop_map
};

struct LegacyTokenParse {
  std::string token{};
  std::string rest{};
};

struct LegacyChatParseResult {
  LegacyChatInputKind kind{LegacyChatInputKind::empty};
  LegacyGmBroadcastKind gm_broadcast{LegacyGmBroadcastKind::none};
  std::string original_text{};
  std::string body_text{};
  std::string message_text{};
  std::string target_name{};
  std::string command_name{};
  std::vector<std::string> command_args{};
  std::string broadcast_text{};
};

[[nodiscard]] LegacyTokenParse legacy_get_valid_str3(std::string_view text,
                                                     std::string_view dividers);

[[nodiscard]] LegacyChatParseResult parse_legacy_chat_input(std::string_view text);

}  // namespace mir2
