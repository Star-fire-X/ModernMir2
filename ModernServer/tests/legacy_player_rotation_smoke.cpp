#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

mir2::CharacterRecord make_character(std::string name, std::int32_t x) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = x;
  character.y = 10;
  character.ability.hp = 15;
  character.ability.max_hp = 15;
  return character;
}

mir2::LegacyReadyUser make_ready(std::uint64_t session_id, std::string name,
                                 std::int32_t x) {
  auto character = make_character(std::move(name), x);
  mir2::LegacyReadyUser ready;
  ready.session_id = session_id;
  ready.account_id = character.account_id;
  ready.character_name = character.character_name;
  ready.map_id = character.map_id;
  ready.x = character.x;
  ready.y = character.y;
  ready.character = std::move(character);
  return ready;
}

mir2::LogicCommand make_say(std::uint64_t session_id, std::string text) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::say;
  command.session_id = session_id;
  command.session_seq = 1;
  command.text = std::move(text);
  command.game_message = mir2::make_default_message(mir2::kCmSay, 0, 0, 0, 0);
  return command;
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "RotationMap", {}, 0, 0, 30, 30});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  const std::vector<std::uint64_t> sessions{101, 102, 103, 104, 105};
  for (std::size_t index = 0; index < sessions.size(); ++index) {
    static_cast<void>(runtime.enqueue_ready_user(make_ready(
        sessions[index], "Hero" + std::to_string(index + 1),
        static_cast<std::int32_t>(10 + index))));
  }

  static_cast<void>(runtime.tick(1000));
  static_cast<void>(runtime.tick(1251));
  for (const auto session_id : sessions) {
    assert(runtime.legacy_session_state(session_id) == mir2::LegacyPlayerState::running);
    static_cast<void>(runtime.route_logic_command(make_say(session_id, "queued")));
    assert(runtime.legacy_session_inbox_size(session_id) == 1);
    assert(runtime.legacy_session_inbox_sequences(session_id) == std::vector<std::uint64_t>{1});
  }

  mir2::LegacyRuntimeContext context;
  context.player_process_limit = 2;

  static_cast<void>(runtime.tick(1502, context));
  assert(runtime.legacy_session_inbox_size(101) == 0);
  assert(runtime.legacy_session_inbox_size(102) == 0);
  assert(runtime.legacy_session_inbox_size(103) == 1);
  assert(runtime.legacy_session_inbox_size(104) == 1);
  assert(runtime.legacy_session_inbox_size(105) == 1);

  static_cast<void>(runtime.tick(1753, context));
  assert(runtime.legacy_session_inbox_size(101) == 0);
  assert(runtime.legacy_session_inbox_size(102) == 0);
  assert(runtime.legacy_session_inbox_size(103) == 0);
  assert(runtime.legacy_session_inbox_size(104) == 0);
  assert(runtime.legacy_session_inbox_size(105) == 1);

  static_cast<void>(runtime.tick(2004, context));
  for (const auto session_id : sessions) {
    assert(runtime.legacy_session_inbox_size(session_id) == 0);
  }

  return 0;
}
