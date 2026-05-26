#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/map_actor.hpp"

namespace {

void check(bool condition, const char* expression, const char* file, int line) {
  if (condition) {
    return;
  }
  std::cerr << "check failed: " << expression << " at " << file << ":" << line << "\n";
  std::exit(1);
}

#define CHECK(expr) check((expr), #expr, __FILE__, __LINE__)

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
                             std::int32_t race_server = 0) {
  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::spawn_monster;
  mail.map_id = "0";
  mail.actor_id = actor_id;
  mail.name = "Monster";
  mail.x = x;
  mail.y = y;
  mail.max_hp = 20;
  mail.attack_power = 3;
  mail.dc_min = 3;
  mail.dc_max = 3;
  mail.exp_reward = 1;
  mail.walk_speed_ms = 200;
  mail.walk_step = 1;
  mail.attack_speed_ms = 40;
  mail.monster_ai_profile = profile;
  mail.race_server = race_server;
  mail.monster_search_rate_ms = 80;
  mail.legacy_spawn_group = true;
  return mail;
}

mir2::ActorMail make_player(std::uint64_t actor_id, std::uint64_t session_id,
                            std::string name, std::int32_t x, std::int32_t y) {
  mir2::CharacterRecord hero;
  hero.account_id = name;
  hero.character_name = std::move(name);
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
  return mir2::MapActor(mir2::MapConfig{"0", "RaceAI", {}, 0, 0, 30, 30},
                        budgets, {}, {});
}

void spawn(mir2::MapActor& map, const mir2::ActorMail& monster,
           const mir2::ActorMail& player) {
  map.enqueue_mail(monster);
  map.enqueue_mail(player);
  static_cast<void>(map.tick(1, 0));
}

}  // namespace

int main() {
  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 100;
    spawn(map, make_monster(monster_id, 10, 8, mir2::MonsterAiProfile::passive_animal),
          make_player(1, 10, "Hero", 10, 10));

    const auto dispatch = map.legacy_process_monster(monster_id, 2, 1001, 0, 0);
    CHECK(!find_packet_by_recog(dispatch, mir2::kSmHit,
                                static_cast<std::int32_t>(monster_id)).has_value());
    CHECK(!find_packet_by_recog(dispatch, mir2::kSmStruck,
                                static_cast<std::int32_t>(monster_id)).has_value());
  }

  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 101;
    map.enqueue_mail(make_monster(monster_id, 10, 8, mir2::MonsterAiProfile::basic, 81));
    map.enqueue_mail(make_player(2, 20, "FarHero", 10, 11));
    map.enqueue_mail(make_player(3, 30, "NearHero", 8, 8));
    static_cast<void>(map.tick(1, 0));

    const auto dispatch = map.legacy_process_monster(monster_id, 2, 1001, 0, 0);
    const auto walk = find_packet_by_recog(dispatch, mir2::kSmWalk,
                                           static_cast<std::int32_t>(monster_id));
    CHECK(walk.has_value());
    const auto snapshot = map.legacy_monster_snapshot(monster_id);
    CHECK(snapshot.has_value());
    CHECK(snapshot->target_actor_id == 3);
  }

  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 102;
    spawn(map, make_monster(monster_id, 10, 8, mir2::MonsterAiProfile::stationary),
          make_player(4, 40, "Hero", 10, 10));

    const auto dispatch = map.legacy_process_monster(monster_id, 2, 1001, 0, 0);
    CHECK(find_packet_by_recog(dispatch, mir2::kSmHit,
                               static_cast<std::int32_t>(monster_id)).has_value());
    CHECK(!find_packet_by_recog(dispatch, mir2::kSmWalk,
                                static_cast<std::int32_t>(monster_id)).has_value());
  }

  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 103;
    spawn(map, make_monster(monster_id, 10, 6, mir2::MonsterAiProfile::ranged),
          make_player(5, 50, "Hero", 10, 10));

    const auto dispatch = map.legacy_process_monster(monster_id, 2, 1001, 0, 0);
    CHECK(find_packet_by_recog(dispatch, mir2::kSmHit,
                               static_cast<std::int32_t>(monster_id)).has_value());
    CHECK(!find_packet_by_recog(dispatch, mir2::kSmWalk,
                                static_cast<std::int32_t>(monster_id)).has_value());
  }

  return 0;
}
