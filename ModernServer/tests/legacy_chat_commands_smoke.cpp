#include <iostream>
#include <filesystem>
#include <fstream>
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
  std::cerr << "legacy_chat_commands_smoke failed at " << stage << '\n';
  return 1;
}

mir2::CharacterRecord make_character(std::string name, std::string account_id,
                                      std::string map_id, std::int32_t x,
                                      std::int32_t y, std::uint8_t level = 20,
                                      std::string guild_name = {}) {
  mir2::CharacterRecord character;
  character.account_id = std::move(account_id);
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

bool has_system(mir2::LogicRuntime& runtime, const mir2::RuntimeDispatch& dispatch,
                std::uint64_t session_id, std::string_view name,
                std::string_view text) {
  return has_packet(dispatch, session_id, mir2::kSmSysMessage,
                    static_cast<std::int32_t>(actor_id(runtime, name)),
                    mir2::make_word(255, 56), text);
}

std::string gbk_command(std::string_view command_name) {
  return "@" + std::string(command_name);
}

mir2::LogicRuntime make_runtime() {
  mir2::HostConfig config;
  config.budgets.tick_ms = 20;
  const auto admin_list =
      std::filesystem::temp_directory_path() / "mir2_legacy_chat_commands_admin.txt";
  {
    std::ofstream output(admin_list);
    output << "1Sysop\n";
  }
  config.runtime.legacy_admin_list = admin_list;

  mir2::MapConfig main_map;
  main_map.id = "0";
  main_map.title = "MainMap";
  main_map.width = 120;
  main_map.height = 120;
  main_map.home_x = 10;
  main_map.home_y = 10;
  config.maps.push_back(main_map);

  mir2::MapConfig other_map;
  other_map.id = "1";
  other_map.title = "OtherMap";
  other_map.width = 120;
  other_map.height = 120;
  other_map.home_x = 5;
  other_map.home_y = 5;
  config.maps.push_back(other_map);

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  mir2::GuildCastleSnapshot snapshot;
  snapshot.guilds.push_back(mir2::GuildState{"Red", "Alice", {"Alice", "Bob"}});
  runtime.set_guild_castle_snapshot(std::move(snapshot));

  enter(runtime, 1, make_character("Alice", "acct_alice", "0", 10, 10, 20, "Red"));
  enter(runtime, 2, make_character("Bob", "acct_bob", "0", 12, 10, 20, "Red"));
  enter(runtime, 3, make_character("Carol", "acct_carol", "0", 20, 10));
  enter(runtime, 4, make_character("Dave", "acct_dave", "1", 5, 5));
  enter(runtime, 9, make_character("Sysop", "gm_admin", "0", 15, 10));
  enter(runtime, 10, make_character("Visitor", "acct_visitor", "0", 18, 10));
  static_cast<void>(runtime.tick(1000));
  return runtime;
}

bool check_utf8_gbk_commands_and_unknown() {
  auto runtime = make_runtime();
  const auto unknown = say(runtime, 1, "@NoSuchCmd", 1500);
  const auto utf8 = say(runtime, 2, "@拒绝私聊", 1600);
  const auto gbk = say(runtime, 2,
                       gbk_command(std::string_view(
                           "\xD4\xCA\xD0\xED\xCB\xBD\xC1\xC4", 8)),
                       1700);

  return unknown.session_events.empty() &&
         has_system(runtime, utf8, 2, "Bob", "[拒绝接收私聊信息]") &&
         has_system(runtime, gbk, 2, "Bob", "[允许私聊]");
}

bool check_whisper_receive_filter() {
  auto runtime = make_runtime();
  static_cast<void>(say(runtime, 2, "@拒绝私聊", 1500));
  const auto rejected = say(runtime, 1, "/Bob hi", 1600);
  static_cast<void>(say(runtime, 2, "@允许私聊", 1700));
  const auto restored = say(runtime, 1, "/Bob open", 1800);
  const auto missing = say(runtime, 1, "/Missing hi", 1900);

  return has_system(runtime, rejected, 1, "Alice", "Bob拒绝密语") &&
         packets(rejected, 2, mir2::kSmWhisper).empty() &&
         has_packet(restored, 2, mir2::kSmWhisper,
                    static_cast<std::int32_t>(actor_id(runtime, "Alice")),
                    mir2::make_word(252, 255), "Alice=> open") &&
         has_system(runtime, missing, 1, "Alice", "Missing无法查找");
}

bool check_whisper_block_list() {
  auto runtime = make_runtime();
  const auto blocked = say(runtime, 2, "@拒绝 Alice Carol Dave Nobody", 1500);
  const auto rejected = say(runtime, 1, "/Bob hi", 1600);
  const auto unblocked = say(runtime, 2, "@拒绝 Alice", 1700);
  const auto restored = say(runtime, 1, "/Bob open", 1800);

  return has_system(runtime, blocked, 2, "Bob", "[禁止与:Alice 私聊]") &&
         has_system(runtime, blocked, 2, "Bob", "[禁止与:Carol 私聊]") &&
         has_system(runtime, blocked, 2, "Bob", "[禁止与:Dave 私聊]") &&
         !has_text(blocked, 2, "[禁止与:Nobody 私聊]") &&
         has_system(runtime, rejected, 1, "Alice", "Bob拒绝密语") &&
         has_system(runtime, unblocked, 2, "Bob", "[允许与:Alice 私聊]") &&
         has_packet(restored, 2, mir2::kSmWhisper,
                    static_cast<std::int32_t>(actor_id(runtime, "Alice")),
                    mir2::make_word(252, 255), "Alice=> open");
}

bool check_guild_receive_filter() {
  auto runtime = make_runtime();
  const auto disabled = say(runtime, 2, "@拒绝行会喊话", 1500);
  const auto filtered = say(runtime, 1, "!~one", 1600);
  const auto enabled = say(runtime, 2, "@允许行会喊话", 1700);
  const auto restored = say(runtime, 1, "!~two", 1800);

  return has_system(runtime, disabled, 2, "Bob", "拒绝接收行会喊话信息") &&
         has_packet(filtered, 1, mir2::kSmGuildMessage,
                    static_cast<std::int32_t>(actor_id(runtime, "Alice")),
                    mir2::make_word(212, 255), "Alice:one") &&
         packets(filtered, 2, mir2::kSmGuildMessage).empty() &&
         has_system(runtime, enabled, 2, "Bob", "允许接收行会喊话信息") &&
         has_packet(restored, 2, mir2::kSmGuildMessage,
                    static_cast<std::int32_t>(actor_id(runtime, "Bob")),
                    mir2::make_word(212, 255), "Alice:two");
}

bool check_shout_receive_filter() {
  auto runtime = make_runtime();
  const auto disabled = say(runtime, 2, "@拒绝喊话", 1500);
  const auto shouted = say(runtime, 1, "!hello", 1600);

  return has_system(runtime, disabled, 2, "Bob", "[拒绝接收(黄颜色字体)喊话]") &&
         has_packet(shouted, 1, mir2::kSmHear, 0, mir2::make_word(0, 151),
                    "(!)Alice:hello") &&
         packets(shouted, 2, mir2::kSmHear).empty() &&
         has_packet(shouted, 3, mir2::kSmHear, 0, mir2::make_word(0, 151),
                    "(!)Alice:hello") &&
         packets(shouted, 4, mir2::kSmHear).empty();
}

bool check_gm_broadcasts() {
  auto runtime = make_runtime();
  const auto global = say(runtime, 9, "@!notice", 1500);
  const auto local = say(runtime, 9, "@$local", 1600);
  const auto map = say(runtime, 9, "@#map", 1700);
  const auto visitor = say(runtime, 10, "@!bad", 1800);

  return has_system(runtime, global, 1, "Alice", "(公告)notice") &&
         has_system(runtime, global, 4, "Dave", "(公告)notice") &&
         has_system(runtime, global, 9, "Sysop", "(公告)notice") &&
         has_system(runtime, local, 2, "Bob", "(!)local") &&
         has_system(runtime, local, 4, "Dave", "(!)local") &&
         has_system(runtime, map, 1, "Alice", "(#)map") &&
         has_system(runtime, map, 10, "Visitor", "(#)map") &&
         !has_text(map, 4, "(#)map") &&
         visitor.session_events.empty();
}

bool check_gm_shut_up_commands() {
  auto runtime = make_runtime();
  const auto ignored = say(runtime, 1, "@Shutup Bob", 1400);
  const auto before = say(runtime, 2, "/Alice before", 1450);
  const auto shut = say(runtime, 9, "@Shutup Bob", 1500);
  const auto list = say(runtime, 9, "@ShutupList", 1500);
  const auto muted = say(runtime, 2, "blocked", 1600);
  const auto missing = say(runtime, 9, "@ReleaseShutup Ghost", 1700);
  const auto released = say(runtime, 9, "@ReleaseShutup Bob", 1800);
  const auto restored = say(runtime, 2, "/Alice after", 1900);

  return ignored.session_events.empty() &&
         has_packet(before, 1, mir2::kSmWhisper,
                    static_cast<std::int32_t>(actor_id(runtime, "Bob")),
                    mir2::make_word(252, 255), "Bob=> before") &&
         has_system(runtime, shut, 9, "Sysop", "Bob禁止聊天 + 5分钟") &&
         has_system(runtime, list, 9, "Sysop", "Bob 5分钟") &&
         has_system(runtime, muted, 2, "Bob", "禁止聊天") &&
         !has_text(muted, 2, "Bob: blocked") &&
         has_system(runtime, missing, 9, "Sysop", "Ghost 无法查找") &&
         has_system(runtime, released, 2, "Bob", "从禁止聊天列表删除") &&
         has_system(runtime, released, 9, "Sysop", "Bob ") &&
         has_packet(restored, 1, mir2::kSmWhisper,
                    static_cast<std::int32_t>(actor_id(runtime, "Bob")),
                    mir2::make_word(252, 255), "Bob=> after");
}

}  // namespace

int main() {
  if (!check_utf8_gbk_commands_and_unknown()) {
    return fail("utf8 gbk commands and unknown");
  }
  if (!check_whisper_receive_filter()) {
    return fail("whisper receive filter");
  }
  if (!check_whisper_block_list()) {
    return fail("whisper block list");
  }
  if (!check_guild_receive_filter()) {
    return fail("guild receive filter");
  }
  if (!check_shout_receive_filter()) {
    return fail("shout receive filter");
  }
  if (!check_gm_broadcasts()) {
    return fail("gm broadcasts");
  }
  if (!check_gm_shut_up_commands()) {
    return fail("gm shut up commands");
  }
  return 0;
}
