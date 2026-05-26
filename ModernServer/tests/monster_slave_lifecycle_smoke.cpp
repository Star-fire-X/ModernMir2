#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>

#include "world/map_actor.hpp"

namespace {

#define CHECK(expr)                                                            \
  do {                                                                         \
    if (!(expr)) {                                                             \
      std::cerr << "CHECK failed: " #expr << " at " << __FILE__ << ":"       \
                << __LINE__ << "\n";                                          \
      return 1;                                                                \
    }                                                                          \
  } while (false)

constexpr std::uint64_t kScriptMonsterBase = 0x6000000000000000ULL;

mir2::CharacterRecord make_character(std::string name, std::int32_t x,
                                      std::int32_t y, std::uint8_t dir = 0) {
  mir2::CharacterRecord hero;
  hero.account_id = "acct_" + name;
  hero.character_name = std::move(name);
  hero.map_id = "0";
  hero.x = x;
  hero.y = y;
  hero.dir = dir;
  hero.ability.level = 20;
  hero.ability.hp = 50;
  hero.ability.max_hp = 50;
  hero.ability.mp = 30;
  hero.ability.max_mp = 30;
  hero.ability.max_exp = 1000;
  hero.ability.max_weight = 100;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;
  return hero;
}

mir2::ActorMail make_player(std::uint64_t actor_id, std::uint64_t session_id,
                            mir2::CharacterRecord character) {
  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::spawn_player;
  mail.map_id = "0";
  mail.actor_id = actor_id;
  mail.session_id = session_id;
  mail.x = character.x;
  mail.y = character.y;
  mail.character = std::move(character);
  return mail;
}

mir2::ActorMail make_slave(std::uint64_t actor_id, std::uint64_t master_id,
                           std::int32_t x, std::int32_t y,
                           std::uint64_t royalty_time_ms = 60000,
                           std::uint64_t life_time_ms = 0) {
  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::spawn_monster;
  mail.map_id = "0";
  mail.actor_id = actor_id;
  mail.name = "__WhiteSkeleton";
  mail.x = x;
  mail.y = y;
  mail.level = 1;
  mail.max_hp = 40;
  mail.max_mp = 5;
  mail.current_hp = 23;
  mail.current_mp = 2;
  mail.attack_power = 5;
  mail.dc_min = 1;
  mail.dc_max = 5;
  mail.accuracy = 10;
  mail.exp_reward = 1;
  mail.walk_speed_ms = 1;
  mail.attack_speed_ms = 1;
  mail.monster_ai_profile = mir2::MonsterAiProfile::aggressive;
  mail.master_actor_id = master_id;
  mail.monster_is_slave = true;
  mail.monster_no_item = true;
  mail.slave_exp = 42;
  mail.slave_make_level = 1;
  mail.slave_exp_level = 2;
  mail.master_royalty_time_ms = royalty_time_ms;
  mail.slave_life_time_ms = life_time_ms;
  return mail;
}

mir2::MonsterDefConfig make_skeleton_def() {
  mir2::MonsterDefConfig def;
  def.name = "__WhiteSkeleton";
  def.level = 1;
  def.hp = 40;
  def.mp = 5;
  def.dc = 1;
  def.dc_max = 5;
  def.accurate = 10;
  def.walk_speed_ms = 200;
  def.attack_speed_ms = 200;
  return def;
}

mir2::MapActor make_map() {
  mir2::LogicBudgetConfig budgets;
  budgets.tick_ms = 20;
  std::unordered_map<std::string, mir2::MonsterDefConfig> defs;
  defs.emplace("__whiteskeleton", make_skeleton_def());
  return mir2::MapActor(mir2::MapConfig{"0", "SlaveLife", {}, 50, 50, 0, 0},
                        budgets, {}, {}, {}, {}, std::move(defs));
}

bool has_trace(const mir2::RuntimeDispatch& dispatch, const std::string& stage,
               const std::string& action) {
  return std::any_of(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                     [&](const mir2::LegacyRuntimeTrace& trace) {
                       return trace.stage == stage && trace.action == action;
                     });
}

const mir2::PersistRequest* find_save_request(const mir2::RuntimeDispatch& dispatch,
                                              const std::string& character_name) {
  for (const auto& request : dispatch.persist_requests) {
    if (request.kind == mir2::PersistRequestKind::save_character &&
        request.character_name == character_name) {
      return &request;
    }
  }
  return nullptr;
}

}  // namespace

int main() {
  {
    auto map = make_map();
    auto restored = make_character("Restorer", 10, 10);
    restored.slaves[0].name = "__WhiteSkeleton";
    restored.slaves[0].slave_exp = 77;
    restored.slaves[0].slave_exp_level = 2;
    restored.slaves[0].slave_make_level = 1;
    restored.slaves[0].remain_royalty_sec = 60;
    restored.slaves[0].hp = 31;
    restored.slaves[0].mp = 2;

    const auto dispatch =
        map.legacy_spawn_player(make_player(1, 100, std::move(restored)), 1, 1000, true);
    CHECK(has_trace(dispatch, "LegacySlave", "restore"));
    auto slave = map.legacy_monster_snapshot(kScriptMonsterBase);
    CHECK(slave.has_value());
    CHECK(slave->name == "__WhiteSkeleton");
    CHECK(slave->is_slave);
    CHECK(slave->master_actor_id == 1);
    CHECK(slave->hp == 31);
    CHECK(slave->mp == 2);
    CHECK(slave->slave_exp == 77);
    CHECK(slave->slave_exp_level == 2);
    CHECK(slave->master_royalty_time_ms == 61000);
  }

  {
    auto map = make_map();
    static_cast<void>(
        map.legacy_spawn_player(make_player(1, 101, make_character("Saver", 10, 10)),
                                1, 0, true));
    map.enqueue_mail(make_slave(2, 1, 11, 10, 65000));
    static_cast<void>(map.tick(1, 0));

    const auto dispatch = map.legacy_disconnect_player(1, 5000);
    const auto* save = find_save_request(dispatch, "Saver");
    CHECK(save != nullptr);
    CHECK(save->character.slaves[0].name == "__WhiteSkeleton");
    CHECK(save->character.slaves[0].slave_exp == 42);
    CHECK(save->character.slaves[0].slave_exp_level == 2);
    CHECK(save->character.slaves[0].slave_make_level == 1);
    CHECK(save->character.slaves[0].remain_royalty_sec == 60);
    CHECK(save->character.slaves[0].hp == 23);
    CHECK(save->character.slaves[0].mp == 2);
    CHECK(!map.legacy_monster_snapshot(2).has_value());
  }

  {
    auto map = make_map();
    static_cast<void>(
        map.legacy_spawn_player(make_player(1, 102, make_character("Royalty", 10, 10)),
                                1, 0, true));
    map.enqueue_mail(make_slave(2, 1, 11, 10, 1000));
    static_cast<void>(map.tick(1, 0));

    const auto dispatch = map.legacy_process_monster(2, 100, 1001, 0, 0);
    CHECK(has_trace(dispatch, "LegacySlave", "royalty_expired"));
    auto slave = map.legacy_monster_snapshot(2);
    CHECK(slave.has_value());
    CHECK(slave->master_actor_id == 0);
    CHECK(slave->hp == 2);
    CHECK(!slave->death_settled);
  }

  {
    auto map = make_map();
    static_cast<void>(
        map.legacy_spawn_player(make_player(1, 103, make_character("Life", 10, 10)),
                                1, 0, true));
    map.enqueue_mail(make_slave(2, 1, 11, 10, 100000000, 1));
    static_cast<void>(map.tick(1, 0));

    const auto twelve_hours_later = 12ULL * 60ULL * 60ULL * 1000ULL + 2ULL;
    const auto dispatch = map.legacy_process_monster(2, 200, twelve_hours_later, 0, 0);
    CHECK(has_trace(dispatch, "ProcessMonsters", "slave_death"));
    CHECK(has_trace(dispatch, "LegacyReward", "slave_no_drop"));
    auto slave = map.legacy_monster_snapshot(2);
    CHECK(slave.has_value());
    CHECK(slave->death_time_ms != 0);
    CHECK(slave->death_settled);
  }

  {
    auto map = make_map();
    map.enqueue_mail(make_slave(2, 999, 11, 10, 100000000));
    static_cast<void>(map.tick(1, 0));

    const auto dispatch = map.legacy_process_monster(2, 300, 1000, 0, 0);
    CHECK(has_trace(dispatch, "ProcessMonsters", "slave_death"));
    auto slave = map.legacy_monster_snapshot(2);
    CHECK(slave.has_value());
    CHECK(slave->death_settled);
  }

  {
    auto map = make_map();
    static_cast<void>(
        map.legacy_spawn_player(make_player(1, 104, make_character("Follower", 10, 10, 0)),
                                1, 0, true));
    map.enqueue_mail(make_slave(2, 1, 10, 13, 100000000));
    static_cast<void>(map.tick(1, 0));

    static_cast<void>(map.legacy_process_monster(2, 400, 1000, 0, 0));
    auto slave = map.legacy_monster_snapshot(2);
    CHECK(slave.has_value());
    CHECK(slave->x == 10);
    CHECK(slave->y == 12);
    CHECK(slave->target_x == 10);
    CHECK(slave->target_y == 11);
  }

  {
    auto map = make_map();
    static_cast<void>(
        map.legacy_spawn_player(make_player(1, 105, make_character("Relax", 10, 10, 0)),
                                1, 0, true));
    CHECK(map.legacy_set_player_slave_relax(1, true));
    map.enqueue_mail(make_slave(2, 1, 10, 13, 100000000));
    static_cast<void>(map.tick(1, 0));

    static_cast<void>(map.legacy_process_monster(2, 500, 1000, 0, 0));
    auto slave = map.legacy_monster_snapshot(2);
    CHECK(slave.has_value());
    CHECK(slave->x == 10);
    CHECK(slave->y == 13);
    CHECK(slave->target_x == -1);
    CHECK(slave->target_y == -1);
  }

  {
    auto map = make_map();
    static_cast<void>(
        map.legacy_spawn_player(make_player(1, 106, make_character("Recall", 10, 10, 0)),
                                1, 0, true));
    map.enqueue_mail(make_slave(2, 1, 35, 35, 100000000));
    static_cast<void>(map.tick(1, 0));

    static_cast<void>(map.legacy_process_monster(2, 600, 1000, 0, 0));
    auto slave = map.legacy_monster_snapshot(2);
    CHECK(slave.has_value());
    CHECK(std::abs(slave->x - 10) <= 3);
    CHECK(std::abs(slave->y - 10) <= 3);
  }

  return 0;
}
