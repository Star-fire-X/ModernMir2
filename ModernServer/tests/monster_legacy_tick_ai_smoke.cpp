#include <cassert>
#include <cstdint>
#include <optional>
#include <string>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/map_actor.hpp"

namespace {

bool has_process_monster_action(const mir2::RuntimeDispatch& dispatch,
                                std::uint64_t actor_id,
                                const std::string& action) {
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == "ProcessMonsters" && trace.actor_id == actor_id &&
        trace.action == action) {
      return true;
    }
  }
  return false;
}

std::optional<mir2::DecodedLegacyGamePacket> find_packet_by_recog(
    const mir2::RuntimeDispatch& dispatch, std::uint16_t ident, std::int32_t recog) {
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident &&
        decoded->message.recog == recog) {
      return decoded;
    }
  }
  return std::nullopt;
}

mir2::ActorMail make_monster(std::uint64_t actor_id, std::int32_t x, std::int32_t y,
                             mir2::MonsterAiProfile profile,
                             std::int32_t walk_step = 1,
                             std::int32_t walk_wait_ms = 0,
                             std::int32_t race_server = 0,
                             std::int32_t attack_speed_ms = 200) {
  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::spawn_monster;
  mail.map_id = "0";
  mail.actor_id = actor_id;
  mail.name = "Oma";
  mail.x = x;
  mail.y = y;
  mail.max_hp = 20;
  mail.attack_power = 3;
  mail.dc_min = 3;
  mail.dc_max = 3;
  mail.exp_reward = 1;
  mail.walk_speed_ms = 200;
  mail.walk_step = walk_step;
  mail.walk_wait_ms = walk_wait_ms;
  mail.attack_speed_ms = attack_speed_ms;
  mail.monster_ai_profile = profile;
  mail.race_server = race_server;
  mail.monster_search_rate_ms = 1500;
  mail.dir = 4;
  mail.legacy_spawn_group = true;
  return mail;
}

bool has_monster_ai_trace(const mir2::RuntimeDispatch& dispatch,
                          const std::string& action) {
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == "MonsterAI" && trace.action == action) {
      return true;
    }
  }
  return false;
}

mir2::ActorMail make_player(std::uint64_t actor_id, std::uint64_t session_id,
                            const std::string& name, std::int32_t x, std::int32_t y) {
  mir2::CharacterRecord hero;
  hero.account_id = name;
  hero.character_name = name;
  hero.map_id = "0";
  hero.x = x;
  hero.y = y;
  hero.ability.level = 1;
  hero.ability.hp = 40;
  hero.ability.max_hp = 40;
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

mir2::MapActor make_map() {
  mir2::LogicBudgetConfig budgets;
  budgets.tick_ms = 20;
  return mir2::MapActor(mir2::MapConfig{"0", "TickAI", {}, 0, 0, 30, 30},
                        budgets, {}, {});
}

void spawn(mir2::MapActor& map, const mir2::ActorMail& monster,
           const mir2::ActorMail& player, std::uint64_t now_ms = 0) {
  map.enqueue_mail(monster);
  map.enqueue_mail(player);
  static_cast<void>(map.tick(1, now_ms));
}

}  // namespace

int main() {
  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 100;
    spawn(map, make_monster(monster_id, 10, 8, mir2::MonsterAiProfile::basic),
          make_player(1, 10, "Hero", 10, 10));

    const auto too_early = map.legacy_process_monster(monster_id, 2, 250, 0, 0);
    assert(has_process_monster_action(too_early, monster_id, "skip"));
    auto snapshot = map.legacy_monster_snapshot(monster_id);
    assert(snapshot.has_value());
    assert(snapshot->legacy_run_time_ms == 0);
    assert(snapshot->legacy_run_next_tick_ms == 250);

    const auto due = map.legacy_process_monster(monster_id, 3, 251, 0, 0);
    assert(has_process_monster_action(due, monster_id, "run"));
    snapshot = map.legacy_monster_snapshot(monster_id);
    assert(snapshot.has_value());
    assert(snapshot->legacy_run_time_ms == 251);
    assert(snapshot->legacy_search_time_ms == 0);
  }

  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 109;
    spawn(map, make_monster(monster_id, 10, 8, mir2::MonsterAiProfile::basic),
          make_player(12, 120, "SearchHero", 20, 20));

    static_cast<void>(map.legacy_process_monster(monster_id, 2, 1500, 0, 0));
    auto snapshot = map.legacy_monster_snapshot(monster_id);
    assert(snapshot.has_value());
    assert(snapshot->legacy_search_time_ms == 0);
    assert(snapshot->legacy_search_rate_ms == 1500);

    static_cast<void>(map.legacy_process_monster(monster_id, 3, 1751, 0, 0));
    snapshot = map.legacy_monster_snapshot(monster_id);
    assert(snapshot.has_value());
    assert(snapshot->legacy_search_time_ms == 1751);
  }

  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 104;
    spawn(map, make_monster(monster_id, 10, 8, mir2::MonsterAiProfile::aggressive,
                            1, 0, 80),
          make_player(6, 60, "PassiveHero", 10, 9));

    const auto dispatch = map.legacy_process_monster(monster_id, 2, 1001, 0, 0);
    assert(!find_packet_by_recog(dispatch, mir2::kSmHit,
                                 static_cast<std::int32_t>(monster_id)).has_value());
    const auto snapshot = map.legacy_monster_snapshot(monster_id);
    assert(snapshot.has_value());
    assert(snapshot->target_actor_id == 0);
  }

  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 101;
    map.enqueue_mail(make_monster(monster_id, 10, 8, mir2::MonsterAiProfile::basic,
                                  1, 0, 81));
    map.enqueue_mail(make_player(2, 20, "FarHero", 10, 11));
    map.enqueue_mail(make_player(3, 30, "NearHero", 8, 8));
    static_cast<void>(map.tick(1, 0));

    const auto dispatch = map.legacy_process_monster(monster_id, 2, 1001, 0, 0);
    const auto walk = find_packet_by_recog(dispatch, mir2::kSmWalk,
                                           static_cast<std::int32_t>(monster_id));
    assert(walk.has_value());
    assert(walk->message.param == 9);
    assert(walk->message.tag == 8);
    assert(walk->message.series == 6);

    const auto snapshot = map.legacy_monster_snapshot(monster_id);
    assert(snapshot.has_value());
    assert(snapshot->target_actor_id == 3);
    assert(snapshot->target_x == 8);
    assert(snapshot->target_y == 8);
  }

  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 105;
    map.enqueue_mail(make_monster(monster_id, 10, 8, mir2::MonsterAiProfile::basic,
                                  1, 0, 81));
    map.enqueue_mail(make_player(7, 70, "FirstHero", 10, 11));
    static_cast<void>(map.tick(1, 0));

    const auto first = map.legacy_process_monster(monster_id, 2, 1001, 0, 0);
    assert(has_monster_ai_trace(first, "MonsterNormalAttack"));
    auto snapshot = map.legacy_monster_snapshot(monster_id);
    assert(snapshot.has_value());
    assert(snapshot->target_actor_id == 7);
    assert(snapshot->search_enemy_time_ms == 1001);

    map.enqueue_mail(make_player(8, 80, "NearHero", snapshot->x, snapshot->y + 1));
    static_cast<void>(map.tick(2, 1100));

    const auto too_early = map.legacy_process_monster(monster_id, 3, 2002, 0, 0);
    assert(!has_monster_ai_trace(too_early, "MonsterNormalAttack"));
    snapshot = map.legacy_monster_snapshot(monster_id);
    assert(snapshot.has_value());
    assert(snapshot->search_enemy_time_ms == 1001);
    assert(snapshot->target_actor_id == 7);

    const auto refreshed = map.legacy_process_monster(monster_id, 4, 9002, 0, 0);
    assert(has_monster_ai_trace(refreshed, "MonsterNormalAttack"));
    snapshot = map.legacy_monster_snapshot(monster_id);
    assert(snapshot.has_value());
    assert(snapshot->search_enemy_time_ms == 9002);
    assert(snapshot->target_actor_id == 8);
  }

  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 106;
    spawn(map, make_monster(monster_id, 10, 9, mir2::MonsterAiProfile::basic,
                            1, 0, 81, 1000),
          make_player(9, 90, "CooldownHero", 10, 10));

    const auto first = map.legacy_process_monster(monster_id, 2, 1001, 0, 0);
    assert(find_packet_by_recog(first, mir2::kSmHit,
                                static_cast<std::int32_t>(monster_id)).has_value());
    const auto after_hit = map.legacy_monster_snapshot(monster_id);
    assert(after_hit.has_value());
    assert(after_hit->hit_time_ms == 1001);

    const auto cooldown = map.legacy_process_monster(monster_id, 3, 1252, 0, 0);
    assert(!find_packet_by_recog(cooldown, mir2::kSmHit,
                                 static_cast<std::int32_t>(monster_id)).has_value());
    assert(!find_packet_by_recog(cooldown, mir2::kSmWalk,
                                 static_cast<std::int32_t>(monster_id)).has_value());
    const auto after_cooldown = map.legacy_monster_snapshot(monster_id);
    assert(after_cooldown.has_value());
    assert(after_cooldown->x == after_hit->x);
    assert(after_cooldown->y == after_hit->y);
    assert(after_cooldown->hit_time_ms == after_hit->hit_time_ms);
  }

  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 107;
    spawn(map, make_monster(monster_id, 10, 8, mir2::MonsterAiProfile::basic,
                            1, 0, 81),
          make_player(10, 100, "GoneHero", 10, 11));

    static_cast<void>(map.legacy_process_monster(monster_id, 2, 1001, 0, 0));
    auto selected = map.legacy_monster_snapshot(monster_id);
    assert(selected.has_value());
    assert(selected->target_actor_id == 10);

    static_cast<void>(map.legacy_disconnect_player(10, 2000));
    const auto dispatch = map.legacy_process_monster(monster_id, 200, 5000, 0, 0);
    assert(!find_packet_by_recog(dispatch, mir2::kSmHit,
                                 static_cast<std::int32_t>(monster_id)).has_value());
    const auto cleared = map.legacy_monster_snapshot(monster_id);
    assert(cleared.has_value());
    assert(cleared->target_actor_id == 0);
    assert(cleared->target_x == -1);
    assert(cleared->target_y == -1);
  }

  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 108;
    spawn(map, make_monster(monster_id, 10, 5, mir2::MonsterAiProfile::aggressive,
                            1, 0, 85),
          make_player(11, 110, "HiddenHero", 10, 12));

    const auto dispatch = map.legacy_process_monster(monster_id, 2, 1001, 0, 0);
    assert(!has_monster_ai_trace(dispatch, "MonsterNormalAttack"));
    assert(!find_packet_by_recog(dispatch, mir2::kSmWalk,
                                 static_cast<std::int32_t>(monster_id)).has_value());
    const auto hidden = map.legacy_monster_snapshot(monster_id);
    assert(hidden.has_value());
    assert(hidden->hide_mode);
    assert(hidden->target_actor_id == 0);
  }

  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 102;
    spawn(map, make_monster(monster_id, 10, 8, mir2::MonsterAiProfile::aggressive),
          make_player(4, 40, "DiagHero", 12, 10));

    const auto dispatch = map.legacy_process_monster(monster_id, 2, 1001, 0, 0);
    const auto walk = find_packet_by_recog(dispatch, mir2::kSmWalk,
                                           static_cast<std::int32_t>(monster_id));
    assert(walk.has_value());
    assert(walk->message.param == 11);
    assert(walk->message.tag == 9);
    assert(walk->message.series == 3);
  }

  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 103;
    spawn(map, make_monster(monster_id, 10, 8, mir2::MonsterAiProfile::aggressive,
                            1, 500),
          make_player(5, 50, "WaitHero", 10, 12));

    const auto first = map.legacy_process_monster(monster_id, 2, 1001, 0, 0);
    assert(find_packet_by_recog(first, mir2::kSmWalk,
                                static_cast<std::int32_t>(monster_id)).has_value());

    const auto second = map.legacy_process_monster(monster_id, 3, 1302, 0, 0);
    assert(find_packet_by_recog(second, mir2::kSmWalk,
                                static_cast<std::int32_t>(monster_id)).has_value());
    auto waiting = map.legacy_monster_snapshot(monster_id);
    assert(waiting.has_value());
    assert(waiting->walk_wait_mode);

    const auto blocked = map.legacy_process_monster(monster_id, 4, 1603, 0, 0);
    assert(!find_packet_by_recog(blocked, mir2::kSmWalk,
                                 static_cast<std::int32_t>(monster_id)).has_value());

    const auto resumed = map.legacy_process_monster(monster_id, 5, 1904, 0, 0);
    assert(find_packet_by_recog(resumed, mir2::kSmWalk,
                                static_cast<std::int32_t>(monster_id)).has_value());
  }

  return 0;
}
