#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "shared/legacy/action_ids.hpp"
#include "world/game_object.hpp"
#include "world/logic_runtime.hpp"

namespace {

mir2::RuntimeDispatch tick_player(mir2::LogicRuntime& runtime, std::uint64_t now_ms,
                                  std::size_t budget = 1) {
  mir2::LegacyRuntimeContext context;
  context.player_input_budget_per_tick = budget;
  return runtime.tick(now_ms, context);
}

void append(mir2::RuntimeDispatch& target, mir2::RuntimeDispatch source) {
  target.session_events.insert(target.session_events.end(),
                               std::make_move_iterator(source.session_events.begin()),
                               std::make_move_iterator(source.session_events.end()));
  target.legacy_traces.insert(target.legacy_traces.end(),
                              std::make_move_iterator(source.legacy_traces.begin()),
                              std::make_move_iterator(source.legacy_traces.end()));
}

bool raw_body_starts_with(const mir2::LegacyPacket& packet, std::string_view prefix) {
  return packet.body.size() >= prefix.size() &&
         std::equal(prefix.begin(), prefix.end(), packet.body.begin());
}

std::int32_t count_ack(const mir2::RuntimeDispatch& dispatch, std::uint64_t session_id,
                       bool ok) {
  const auto prefix = ok ? std::string_view("+GOOD/") : std::string_view("+FAIL/");
  return static_cast<std::int32_t>(std::count_if(
      dispatch.session_events.begin(), dispatch.session_events.end(),
      [&](const mir2::SessionEvent& event) {
        return event.session_id == session_id &&
               event.kind == mir2::SessionEventKind::send_packet &&
               raw_body_starts_with(event.packet, prefix);
      }));
}

std::int32_t count_packet_ident(const mir2::RuntimeDispatch& dispatch, std::uint16_t ident) {
  return static_cast<std::int32_t>(std::count_if(
      dispatch.session_events.begin(), dispatch.session_events.end(),
      [&](const mir2::SessionEvent& event) {
        const auto decoded = mir2::decode_legacy_game_packet(event.packet);
        return decoded.has_value() && decoded->message.ident == ident;
      }));
}

bool has_force_disconnect(const mir2::RuntimeDispatch& dispatch, std::uint64_t session_id,
                          std::string_view reason) {
  return std::any_of(dispatch.session_events.begin(), dispatch.session_events.end(),
                     [&](const mir2::SessionEvent& event) {
                       return event.session_id == session_id &&
                              event.kind == mir2::SessionEventKind::force_disconnect &&
                              event.reason == reason;
                     });
}

bool has_trace(const mir2::RuntimeDispatch& dispatch, std::string_view stage,
               std::string_view action) {
  return std::any_of(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                     [&](const mir2::LegacyRuntimeTrace& trace) {
                       return trace.stage == stage && trace.action == action;
                     });
}

mir2::ItemConfig speed_weapon_config(std::int32_t id, std::int32_t hit_speed = 2) {
  mir2::ItemConfig item;
  item.id = id;
  item.name = "Speed Sword";
  item.std_mode = 5;
  item.equip_slot = static_cast<std::int32_t>(mir2::kEquipWeapon);
  item.weight = 1;
  item.dura_max = 1000;
  const auto mac_high = hit_speed > 0 ? hit_speed + 10 : -hit_speed;
  item.mac = mir2::make_word(0, static_cast<std::uint8_t>(mac_high));
  item.dc = mir2::make_word(4, 4);
  return item;
}

mir2::MagicConfig long_hit_magic() {
  mir2::MagicConfig magic;
  magic.id = 12;
  magic.name = "LongHit";
  magic.legacy.legacy_present = true;
  magic.legacy.is_sword_skill = true;
  magic.legacy.need_level = {1, 1, 1, 1};
  magic.legacy.max_train = {2, 500, 1000, 1000};
  magic.legacy.max_train_level = 3;
  magic.legacy.delay_time = 0;
  return magic;
}

mir2::HostConfig base_config() {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 1;
  config.budgets.tick_ms = 20;
  config.maps.push_back(mir2::MapConfig{"0", "CadenceMap", {}, 0, 0, 30, 30});
  return config;
}

mir2::SpawnConfig target(std::string name, std::int32_t x, std::int32_t y,
                         std::int32_t hp = 500) {
  mir2::SpawnConfig spawn;
  spawn.map_id = "0";
  spawn.actor_type = "monster";
  spawn.name = std::move(name);
  spawn.x = x;
  spawn.y = y;
  spawn.respawn_ms = 30000;
  spawn.level = 1;
  spawn.max_hp = hp;
  spawn.attack_power = 0;
  spawn.defense = 0;
  spawn.magic_defense = 0;
  spawn.exp_reward = 10;
  return spawn;
}

mir2::LegacyUserItem user_item(std::int32_t make_index, std::int32_t index) {
  mir2::LegacyUserItem item;
  item.make_index = make_index;
  item.index = static_cast<std::uint16_t>(index);
  item.dura = 1000;
  item.dura_max = 1000;
  return item;
}

mir2::CharacterRecord character(std::string name) {
  mir2::CharacterRecord record;
  record.account_id = "acct_" + name;
  record.character_name = std::move(name);
  record.map_id = "0";
  record.x = 10;
  record.y = 10;
  record.dir = 0;
  record.ability.level = 40;
  record.ability.dc = mir2::make_word(20, 20);
  record.ability.hp = 100;
  record.ability.max_hp = 100;
  record.ability.mp = 100;
  record.ability.max_mp = 100;
  record.ability.max_exp = 1000;
  record.ability.max_weight = 30;
  record.ability.max_wear_weight = 100;
  record.ability.max_hand_weight = 100;
  return record;
}

mir2::LogicCommand enter(std::uint64_t session_id, mir2::CharacterRecord record) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = session_id;
  command.account_id = record.account_id;
  command.character_name = record.character_name;
  command.map_id = record.map_id;
  command.x = record.x;
  command.y = record.y;
  command.character = std::move(record);
  return command;
}

mir2::LogicCommand attack(std::uint64_t session_id, std::int32_t x, std::int32_t y,
                          std::uint16_t ident = mir2::kCmHit, std::uint8_t dir = 0) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::attack;
  command.session_id = session_id;
  command.x = x;
  command.y = y;
  command.dir = dir;
  command.game_message.ident = ident;
  return command;
}

mir2::LogicCommand walk(std::uint64_t session_id, std::int32_t x, std::int32_t y,
                        std::uint8_t dir = 0) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::walk;
  command.session_id = session_id;
  command.x = x;
  command.y = y;
  command.dir = dir;
  return command;
}

mir2::LogicCommand spell(std::uint64_t session_id, std::int32_t magic_id) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::spell;
  command.session_id = session_id;
  command.game_message.ident = mir2::kCmSpell;
  command.game_message.tag = static_cast<std::uint16_t>(magic_id);
  return command;
}

mir2::LogicCommand drop_gold(std::uint64_t session_id, std::int32_t amount) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::drop_gold;
  command.session_id = session_id;
  command.amount = amount;
  return command;
}

mir2::LogicCommand pickup(std::uint64_t session_id, std::int32_t x, std::int32_t y) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::pickup_item;
  command.session_id = session_id;
  command.x = x;
  command.y = y;
  return command;
}

void enter_player(mir2::LogicRuntime& runtime, std::uint64_t session_id,
                  mir2::CharacterRecord record) {
  static_cast<void>(runtime.route_logic_command(enter(session_id, std::move(record))));
  const auto login = runtime.tick(1000);
  assert(count_packet_ident(login, mir2::kSmNewMap) == 1);
}

void assert_budget_batch_window_allows_second_attack() {
  auto config = base_config();
  config.spawns.push_back(target("BatchTarget", 10, 9));
  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  enter_player(runtime, 101, character("BatchHero"));

  static_cast<void>(runtime.route_logic_command(attack(101, 10, 9)));
  static_cast<void>(runtime.route_logic_command(attack(101, 10, 9)));
  const auto dispatch = tick_player(runtime, 2000, 2);

  assert(count_ack(dispatch, 101, true) == 2);
  assert(count_ack(dispatch, 101, false) == 0);
  assert(has_trace(dispatch, "LegacyCombat", "attack_broadcast"));
  assert(has_trace(dispatch, "LegacyCombat", "struck"));
  assert(!has_trace(dispatch, "LegacyCombat", "attack_cooldown_reject"));
}

void assert_walk_hit_pickup_batch_executes() {
  auto config = base_config();
  mir2::MonsterDefConfig stationary;
  stationary.name = "PickupBatchTarget";
  stationary.hp = 500;
  stationary.ai_profile = mir2::MonsterAiProfile::stationary;
  config.monsters.push_back(stationary);
  config.spawns.push_back(target("PickupBatchTarget", 10, 11));
  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  auto hero = character("PickupBatchHero");
  hero.gold = 50;
  enter_player(runtime, 107, std::move(hero));

  static_cast<void>(runtime.route_logic_command(drop_gold(107, 10)));
  auto dispatch = tick_player(runtime, 2000);
  assert(count_packet_ident(dispatch, mir2::kSmItemShow) == 1);

  static_cast<void>(runtime.route_logic_command(walk(107, 10, 9, 0)));
  dispatch = tick_player(runtime, 2200);
  assert(count_ack(dispatch, 107, true) == 1);
  for (std::uint64_t frame = 0; frame < 20; ++frame) {
    static_cast<void>(tick_player(runtime, 2220 + frame * 20));
  }

  static_cast<void>(runtime.route_logic_command(walk(107, 10, 10, 4)));
  static_cast<void>(runtime.route_logic_command(attack(107, 10, 11, mir2::kCmHit, 4)));
  static_cast<void>(runtime.route_logic_command(pickup(107, 10, 10)));
  dispatch = tick_player(runtime, 3200, 3);

  assert(count_ack(dispatch, 107, true) >= 1);
  assert(has_trace(dispatch, "LegacyCombat", "struck"));
  assert(count_packet_ident(dispatch, mir2::kSmItemHide) == 1);
  assert(count_packet_ident(dispatch, mir2::kSmGoldChanged) == 1);
}

void assert_walk_hit_spell_batch_executes() {
  auto config = base_config();
  config.magics.push_back(long_hit_magic());
  config.spawns.push_back(target("SpellBatchTarget", 10, 8));
  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  auto hero = character("SpellBatchHero");
  hero.magics[0].magic_id = 12;
  hero.magics[0].level = 0;
  enter_player(runtime, 108, std::move(hero));

  static_cast<void>(runtime.route_logic_command(walk(108, 10, 9, 0)));
  static_cast<void>(runtime.route_logic_command(attack(108, 10, 8)));
  static_cast<void>(runtime.route_logic_command(spell(108, 12)));
  const auto dispatch = tick_player(runtime, 2000, 3);

  assert(count_ack(dispatch, 108, true) >= 1);
  assert(has_trace(dispatch, "LegacyCombat", "struck"));
  assert(has_trace(dispatch, "LegacySkill", "sword_toggle"));
}

void assert_base_interval_allows_later_attack() {
  auto config = base_config();
  config.spawns.push_back(target("BaseTarget", 10, 9));
  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  enter_player(runtime, 102, character("BaseHero"));

  static_cast<void>(runtime.route_logic_command(attack(102, 10, 9)));
  auto dispatch = tick_player(runtime, 2000);
  assert(count_ack(dispatch, 102, true) == 1);

  static_cast<void>(runtime.route_logic_command(attack(102, 10, 9)));
  dispatch = tick_player(runtime, 2800);
  assert(count_ack(dispatch, 102, true) == 1);
  assert(has_trace(dispatch, "LegacyCombat", "struck"));

  static_cast<void>(runtime.route_logic_command(attack(102, 10, 9)));
  dispatch = tick_player(runtime, 3700);
  assert(count_ack(dispatch, 102, true) == 1);
}

void assert_equipped_hit_speed_reduces_interval() {
  auto config = base_config();
  config.items.push_back(speed_weapon_config(5));
  config.spawns.push_back(target("SpeedTarget", 10, 9));
  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  auto hero = character("SpeedHero");
  hero.equipped_items[mir2::kEquipWeapon] = user_item(5001, 5);
  enter_player(runtime, 103, std::move(hero));

  static_cast<void>(runtime.route_logic_command(attack(103, 10, 9)));
  auto dispatch = tick_player(runtime, 2000);
  assert(count_ack(dispatch, 103, true) == 1);

  static_cast<void>(runtime.route_logic_command(attack(103, 10, 9)));
  dispatch = tick_player(runtime, 2780);
  assert(count_ack(dispatch, 103, true) == 1);
}

void assert_server_attack_interval_formula() {
  assert(mir2::legacy_server_attack_interval_ms(0) == 900);
  assert(mir2::legacy_server_attack_interval_ms(5) == 600);
  assert(mir2::legacy_server_attack_interval_ms(10) == 300);
  assert(mir2::legacy_server_attack_interval_ms(12) == 180);
  assert(mir2::legacy_server_attack_interval_ms(14) == 60);
  assert(mir2::legacy_server_attack_interval_ms(15) == 0);
  assert(mir2::legacy_server_attack_interval_ms(16) == -60);
  assert(mir2::legacy_server_attack_interval_ms(-1) == 960);
}

void assert_hit_speed_window_cases() {
  struct HitSpeedCase {
    std::int32_t hit_speed;
    std::int32_t interval_ms;
  };
  constexpr std::array<HitSpeedCase, 8> kCases{{
      {0, 900},
      {5, 600},
      {10, 300},
      {12, 180},
      {14, 60},
      {15, 0},
      {16, -60},
      {-1, 960},
  }};

  for (std::size_t index = 0; index < kCases.size(); ++index) {
    const auto test_case = kCases[index];
    auto config = base_config();
    config.spawns.push_back(target("WindowTarget", 10, 9, 5000));
    if (test_case.hit_speed != 0) {
      config.items.push_back(speed_weapon_config(5, test_case.hit_speed));
    }
    mir2::LogicRuntime runtime(config);
    runtime.initialize();

    auto hero = character("WindowHero" + std::to_string(index));
    if (test_case.hit_speed != 0) {
      hero.equipped_items[mir2::kEquipWeapon] = user_item(5001, 5);
    }
    const auto session_id = 200 + index;
    enter_player(runtime, session_id, std::move(hero));

    static_cast<void>(runtime.route_logic_command(attack(session_id, 10, 9)));
    auto dispatch = tick_player(runtime, 2000);
    assert(count_ack(dispatch, session_id, true) == 1);

    if (test_case.interval_ms <= 0) {
      static_cast<void>(runtime.route_logic_command(attack(session_id, 10, 9)));
      dispatch = tick_player(runtime, 2000);
      assert(count_ack(dispatch, session_id, true) == 1);
      assert(count_ack(dispatch, session_id, false) == 0);
      continue;
    }

    const auto early_ms = 2000 + static_cast<std::uint64_t>(test_case.interval_ms - 1);
    mir2::RuntimeDispatch combined;
    for (int attempt = 0; attempt < 4; ++attempt) {
      static_cast<void>(runtime.route_logic_command(attack(session_id, 10, 9)));
      append(combined, tick_player(runtime, early_ms));
    }
    assert(count_ack(combined, session_id, true) == 3);
    assert(count_ack(combined, session_id, false) == 1);
    assert(has_trace(combined, "LegacyCombat", "attack_cooldown_reject"));

    static_cast<void>(runtime.route_logic_command(attack(session_id, 10, 9)));
    dispatch = tick_player(runtime, early_ms + static_cast<std::uint64_t>(test_case.interval_ms));
    assert(count_ack(dispatch, session_id, true) == 1);
  }
}

void assert_no_target_attack_consumes_cooldown() {
  auto config = base_config();
  config.spawns.push_back(target("CooldownTarget", 10, 9));
  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  enter_player(runtime, 106, character("NoTargetHero"));

  static_cast<void>(runtime.route_logic_command(attack(106, 1, 1, mir2::kCmHit, 2)));
  auto dispatch = tick_player(runtime, 2000);
  assert(count_ack(dispatch, 106, true) == 1);
  assert(has_trace(dispatch, "LegacyCombat", "no_target"));

  mir2::RuntimeDispatch combined;
  for (int attempt = 0; attempt < 4; ++attempt) {
    static_cast<void>(runtime.route_logic_command(attack(106, 10, 9)));
    append(combined, tick_player(runtime, 2010 + static_cast<std::uint64_t>(attempt) * 10));
  }
  assert(count_ack(combined, 106, true) == 3);
  assert(count_ack(combined, 106, false) == 1);
  assert(has_trace(combined, "LegacyCombat", "attack_cooldown_reject"));
}

void assert_repeated_fast_attack_disconnects() {
  auto config = base_config();
  config.spawns.push_back(target("HackTarget", 10, 9));
  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  enter_player(runtime, 104, character("HackHero"));

  static_cast<void>(runtime.route_logic_command(attack(104, 10, 9)));
  auto dispatch = tick_player(runtime, 2000);
  assert(count_ack(dispatch, 104, true) == 1);

  mir2::RuntimeDispatch combined;
  for (int index = 0; index < 12; ++index) {
    static_cast<void>(runtime.route_logic_command(attack(104, 10, 9)));
    append(combined, tick_player(runtime, 2010 + static_cast<std::uint64_t>(index) * 10));
  }
  assert(count_ack(combined, 104, true) == 3);
  assert(count_ack(combined, 104, false) == 9);
  assert(has_force_disconnect(combined, 104, "speed_hack_attack"));
}

void assert_rejected_attack_keeps_toggled_sword_skill() {
  auto config = base_config();
  config.magics.push_back(long_hit_magic());
  config.spawns.push_back(target("LongTarget", 10, 8));
  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  auto hero = character("LongHero");
  hero.magics[0].magic_id = 12;
  hero.magics[0].level = 0;
  enter_player(runtime, 105, std::move(hero));

  static_cast<void>(runtime.route_logic_command(attack(105, 1, 1)));
  auto dispatch = tick_player(runtime, 2000);
  assert(count_ack(dispatch, 105, true) == 1);

  static_cast<void>(runtime.route_logic_command(spell(105, 12)));
  dispatch = tick_player(runtime, 2100);
  assert(has_trace(dispatch, "LegacySkill", "sword_toggle"));

  for (int attempt = 0; attempt < 3; ++attempt) {
    static_cast<void>(runtime.route_logic_command(attack(105, 1, 1)));
    dispatch = tick_player(runtime, 2110 + static_cast<std::uint64_t>(attempt) * 10);
    assert(count_ack(dispatch, 105, true) == 1);
  }

  static_cast<void>(runtime.route_logic_command(attack(105, 10, 8, mir2::kCmLongHit)));
  dispatch = tick_player(runtime, 2140);
  assert(count_ack(dispatch, 105, false) == 1);
  assert(count_packet_ident(dispatch, mir2::legacy::kSmLongHit) == 0);

  static_cast<void>(runtime.route_logic_command(attack(105, 10, 8, mir2::kCmLongHit)));
  dispatch = tick_player(runtime, 3030);
  assert(count_ack(dispatch, 105, true) == 1);
  assert(count_packet_ident(dispatch, mir2::legacy::kSmLongHit) == 1);
  assert(has_trace(dispatch, "LegacyCombat", "struck"));
  assert(has_trace(dispatch, "LegacySkill", "train_skill"));
}

}  // namespace

int main() {
  assert_server_attack_interval_formula();
  assert_hit_speed_window_cases();
  assert_budget_batch_window_allows_second_attack();
  assert_walk_hit_pickup_batch_executes();
  assert_walk_hit_spell_batch_executes();
  assert_base_interval_allows_later_attack();
  assert_equipped_hit_speed_reduces_interval();
  assert_no_target_attack_consumes_cooldown();
  assert_repeated_fast_attack_disconnects();
  assert_rejected_attack_keeps_toggled_sword_skill();
  return 0;
}
