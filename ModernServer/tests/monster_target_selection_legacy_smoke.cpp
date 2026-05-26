#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "protocol/legacy_types.hpp"
#include "world/map_actor.hpp"

#undef assert
#define assert(expr)                                                            \
  do {                                                                          \
    if (!(expr)) {                                                              \
      std::cerr << "Assertion failed: " #expr << " at " << __FILE__ << ":"    \
                << __LINE__ << "\n";                                           \
      return 1;                                                                 \
    }                                                                           \
  } while (false)

namespace {

mir2::ActorMail make_monster(std::uint64_t actor_id, std::int32_t x, std::int32_t y,
                             mir2::MonsterAiProfile profile = mir2::MonsterAiProfile::ranged,
                             std::int32_t race_server = 0,
                             std::uint64_t search_rate_ms = 80,
                             std::uint64_t master_actor_id = 0) {
  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::spawn_monster;
  mail.map_id = "0";
  mail.actor_id = actor_id;
  mail.name = "Oma";
  mail.x = x;
  mail.y = y;
  mail.max_hp = 40;
  mail.attack_power = 6;
  mail.dc_min = 6;
  mail.dc_max = 6;
  mail.accuracy = 20;
  mail.exp_reward = 1;
  mail.walk_speed_ms = 200;
  mail.attack_speed_ms = 200;
  mail.monster_ai_profile = profile;
  mail.race_server = race_server;
  mail.monster_search_rate_ms = search_rate_ms;
  mail.master_actor_id = master_actor_id;
  mail.monster_is_slave = master_actor_id != 0;
  mail.legacy_spawn_group = true;
  return mail;
}

mir2::ActorMail make_player(std::uint64_t actor_id, std::uint64_t session_id,
                            const std::string& name, std::int32_t x, std::int32_t y,
                            std::int32_t hp = 40) {
  mir2::CharacterRecord hero;
  hero.account_id = name;
  hero.character_name = name;
  hero.map_id = "0";
  hero.x = x;
  hero.y = y;
  hero.ability.level = 1;
  hero.ability.dc = mir2::make_word(6, 6);
  hero.ability.hp = hp;
  hero.ability.max_hp = hp;
  hero.ability.mp = 10;
  hero.ability.max_mp = 10;
  hero.ability.max_exp = 100;
  hero.ability.max_weight = 100;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;

  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::spawn_player;
  mail.map_id = "0";
  mail.actor_id = actor_id;
  mail.session_id = session_id;
  mail.character = hero;
  mail.x = x;
  mail.y = y;
  return mail;
}

mir2::ActorMail make_transparent(std::uint64_t caster_id, std::uint64_t target_id,
                                 std::uint64_t duration_ticks) {
  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::legacy_delayed_effect;
  mail.map_id = "0";
  mail.actor_id = caster_id;
  mail.target_actor_id = target_id;
  mail.delayed_effect_kind = mir2::LegacyDelayedEffectKind::transparent;
  mail.duration_ticks = duration_ticks;
  return mail;
}

mir2::ActorMail make_poison(std::uint64_t caster_id, std::uint64_t target_id,
                            std::uint64_t duration_ticks) {
  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::legacy_delayed_effect;
  mail.map_id = "0";
  mail.actor_id = caster_id;
  mail.target_actor_id = target_id;
  mail.delayed_effect_kind = mir2::LegacyDelayedEffectKind::make_poison;
  mail.poison_kind = 0;
  mail.poison_level = 0;
  mail.duration_ticks = duration_ticks;
  return mail;
}

mir2::ActorMail make_attack(std::uint64_t actor_id, std::uint64_t session_id,
                            std::uint64_t target_actor_id, std::int32_t x,
                            std::int32_t y) {
  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::attack;
  mail.map_id = "0";
  mail.actor_id = actor_id;
  mail.session_id = session_id;
  mail.target_actor_id = target_actor_id;
  mail.x = x;
  mail.y = y;
  mail.game_message.ident = mir2::kCmHit;
  return mail;
}

mir2::MapActor make_map(std::vector<mir2::MapZoneConfig> safe_zones = {}) {
  mir2::LogicBudgetConfig budgets;
  budgets.tick_ms = 20;
  mir2::MapConfig config{"0", "TargetLegacy", {}, 0, 0, 30, 30};
  config.safe_zones = std::move(safe_zones);
  return mir2::MapActor(std::move(config), budgets, {}, {});
}

void spawn(mir2::MapActor& map, const std::vector<mir2::ActorMail>& mails,
           std::uint64_t tick = 1, std::uint64_t now_ms = 0) {
  for (const auto& mail : mails) {
    map.enqueue_mail(mail);
  }
  static_cast<void>(map.tick(tick, now_ms));
}

bool contains_actor(const std::vector<std::uint64_t>& actors, std::uint64_t actor_id) {
  return std::find(actors.begin(), actors.end(), actor_id) != actors.end();
}

}  // namespace

int main() {
  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 100;
    spawn(map, {make_monster(monster_id, 10, 10),
                make_player(1, 10, "First", 12, 10)});

    static_cast<void>(map.legacy_process_monster(monster_id, 2, 1001, 0, 0));
    auto snapshot = map.legacy_monster_snapshot(monster_id);
    assert(snapshot.has_value());
    assert(snapshot->legacy_visible_actor_ids.size() == 1);
    assert(snapshot->legacy_visible_actor_ids[0] == 1);
    assert(snapshot->target_actor_id == 1);

    spawn(map, {make_player(2, 20, "TieButLater", 8, 10)}, 3, 300);
    static_cast<void>(map.legacy_process_monster(monster_id, 4, 1252, 0, 0));
    snapshot = map.legacy_monster_snapshot(monster_id);
    assert(snapshot.has_value());
    assert(snapshot->legacy_visible_actor_ids.size() == 2);
    assert(snapshot->legacy_visible_actor_ids[0] == 1);
    assert(snapshot->legacy_visible_actor_ids[1] == 2);

    static_cast<void>(map.legacy_process_monster(monster_id, 5, 9002, 0, 0));
    snapshot = map.legacy_monster_snapshot(monster_id);
    assert(snapshot.has_value());
    assert(snapshot->target_actor_id == 1);

    spawn(map, {make_player(3, 30, "Closer", 10, 11)}, 6, 8400);
    static_cast<void>(map.legacy_process_monster(monster_id, 7, 17003, 0, 0));
    snapshot = map.legacy_monster_snapshot(monster_id);
    assert(snapshot.has_value());
    assert(snapshot->target_actor_id == 3);
  }

  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 105;
    spawn(map, {make_monster(monster_id, 10, 10),
                make_player(10, 100, "InRange", 15, 10),
                make_player(11, 110, "OutOfRange", 16, 10)});

    static_cast<void>(map.legacy_process_monster(monster_id, 2, 1001, 0, 0));
    const auto snapshot = map.legacy_monster_snapshot(monster_id);
    assert(snapshot.has_value());
    assert(snapshot->legacy_visible_actor_ids.size() == 1);
    assert(snapshot->legacy_visible_actor_ids[0] == 10);
    assert(snapshot->target_actor_id == 10);
  }

  {
    auto map = make_map({mir2::MapZoneConfig{8, 8, 6, 6}});
    constexpr std::uint64_t monster_id = 110;
    spawn(map, {make_monster(monster_id, 10, 10),
                make_player(4, 40, "Safe", 11, 10)});

    static_cast<void>(map.legacy_process_monster(monster_id, 2, 1001, 0, 0));
    auto snapshot = map.legacy_monster_snapshot(monster_id);
    assert(snapshot.has_value());
    assert(snapshot->target_actor_id == 0);
  }

  {
    auto map = make_map();
    constexpr std::uint64_t master_id = 165;
    constexpr std::uint64_t slave_id = 166;
    constexpr std::uint64_t session_id = 1650;
    spawn(map, {make_player(master_id, session_id, "MasterHitsSlave", 10, 10),
                make_monster(slave_id, 10, 9, mir2::MonsterAiProfile::basic,
                             0, 80, master_id)});

    const auto attack = make_attack(master_id, session_id, slave_id, 10, 9);
    assert(map.enqueue_legacy_player_command(attack, 20));
    static_cast<void>(map.legacy_process_player(master_id, 2, 20, false));

    const auto snapshot = map.legacy_monster_snapshot(slave_id);
    assert(snapshot.has_value());
    assert(snapshot->target_actor_id == 0);
  }

  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 120;
    spawn(map, {make_monster(monster_id, 10, 10),
                make_player(5, 50, "Hidden", 11, 10)});
    static_cast<void>(map.legacy_process_monster(monster_id, 2, 1001, 0, 0));
    auto selected = map.legacy_monster_snapshot(monster_id);
    assert(selected.has_value());
    assert(selected->target_actor_id == 5);

    map.enqueue_mail(make_transparent(5, 5, 1000));
    static_cast<void>(map.tick(3, 300));
    static_cast<void>(map.legacy_process_monster(monster_id, 301, 1252, 0, 0));
    auto cleared = map.legacy_monster_snapshot(monster_id);
    assert(cleared.has_value());
    assert(cleared->target_actor_id == 0);
    assert(cleared->target_x == -1);
    assert(cleared->target_y == -1);
  }

  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 130;
    spawn(map, {make_monster(monster_id, 10, 10),
                make_player(6, 60, "Fragile", 11, 10, 1)});
    static_cast<void>(map.legacy_process_monster(monster_id, 2, 1001, 0, 0));
    static_cast<void>(map.legacy_process_monster(monster_id, 3, 1252, 0, 0));
    auto cleared = map.legacy_monster_snapshot(monster_id);
    assert(cleared.has_value());
    assert(cleared->target_actor_id == 0);
  }

  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 140;
    spawn(map, {make_monster(monster_id, 10, 10),
                make_player(7, 70, "Gone", 11, 10)});
    static_cast<void>(map.legacy_process_monster(monster_id, 2, 1001, 0, 0));
    static_cast<void>(map.legacy_disconnect_player(7, 500));
    static_cast<void>(map.legacy_process_monster(monster_id, 3, 1252, 0, 0));
    auto cleared = map.legacy_monster_snapshot(monster_id);
    assert(cleared.has_value());
    assert(cleared->target_actor_id == 0);
    assert(!contains_actor(cleared->legacy_visible_actor_ids, 7));
  }

  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 150;
    spawn(map, {make_monster(monster_id, 10, 10),
                make_monster(151, 9, 10, mir2::MonsterAiProfile::basic),
                make_monster(152, 12, 10, mir2::MonsterAiProfile::basic, 0, 80, 20)});

    static_cast<void>(map.legacy_process_monster(monster_id, 2, 1001, 0, 0));
    const auto snapshot = map.legacy_monster_snapshot(monster_id);
    assert(snapshot.has_value());
    assert(snapshot->target_actor_id == 152);
  }

  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 155;
    constexpr std::uint64_t master_id = 156;
    constexpr std::uint64_t slave_id = 157;
    auto slave = make_monster(slave_id, 10, 11, mir2::MonsterAiProfile::basic,
                              0, 80, master_id);
    slave.target_actor_id = monster_id;
    spawn(map, {make_player(master_id, 1560, "Master", 20, 20),
                make_monster(monster_id, 10, 10, mir2::MonsterAiProfile::basic),
                slave});

    static_cast<void>(map.legacy_process_monster(slave_id, 2, 1001, 0, 0));
    const auto snapshot = map.legacy_monster_snapshot(monster_id);
    assert(snapshot.has_value());
    assert(snapshot->hp < snapshot->max_hp);
    assert(snapshot->target_actor_id == slave_id);
    assert(snapshot->target_actor_id != master_id);
    assert(snapshot->target_actor_id != 0);
  }

  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 160;
    spawn(map, {make_monster(monster_id, 10, 10),
                make_player(8, 80, "Initial", 12, 10)});
    static_cast<void>(map.legacy_process_monster(monster_id, 2, 1001, 0, 0));
    auto snapshot = map.legacy_monster_snapshot(monster_id);
    assert(snapshot.has_value());
    assert(snapshot->legacy_search_time_ms == 1001);
    assert(snapshot->legacy_visible_actor_ids.size() == 1);
    assert(snapshot->legacy_visible_actor_ids[0] == 8);

    spawn(map, {make_player(9, 90, "StatusOnlyNew", 9, 10)}, 3, 300);
    map.enqueue_mail(make_poison(8, monster_id, 300));
    static_cast<void>(map.tick(10, 300));
    static_cast<void>(map.legacy_process_monster(monster_id, 135, 1202, 0, 0));
    snapshot = map.legacy_monster_snapshot(monster_id);
    assert(snapshot.has_value());
    assert(snapshot->legacy_search_time_ms == 1001);
    assert(snapshot->legacy_visible_actor_ids.size() == 1);
    assert(snapshot->legacy_visible_actor_ids[0] == 8);
  }

  return 0;
}
