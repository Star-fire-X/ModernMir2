#include <cassert>
#include <string>
#include <vector>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

mir2::CharacterRecord make_character() {
  mir2::CharacterRecord character;
  character.account_id = "acct_hero";
  character.character_name = "Hero";
  character.map_id = "0";
  character.x = 10;
  character.y = 10;
  character.ability.hp = 15;
  character.ability.max_hp = 15;
  return character;
}

mir2::LegacyReadyUser make_ready(std::uint64_t session_id) {
  auto character = make_character();
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

mir2::LogicCommand make_walk(std::uint64_t session_id, int x, int y) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::walk;
  command.session_id = session_id;
  command.x = x;
  command.y = y;
  return command;
}

mir2::LogicCommand make_say(std::uint64_t session_id, std::string text) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::say;
  command.session_id = session_id;
  command.text = std::move(text);
  command.game_message = mir2::make_default_message(mir2::kCmSay, 0, 0, 0, 0);
  return command;
}

std::vector<std::string> hear_lines(const mir2::RuntimeDispatch& dispatch) {
  std::vector<std::string> lines;
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (!decoded.has_value() || decoded->message.ident != mir2::kSmHear) {
      continue;
    }
    lines.push_back(mir2::legacy_decode_string(decoded->body));
  }
  return lines;
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "InboxMap", {}, 0, 0, 10, 10});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  static_cast<void>(runtime.enqueue_ready_user(make_ready(31)));
  static_cast<void>(runtime.tick(1000));
  assert(runtime.legacy_session_state(31) == mir2::LegacyPlayerState::initialize_pending);

  static_cast<void>(runtime.route_logic_command(make_walk(31, 11, 10)));
  assert(runtime.legacy_session_inbox_size(31) == 1);
  auto snapshot = runtime.snapshot_character_actor("Hero");
  assert(snapshot.has_value());
  assert(snapshot->x == 10 && snapshot->y == 10);

  static_cast<void>(runtime.tick(1251));
  assert(runtime.legacy_session_state(31) == mir2::LegacyPlayerState::running);
  assert(runtime.legacy_session_inbox_size(31) == 1);
  snapshot = runtime.snapshot_character_actor("Hero");
  assert(snapshot.has_value());
  assert(snapshot->x == 10 && snapshot->y == 10);

  const auto before_move_run_time = runtime.legacy_session_run_time_ms(31);
  static_cast<void>(runtime.route_logic_command(make_walk(31, 11, 10)));
  assert(runtime.legacy_session_run_time_ms(31) == before_move_run_time - 100);
  const auto move_dispatch = runtime.tick(1502);
  assert(runtime.legacy_session_inbox_size(31) == 0);
  snapshot = runtime.snapshot_character_actor("Hero");
  assert(snapshot.has_value());
  assert(snapshot->x == 11 && snapshot->y == 10);
  assert(!move_dispatch.session_events.empty());

  const auto before_say_run_time = runtime.legacy_session_run_time_ms(31);
  static_cast<void>(runtime.route_logic_command(make_say(31, "first")));
  static_cast<void>(runtime.route_logic_command(make_say(31, "second")));
  assert(runtime.legacy_session_run_time_ms(31) == before_say_run_time);
  assert(runtime.legacy_session_inbox_size(31) == 2);
  const auto say_dispatch = runtime.tick(1753);
  const auto lines = hear_lines(say_dispatch);
  assert(lines.size() == 2);
  assert(lines[0] == "Hero: first");
  assert(lines[1] == "Hero: second");

  return 0;
}
