#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

struct DecodedPacket {
  mir2::LegacyDefaultMessage message{};
  std::string text{};
};

int fail(std::string_view stage) {
  std::cerr << "legacy_chat_rules_smoke failed at " << stage << '\n';
  return 1;
}

mir2::CharacterRecord make_character(std::string name, std::string map_id,
                                      std::int32_t x, std::int32_t y,
                                      std::uint8_t level = 20,
                                      std::string guild_name = {}) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = std::move(map_id);
  character.x = x;
  character.y = y;
  character.guild_name = std::move(guild_name);
  character.ability.level = level;
  character.ability.hp = 20;
  character.ability.max_hp = 20;
  character.ability.mp = 20;
  character.ability.max_mp = 20;
  return character;
}

void enter(mir2::LogicRuntime& runtime, std::uint64_t session_id,
           const mir2::CharacterRecord& character) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = session_id;
  command.account_id = character.account_id;
  command.character_name = character.character_name;
  command.map_id = character.map_id;
  command.x = character.x;
  command.y = character.y;
  command.character = character;
  static_cast<void>(runtime.route_logic_command(command));
  static_cast<void>(runtime.tick());
}

mir2::RuntimeDispatch say(mir2::LogicRuntime& runtime, std::uint64_t session_id,
                          std::string text, std::uint64_t now_ms) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::say;
  command.session_id = session_id;
  command.text = std::move(text);
  command.timestamp_ms = now_ms;
  static_cast<void>(runtime.route_logic_command(command));
  return runtime.tick(now_ms);
}

std::uint64_t actor_id(mir2::LogicRuntime& runtime, std::string_view name) {
  const auto located = runtime.locate_character_actor(name);
  return located.has_value() ? located->second : 0;
}

std::vector<DecodedPacket> packets(const mir2::RuntimeDispatch& dispatch,
                                   std::uint64_t session_id,
                                   std::uint16_t ident) {
  std::vector<DecodedPacket> result;
  for (const auto& event : dispatch.session_events) {
    if (event.session_id != session_id) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (!decoded.has_value() || decoded->message.ident != ident) {
      continue;
    }
    result.push_back(DecodedPacket{decoded->message,
                                   mir2::legacy_decode_string(decoded->body)});
  }
  return result;
}

bool has_packet(const mir2::RuntimeDispatch& dispatch, std::uint64_t session_id,
                std::uint16_t ident, std::int32_t recog, std::uint16_t param,
                std::string_view text) {
  for (const auto& packet : packets(dispatch, session_id, ident)) {
    if (packet.message.recog == recog &&
        packet.message.param == param &&
        packet.message.series == 1 &&
        packet.text == text) {
      return true;
    }
  }
  return false;
}

bool has_system(mir2::LogicRuntime& runtime, const mir2::RuntimeDispatch& dispatch,
                std::uint64_t session_id, std::string_view name,
                std::string_view text) {
  return has_packet(dispatch, session_id, mir2::kSmSysMessage,
                    static_cast<std::int32_t>(actor_id(runtime, name)),
                    mir2::make_word(255, 56), text);
}

bool has_text(const mir2::RuntimeDispatch& dispatch, std::uint64_t session_id,
              std::string_view text) {
  for (const auto& event : dispatch.session_events) {
    if (event.session_id != session_id) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && mir2::legacy_decode_string(decoded->body) == text) {
      return true;
    }
  }
  return false;
}

mir2::LogicRuntime make_runtime() {
  mir2::HostConfig config;
  config.budgets.tick_ms = 20;

  mir2::MapConfig main_map;
  main_map.id = "0";
  main_map.title = "MainMap";
  main_map.width = 100;
  main_map.height = 100;
  main_map.home_x = 10;
  main_map.home_y = 10;
  config.maps.push_back(main_map);

  mir2::MapConfig other_map;
  other_map.id = "1";
  other_map.title = "OtherMap";
  other_map.width = 100;
  other_map.height = 100;
  other_map.home_x = 5;
  other_map.home_y = 5;
  config.maps.push_back(other_map);

  mir2::MapConfig quiz_map;
  quiz_map.id = "quiz";
  quiz_map.title = "QuizMap";
  quiz_map.width = 100;
  quiz_map.height = 100;
  quiz_map.home_x = 5;
  quiz_map.home_y = 5;
  quiz_map.quiz_zone = true;
  config.maps.push_back(quiz_map);

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  mir2::GuildCastleSnapshot snapshot;
  snapshot.guilds.push_back(mir2::GuildState{"Red", "Alice", {"Alice", "GuildMate"}});
  runtime.set_guild_castle_snapshot(std::move(snapshot));

  enter(runtime, 1, make_character("Alice", "0", 10, 10, 20, "Red"));
  enter(runtime, 2, make_character("Near", "0", 21, 10));
  enter(runtime, 5, make_character("Bob", "1", 5, 5));
  enter(runtime, 6, make_character("GuildMate", "1", 6, 5, 20, "Red"));
  enter(runtime, 8, make_character("Lowbie", "0", 12, 10, 7));
  enter(runtime, 9, make_character("Quizzer", "quiz", 5, 5, 20));
  static_cast<void>(runtime.tick(1000));
  static_cast<void>(runtime.tick(1251));

  return runtime;
}

bool check_commands_bypass_bomb_say() {
  auto runtime = make_runtime();
  static_cast<void>(say(runtime, 1, "@NoSuchCmd", 1502));
  static_cast<void>(say(runtime, 1, "@NoSuchCmd", 1753));
  static_cast<void>(say(runtime, 1, "@NoSuchCmd", 2004));

  const auto alice = static_cast<std::int32_t>(actor_id(runtime, "Alice"));
  const auto dispatch = say(runtime, 1, "hello", 2255);
  return has_packet(dispatch, 1, mir2::kSmHear, alice, mir2::make_word(0, 255),
                    "Alice: hello") &&
         !has_system(runtime, dispatch, 1, "Alice",
                     "[由于您重复发出相同内容，一分钟内将被禁止交谈。]");
}

bool check_bomb_say_auto_mute() {
  auto runtime = make_runtime();
  const auto alice = static_cast<std::int32_t>(actor_id(runtime, "Alice"));

  const auto first = say(runtime, 1, "spam", 1502);
  const auto second = say(runtime, 1, "spam", 1753);
  const auto third = say(runtime, 1, "spam", 2004);
  const auto muted = say(runtime, 1, "/Bob hi", 2255);
  static_cast<void>(runtime.tick(62000));
  const auto restored = say(runtime, 1, "free", 62251);

  return has_packet(first, 1, mir2::kSmHear, alice, mir2::make_word(0, 255),
                    "Alice: spam") &&
         has_packet(second, 1, mir2::kSmHear, alice, mir2::make_word(0, 255),
                    "Alice: spam") &&
         !has_text(third, 1, "Alice: spam") &&
         has_system(runtime, third, 1, "Alice",
                    "[由于您重复发出相同内容，一分钟内将被禁止交谈。]") &&
         has_system(runtime, third, 1, "Alice", "禁止聊天") &&
         packets(muted, 5, mir2::kSmWhisper).empty() &&
         has_system(runtime, muted, 1, "Alice", "禁止聊天") &&
         has_packet(restored, 1, mir2::kSmHear, alice, mir2::make_word(0, 255),
                    "Alice: free");
}

bool check_channel_bomb_say() {
  {
    auto runtime = make_runtime();
    static_cast<void>(say(runtime, 1, "/Bob hi", 1502));
    static_cast<void>(say(runtime, 1, "/Bob hi", 1753));
    const auto third = say(runtime, 1, "/Bob hi", 2004);
    if (!packets(third, 5, mir2::kSmWhisper).empty() ||
        !has_system(runtime, third, 1, "Alice",
                    "[由于您重复发出相同内容，一分钟内将被禁止交谈。]")) {
      return false;
    }
  }
  {
    auto runtime = make_runtime();
    static_cast<void>(say(runtime, 1, "!~hi", 1502));
    static_cast<void>(say(runtime, 1, "!~hi", 1753));
    const auto third = say(runtime, 1, "!~hi", 2004);
    if (!packets(third, 6, mir2::kSmGuildMessage).empty() ||
        !has_system(runtime, third, 1, "Alice",
                    "[由于您重复发出相同内容，一分钟内将被禁止交谈。]")) {
      return false;
    }
  }
  {
    auto runtime = make_runtime();
    static_cast<void>(say(runtime, 1, "!!hi", 1502));
    static_cast<void>(say(runtime, 1, "!!hi", 1753));
    const auto third = say(runtime, 1, "!!hi", 2004);
    if (!has_system(runtime, third, 1, "Alice",
                    "[由于您重复发出相同内容，一分钟内将被禁止交谈。]")) {
      return false;
    }
  }
  {
    auto runtime = make_runtime();
    static_cast<void>(runtime.tick(11255));
    static_cast<void>(say(runtime, 1, "!boom", 11506));
    static_cast<void>(say(runtime, 1, "!boom", 11757));
    const auto third = say(runtime, 1, "!boom", 12008);
    if (has_text(third, 1, "(!)Alice:boom") ||
        !has_system(runtime, third, 1, "Alice",
                    "[由于您重复发出相同内容，一分钟内将被禁止交谈。]")) {
      return false;
    }
  }
  return true;
}

bool check_global_shut_up_list() {
  auto runtime = make_runtime();
  const auto alice = static_cast<std::int32_t>(actor_id(runtime, "Alice"));

  runtime.add_legacy_shut_up("Alice", 1000, 1251);
  runtime.add_legacy_shut_up("ALICE", 1000, 1251);
  if (runtime.legacy_shut_up_entries().size() != 1 ||
      runtime.legacy_shut_up_entries().front().expire_ms != 3251) {
    return false;
  }
  runtime.add_legacy_shut_up("Alice", 1000, 4000);
  if (runtime.legacy_shut_up_entries().front().expire_ms != 5000) {
    return false;
  }

  const auto muted = say(runtime, 1, "/Bob muted", 4251);
  static_cast<void>(runtime.tick(5251));
  const auto restored = say(runtime, 1, "/Bob free", 5502);

  return packets(muted, 5, mir2::kSmWhisper).empty() &&
         has_system(runtime, muted, 1, "Alice", "禁止聊天") &&
         has_packet(restored, 5, mir2::kSmWhisper, alice, mir2::make_word(252, 255),
                    "Alice=> free");
}

bool check_shout_rules() {
  auto runtime = make_runtime();

  const auto first = say(runtime, 1, "!hi", 1502);
  const auto cooldown = say(runtime, 1, "!again", 1753);
  static_cast<void>(runtime.tick(11400));
  const auto after_cooldown = say(runtime, 1, "!after", 11503);
  const auto lowbie = say(runtime, 8, "!low", 11754);
  const auto quiz = say(runtime, 9, "!quiz", 12005);

  return has_packet(first, 1, mir2::kSmHear, 0, mir2::make_word(0, 151),
                    "(!)Alice:hi") &&
         !has_text(cooldown, 1, "(!)Alice:again") &&
         has_system(runtime, cooldown, 1, "Alice",
                    "10秒以后才能再次使用喊话。") &&
         has_packet(after_cooldown, 1, mir2::kSmHear, 0, mir2::make_word(0, 151),
                    "(!)Alice:after") &&
         has_system(runtime, lowbie, 8, "Lowbie",
                    "喊话功能只有7级以上才可以使用") &&
         !has_text(lowbie, 8, "(!)Lowbie:low") &&
         has_system(runtime, quiz, 9, "Quizzer", "无法使用") &&
         !has_text(quiz, 9, "(!)Quizzer:quiz");
}

}  // namespace

int main() {
  if (!check_commands_bypass_bomb_say()) {
    return fail("commands bypass bomb say");
  }
  if (!check_bomb_say_auto_mute()) {
    return fail("bomb say auto mute");
  }
  if (!check_channel_bomb_say()) {
    return fail("channel bomb say");
  }
  if (!check_global_shut_up_list()) {
    return fail("global shut up list");
  }
  if (!check_shout_rules()) {
    return fail("shout rules");
  }
  return 0;
}
