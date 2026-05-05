#include <cassert>
#include <string>
#include <vector>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

constexpr std::int32_t kPoisonDecHealth = 0;

mir2::CharacterRecord make_character(std::string name = "OperateHero") {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = 10;
  character.y = 10;
  character.ability.level = 10;
  character.ability.hp = 15;
  character.ability.max_hp = 20;
  character.ability.mp = 4;
  character.ability.max_mp = 20;
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

mir2::LogicCommand make_say(std::uint64_t session_id, std::string text) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::say;
  command.session_id = session_id;
  command.text = std::move(text);
  command.game_message = mir2::make_default_message(mir2::kCmSay, 0, 0, 0, 0);
  return command;
}

std::vector<std::string> player_operate_actions(const mir2::RuntimeDispatch& dispatch) {
  std::vector<std::string> actions;
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == "PlayerOperate") {
      actions.push_back(trace.action);
    }
  }
  return actions;
}

bool has_hear(const mir2::RuntimeDispatch& dispatch, const std::string& expected) {
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (!decoded.has_value() || decoded->message.ident != mir2::kSmHear) {
      continue;
    }
    if (mir2::legacy_decode_string(decoded->body) == expected) {
      return true;
    }
  }
  return false;
}

void assert_player_operate_order() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "OperateMap", {}, 0, 0, 20, 20});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  static_cast<void>(runtime.enqueue_ready_user(make_ready(41)));
  static_cast<void>(runtime.tick(1000));
  static_cast<void>(runtime.tick(1251));
  assert(runtime.legacy_session_state(41) == mir2::LegacyPlayerState::running);

  static_cast<void>(runtime.route_logic_command(make_say(41, "after phases")));
  const auto early = runtime.tick(1400);
  assert(player_operate_actions(early).empty());
  assert(!has_hear(early, "OperateHero: after phases"));
  assert(runtime.legacy_session_inbox_size(41) == 1);

  const auto due = runtime.tick(1502);
  const std::vector<std::string> expected{
      "pre_periodic",
      "operate_timers",
      "health_spell",
      "status",
      "messages",
      "post_operate",
  };
  assert(player_operate_actions(due) == expected);
  assert(has_hear(due, "OperateHero: after phases"));
  assert(runtime.legacy_session_inbox_size(41) == 0);
}

void assert_health_spell_tick() {
  auto character = make_character("HealthSpell");
  character.ability.hp = 10;
  character.ability.mp = 3;
  mir2::Player player(1, 51, std::move(character));

  player.queue_legacy_health_spell(20, 20, 10, 0, 1);
  auto tick = player.tick_legacy_health_spell(0);
  assert(!tick.changed);

  tick = player.tick_legacy_health_spell(1);
  assert(tick.changed);
  assert(tick.hp == 10);
  assert(tick.mp == 6);
  assert(player.character().ability.hp == 20);
  assert(player.character().ability.mp == 9);

  tick = player.tick_legacy_health_spell(2);
  assert(tick.changed);
  assert(tick.hp == 0);
  assert(tick.mp == 6);
  assert(player.character().ability.hp == 20);
  assert(player.character().ability.mp == 15);
}

void assert_poison_status_is_separate_from_health_spell() {
  auto character = make_character("Poisoned");
  character.ability.hp = 10;
  character.ability.mp = 10;
  mir2::Player player(2, 52, std::move(character));

  player.queue_legacy_health_spell(10, 0, 0, 0, 1);
  assert(player.apply_legacy_poison(kPoisonDecHealth, 5, 1, 1, 99, 0));

  const auto health = player.tick_legacy_health_spell(1);
  assert(health.changed);
  assert(health.hp == 6);
  assert(player.character().ability.hp == 16);

  const auto status = player.tick_status_effects(1);
  assert(status.damage == 2);
  assert(player.character().ability.hp == 14);
}

}  // namespace

int main() {
  assert_player_operate_order();
  assert_health_spell_tick();
  assert_poison_status_is_separate_from_health_spell();
  return 0;
}
