#include <cassert>
#include <string>
#include <vector>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

mir2::CharacterRecord make_character(std::string name = "BudgetHero") {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = 10;
  character.y = 10;
  character.ability.hp = 15;
  character.ability.max_hp = 15;
  return character;
}

mir2::LegacyReadyUser make_ready(std::uint64_t session_id, std::string name = "BudgetHero") {
  auto character = make_character(std::move(name));
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

mir2::LogicCommand make_say(std::uint64_t session_id, std::string text,
                            std::uint64_t session_seq) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::say;
  command.session_id = session_id;
  command.session_seq = session_seq;
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

bool has_budget_exhausted_trace(const mir2::RuntimeDispatch& dispatch, std::int32_t remaining) {
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == "PlayerOperate" && trace.action == "messages_budget_exhausted" &&
        trace.value == remaining) {
      return true;
    }
  }
  return false;
}

void prepare_running_player(mir2::LogicRuntime& runtime, std::uint64_t session_id,
                            std::string name) {
  static_cast<void>(runtime.enqueue_ready_user(make_ready(session_id, std::move(name))));
  static_cast<void>(runtime.tick(1000));
  static_cast<void>(runtime.tick(1251));
  assert(runtime.legacy_session_state(session_id) == mir2::LegacyPlayerState::running);
}

void assert_default_budget_unlimited() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "BudgetMap", {}, 0, 0, 20, 20});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  prepare_running_player(runtime, 51, "BudgetHero");

  static_cast<void>(runtime.route_logic_command(make_say(51, "one", 1)));
  static_cast<void>(runtime.route_logic_command(make_say(51, "two", 2)));
  static_cast<void>(runtime.route_logic_command(make_say(51, "three", 3)));
  assert((runtime.legacy_session_inbox_sequences(51) == std::vector<std::uint64_t>{1, 2, 3}));

  const auto dispatch = runtime.tick(1502);
  assert((hear_lines(dispatch) == std::vector<std::string>{
                                      "BudgetHero: one",
                                      "BudgetHero: two",
                                      "BudgetHero: three"}));
  assert(runtime.legacy_session_inbox_size(51) == 0);
  assert(!has_budget_exhausted_trace(dispatch, 0));
}

void assert_context_budget_two() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "BudgetMap", {}, 0, 0, 20, 20});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  prepare_running_player(runtime, 52, "TwoBudgetHero");

  static_cast<void>(runtime.route_logic_command(make_say(52, "one", 1)));
  static_cast<void>(runtime.route_logic_command(make_say(52, "two", 2)));
  static_cast<void>(runtime.route_logic_command(make_say(52, "three", 3)));

  mir2::LegacyRuntimeContext context;
  context.player_input_budget_per_tick = 2;
  const auto dispatch = runtime.tick(1502, context);
  assert((hear_lines(dispatch) ==
          std::vector<std::string>{"TwoBudgetHero: one", "TwoBudgetHero: two"}));
  assert((runtime.legacy_session_inbox_sequences(52) == std::vector<std::uint64_t>{3}));
  assert(has_budget_exhausted_trace(dispatch, 1));
}

}  // namespace

int main() {
  assert_default_budget_unlimited();
  assert_context_budget_two();
  return 0;
}
