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
  std::cerr << "legacy_chat_router_smoke failed at " << stage << '\n';
  return 1;
}

mir2::CharacterRecord make_character(std::string name, std::string map_id,
                                      std::int32_t x, std::int32_t y,
                                      std::string guild_name = {}) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = std::move(map_id);
  character.x = x;
  character.y = y;
  character.guild_name = std::move(guild_name);
  character.ability.level = 20;
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

mir2::LogicRuntime make_runtime() {
  mir2::HostConfig config;
  config.budgets.tick_ms = 20;
  config.maps.push_back(mir2::MapConfig{"0", "MainMap", {}, 100, 100, 10, 10});
  config.maps.push_back(mir2::MapConfig{"1", "OtherMap", {}, 100, 100, 5, 5});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  mir2::GuildCastleSnapshot snapshot;
  snapshot.guilds.push_back(mir2::GuildState{"Red", "Alice", {"Alice", "GuildMate"}});
  runtime.set_guild_castle_snapshot(std::move(snapshot));

  enter(runtime, 1, make_character("Alice", "0", 10, 10, "Red"));
  enter(runtime, 2, make_character("Near", "0", 21, 10));
  enter(runtime, 3, make_character("Far", "0", 23, 10));
  enter(runtime, 4, make_character("VeryFar", "0", 70, 10));
  enter(runtime, 5, make_character("Bob", "1", 5, 5));
  enter(runtime, 6, make_character("GuildMate", "1", 6, 5, "Red"));
  enter(runtime, 7, make_character("Outsider", "0", 12, 10));
  static_cast<void>(runtime.tick(1000));
  static_cast<void>(runtime.tick(1251));

  return runtime;
}

bool check_normal_chat(mir2::LogicRuntime& runtime) {
  const auto alice = static_cast<std::int32_t>(actor_id(runtime, "Alice"));
  const auto dispatch = say(runtime, 1, "hello", 1502);
  return has_packet(dispatch, 1, mir2::kSmHear, alice, mir2::make_word(0, 255),
                    "Alice: hello") &&
         has_packet(dispatch, 2, mir2::kSmHear, alice, mir2::make_word(0, 255),
                    "Alice: hello") &&
         !has_text(dispatch, 3, "Alice: hello");
}

bool check_whisper(mir2::LogicRuntime& runtime) {
  const auto alice = static_cast<std::int32_t>(actor_id(runtime, "Alice"));
  const auto dispatch = say(runtime, 1, "/bob hi", 1753);
  return has_packet(dispatch, 5, mir2::kSmWhisper, alice, mir2::make_word(252, 255),
                    "Alice=> hi") &&
         packets(dispatch, 1, mir2::kSmWhisper).empty();
}

bool check_guild_chat(mir2::LogicRuntime& runtime) {
  const auto alice = static_cast<std::int32_t>(actor_id(runtime, "Alice"));
  const auto guild_mate = static_cast<std::int32_t>(actor_id(runtime, "GuildMate"));
  const auto dispatch = say(runtime, 1, "!~hi", 2004);
  return has_packet(dispatch, 1, mir2::kSmGuildMessage, alice, mir2::make_word(212, 255),
                    "Alice:hi") &&
         has_packet(dispatch, 6, mir2::kSmGuildMessage, guild_mate,
                    mir2::make_word(212, 255), "Alice:hi") &&
         packets(dispatch, 7, mir2::kSmGuildMessage).empty();
}

bool check_shout(mir2::LogicRuntime& runtime) {
  static_cast<void>(runtime.tick(11255));
  const auto dispatch = say(runtime, 1, "!hi", 11506);
  return has_packet(dispatch, 1, mir2::kSmHear, 0, mir2::make_word(0, 151),
                    "(!)Alice:hi") &&
         has_packet(dispatch, 2, mir2::kSmHear, 0, mir2::make_word(0, 151),
                    "(!)Alice:hi") &&
         has_packet(dispatch, 3, mir2::kSmHear, 0, mir2::make_word(0, 151),
                    "(!)Alice:hi") &&
         !has_text(dispatch, 4, "(!)Alice:hi") &&
         !has_text(dispatch, 5, "(!)Alice:hi");
}

bool check_group_is_silent(mir2::LogicRuntime& runtime) {
  const auto dispatch = say(runtime, 1, "!!hi", 11757);
  return !has_text(dispatch, 1, "Alice: !!hi") &&
         packets(dispatch, 1, mir2::kSmSysMessage).empty();
}

bool check_unknown_command_is_silent(mir2::LogicRuntime& runtime) {
  const auto dispatch = say(runtime, 1, "@NoSuchCmd", 12008);
  return !has_text(dispatch, 1, "Alice: @NoSuchCmd") &&
         packets(dispatch, 1, mir2::kSmHear).empty();
}

bool check_system_packet_builder(mir2::LogicRuntime& runtime) {
  const auto alice = actor_id(runtime, "Alice");
  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::legacy_chat_delivery;
  mail.map_id = "0";
  mail.actor_id = alice;
  mail.target_actor_id = alice;
  mail.legacy_chat_kind = mir2::LegacyChatDeliveryKind::system;
  mail.payload = "-Alice: hi";
  static_cast<void>(runtime.route_actor_mail(mail));
  const auto dispatch = runtime.tick();
  return has_packet(dispatch, 1, mir2::kSmSysMessage, static_cast<std::int32_t>(alice),
                    mir2::make_word(255, 56), "-Alice: hi");
}

}  // namespace

int main() {
  auto runtime = make_runtime();

  if (!check_normal_chat(runtime)) {
    return fail("normal chat");
  }
  if (!check_whisper(runtime)) {
    return fail("whisper");
  }
  if (!check_guild_chat(runtime)) {
    return fail("guild chat");
  }
  if (!check_shout(runtime)) {
    return fail("shout");
  }
  if (!check_group_is_silent(runtime)) {
    return fail("group silent");
  }
  if (!check_unknown_command_is_silent(runtime)) {
    return fail("unknown command");
  }
  if (!check_system_packet_builder(runtime)) {
    return fail("system packet builder");
  }

  return 0;
}
