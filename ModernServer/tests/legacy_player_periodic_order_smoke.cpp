#include <algorithm>
#include <cassert>
#include <string>
#include <utility>
#include <vector>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

mir2::LegacyReadyUser make_ready() {
  mir2::CharacterRecord character;
  character.account_id = "acct_periodic";
  character.character_name = "PeriodicHero";
  character.map_id = "0";
  character.x = 10;
  character.y = 10;
  character.ability.level = 10;
  character.ability.hp = 20;
  character.ability.max_hp = 20;

  mir2::LegacyReadyUser ready;
  ready.session_id = 61;
  ready.account_id = character.account_id;
  ready.character_name = character.character_name;
  ready.map_id = character.map_id;
  ready.x = character.x;
  ready.y = character.y;
  ready.character = std::move(character);
  return ready;
}

mir2::LogicCommand make_say() {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::say;
  command.session_id = 61;
  command.text = "periodic order";
  command.game_message = mir2::make_default_message(mir2::kCmSay, 0, 0, 0, 0);
  return command;
}

std::vector<std::string> player_actions(const mir2::RuntimeDispatch& dispatch) {
  std::vector<std::string> actions;
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == "PlayerOperate") {
      actions.push_back(trace.action);
    }
  }
  return actions;
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "PeriodicMap", {}, 0, 0, 20, 20});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  static_cast<void>(runtime.enqueue_ready_user(make_ready()));

  const auto notice = runtime.tick(1000);
  assert(std::any_of(notice.audit_events.begin(), notice.audit_events.end(),
                     [](const mir2::AuditEvent& event) {
                       return event.category == "world.run_notice";
                     }));

  const auto initialize = runtime.tick(1251);
  assert(std::any_of(initialize.audit_events.begin(), initialize.audit_events.end(),
                     [](const mir2::AuditEvent& event) {
                       return event.category == "world.initialize";
                     }));

  static_cast<void>(runtime.route_logic_command(make_say()));
  const auto periodic = runtime.tick(901252);
  const std::vector<std::string> expected{
      "pre_periodic",
      "operate_timers",
      "health_spell",
      "status",
      "periodic_hook",
      "line_notice",
      "messages",
      "post_operate",
  };
  assert(player_actions(periodic) == expected);
  assert(std::any_of(periodic.persist_requests.begin(), periodic.persist_requests.end(),
                     [](const mir2::PersistRequest& request) {
                       return request.kind == mir2::PersistRequestKind::save_character;
                     }));
  return 0;
}
