#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "protocol/legacy_types.hpp"
#include "world/legacy_chat_parser.hpp"

namespace {

int fail(std::string_view stage) {
  std::cerr << "legacy_chat_parser_smoke failed at " << stage << '\n';
  return 1;
}

std::string bytes(std::initializer_list<unsigned char> values) {
  std::string output;
  output.reserve(values.size());
  for (const auto value : values) {
    output.push_back(static_cast<char>(value));
  }
  return output;
}

bool check_constants() {
  return mir2::kSmSysMessage == 100 &&
         mir2::kSmGroupMessage == 101 &&
         mir2::kSmCry == 102 &&
         mir2::kSmWhisper == 103 &&
         mir2::kSmGuildMessage == 104;
}

bool check_text(std::string_view input, mir2::LegacyChatInputKind kind,
                std::string_view body, std::string_view message) {
  const auto parsed = mir2::parse_legacy_chat_input(input);
  return parsed.kind == kind &&
         parsed.gm_broadcast == mir2::LegacyGmBroadcastKind::none &&
         parsed.original_text == input &&
         parsed.body_text == body &&
         parsed.message_text == message;
}

bool check_empty_and_normal() {
  const auto empty = mir2::parse_legacy_chat_input("");
  return empty.kind == mir2::LegacyChatInputKind::empty &&
         empty.original_text.empty() &&
         check_text("   ", mir2::LegacyChatInputKind::normal, "   ", "   ") &&
         check_text("hello", mir2::LegacyChatInputKind::normal, "hello", "hello") &&
         check_text("hello @Move", mir2::LegacyChatInputKind::normal,
                    "hello @Move", "hello @Move") &&
         check_text("hello /target", mir2::LegacyChatInputKind::normal,
                    "hello /target", "hello /target");
}

bool check_prefix_priority() {
  return check_text("!!team", mir2::LegacyChatInputKind::group, "team", "team") &&
         check_text("!~guild", mir2::LegacyChatInputKind::guild, "guild", "guild") &&
         check_text("!shout", mir2::LegacyChatInputKind::shout, "shout", "shout") &&
         mir2::parse_legacy_chat_input("@!notice").kind ==
             mir2::LegacyChatInputKind::command &&
         mir2::parse_legacy_chat_input("@!notice").gm_broadcast ==
             mir2::LegacyGmBroadcastKind::sysop_global_interserver;
}

bool check_command(std::string_view input, std::string_view command,
                   const std::vector<std::string>& args) {
  const auto parsed = mir2::parse_legacy_chat_input(input);
  return parsed.kind == mir2::LegacyChatInputKind::command &&
         parsed.command_name == command &&
         parsed.command_args == args;
}

bool check_commands() {
  return check_command("@Move Bichon", "Move", {"Bichon"}) &&
         check_command("@Mob Hen,5", "Mob", {"Hen", "5"}) &&
         check_command("@flag name:5", "flag", {"name", "5"}) &&
         check_command("@Move    Bichon", "Move", {"Bichon"}) &&
         check_command("@NoSuchCmd", "NoSuchCmd", {});
}

bool check_gm_broadcasts() {
  const auto interserver = mir2::parse_legacy_chat_input("@!notice");
  const auto local = mir2::parse_legacy_chat_input("@$notice");
  const auto map = mir2::parse_legacy_chat_input("@#notice");
  const auto short_interserver = mir2::parse_legacy_chat_input("@!");
  return interserver.kind == mir2::LegacyChatInputKind::command &&
         interserver.gm_broadcast == mir2::LegacyGmBroadcastKind::sysop_global_interserver &&
         interserver.command_name == "!notice" &&
         interserver.broadcast_text == "notice" &&
         local.gm_broadcast == mir2::LegacyGmBroadcastKind::sysop_global_local &&
         local.command_name == "$notice" &&
         local.broadcast_text == "notice" &&
         map.gm_broadcast == mir2::LegacyGmBroadcastKind::sysop_map &&
         map.command_name == "#notice" &&
         map.broadcast_text == "notice" &&
         short_interserver.gm_broadcast == mir2::LegacyGmBroadcastKind::none &&
         short_interserver.command_name == "!" &&
         short_interserver.broadcast_text.empty();
}

bool check_whisper(std::string_view input, std::string_view body,
                   std::string_view target, std::string_view message) {
  const auto parsed = mir2::parse_legacy_chat_input(input);
  return parsed.kind == mir2::LegacyChatInputKind::whisper &&
         parsed.body_text == body &&
         parsed.target_name == target &&
         parsed.message_text == message;
}

bool check_whispers() {
  return check_whisper("/target hi", "target hi", "target", "hi") &&
         check_whisper("/target   hi", "target   hi", "target", "  hi") &&
         check_whisper("/", "", "", "") &&
         check_whisper("/who", "who", "who", "") &&
         check_whisper("/who ", "who ", "who", "");
}

bool check_empty_bodies() {
  return check_text("!!", mir2::LegacyChatInputKind::group, "", "") &&
         check_text("!~", mir2::LegacyChatInputKind::guild, "", "") &&
         check_text("!", mir2::LegacyChatInputKind::shout, "", "");
}

bool check_byte_preservation() {
  const auto gbk = bytes({0xB0, 0xA1});
  const auto input = std::string{"@"} + gbk + " arg";
  const auto parsed = mir2::parse_legacy_chat_input(input);
  return parsed.kind == mir2::LegacyChatInputKind::command &&
         parsed.original_text == input &&
         parsed.body_text == gbk + " arg" &&
         parsed.command_name == gbk &&
         parsed.command_args == std::vector<std::string>{"arg"};
}

bool check_token(std::string_view input, std::string_view dividers,
                 std::string_view token, std::string_view rest) {
  const auto parsed = mir2::legacy_get_valid_str3(input, dividers);
  return parsed.token == token && parsed.rest == rest;
}

bool check_get_valid_str3() {
  const auto long_text = std::string(20479, 'a');
  const auto overflow = mir2::legacy_get_valid_str3(long_text, " ");
  return check_token("alpha,,beta", " ,:", "alpha", ",beta") &&
         check_token(",,alpha,,beta", " ,:", "alpha", ",beta") &&
         check_token("alpha", " ,:", "alpha", "") &&
         check_token("", " ,:", "", "") &&
         check_token("   ", " ", "", "") &&
         overflow.token.empty() &&
         overflow.rest.empty();
}

}  // namespace

int main() {
  if (!check_constants()) {
    return fail("constants");
  }
  if (!check_empty_and_normal()) {
    return fail("empty and normal");
  }
  if (!check_prefix_priority()) {
    return fail("prefix priority");
  }
  if (!check_commands()) {
    return fail("commands");
  }
  if (!check_gm_broadcasts()) {
    return fail("gm broadcasts");
  }
  if (!check_whispers()) {
    return fail("whispers");
  }
  if (!check_empty_bodies()) {
    return fail("empty bodies");
  }
  if (!check_byte_preservation()) {
    return fail("byte preservation");
  }
  if (!check_get_valid_str3()) {
    return fail("get valid str3");
  }
  return 0;
}
