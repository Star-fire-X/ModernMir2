#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/game_object.hpp"
#include "world/map_actor.hpp"

#undef assert
#define assert(expr)                                                           \
  do {                                                                         \
    if (!(expr)) {                                                             \
      std::cerr << "Assertion failed: " #expr ", file " << __FILE__           \
                << ", line " << __LINE__ << '\n';                             \
      std::exit(1);                                                            \
    }                                                                          \
  } while (false)

namespace {

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

std::optional<std::size_t> first_packet_index(const mir2::RuntimeDispatch& dispatch,
                                              std::uint16_t ident,
                                              std::int32_t recog) {
  for (std::size_t i = 0; i < dispatch.session_events.size(); ++i) {
    const auto decoded = mir2::decode_legacy_game_packet(dispatch.session_events[i].packet);
    if (decoded.has_value() && decoded->message.ident == ident &&
        decoded->message.recog == recog) {
      return i;
    }
  }
  return std::nullopt;
}

bool has_packet_for_session(const mir2::RuntimeDispatch& dispatch, std::uint16_t ident,
                            std::uint64_t session_id) {
  for (const auto& event : dispatch.session_events) {
    if (event.session_id != session_id) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return true;
    }
  }
  return false;
}

std::optional<mir2::LegacyRuntimeTrace> find_trace(const mir2::RuntimeDispatch& dispatch,
                                                   const std::string& action) {
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == "MonsterCombat" && trace.action == action) {
      return trace;
    }
  }
  return std::nullopt;
}

template <typename T>
std::optional<T> decode_body(std::string_view body) {
  T value{};
  if (!mir2::legacy_decode_buffer(body, &value, sizeof(value))) {
    return std::nullopt;
  }
  return value;
}

std::optional<mir2::LegacyClientItem> find_update_item(
    const mir2::RuntimeDispatch& dispatch, std::uint64_t session_id) {
  for (const auto& event : dispatch.session_events) {
    if (event.session_id != session_id) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (!decoded.has_value() || decoded->message.ident != mir2::kSmUpdateItem) {
      continue;
    }
    return decode_body<mir2::LegacyClientItem>(decoded->body);
  }
  return std::nullopt;
}

mir2::ActorMail make_monster(std::uint64_t actor_id, std::int32_t x, std::int32_t y,
                             std::uint64_t target_actor_id = 0,
                             std::int32_t accuracy = 20,
                             std::int32_t dc_min = 2,
                             std::int32_t dc_max = 7,
                             std::int32_t max_hp = 40,
                             std::int32_t walk_speed_ms = 200,
                             std::int32_t attack_speed_ms = 200,
                             std::uint64_t master_actor_id = 0) {
  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::spawn_monster;
  mail.map_id = "0";
  mail.actor_id = actor_id;
  mail.name = "Oma";
  mail.x = x;
  mail.y = y;
  mail.max_hp = max_hp;
  mail.attack_power = dc_max;
  mail.dc_min = dc_min;
  mail.dc_max = dc_max;
  mail.accuracy = accuracy;
  mail.exp_reward = 1;
  mail.walk_speed_ms = walk_speed_ms;
  mail.attack_speed_ms = attack_speed_ms;
  mail.monster_ai_profile = mir2::MonsterAiProfile::basic;
  mail.monster_search_rate_ms = 80;
  mail.dir = 4;
  mail.target_actor_id = target_actor_id;
  mail.master_actor_id = master_actor_id;
  mail.monster_is_slave = master_actor_id != 0;
  mail.legacy_spawn_group = true;
  return mail;
}

mir2::ActorMail make_player(std::uint64_t actor_id, std::uint64_t session_id,
                            const std::string& name, std::int32_t x, std::int32_t y,
                            std::int32_t hp = 40, std::int32_t ac_min = 1,
                            std::int32_t ac_max = 1,
                            std::optional<mir2::LegacyUserItem> dress = std::nullopt,
                            std::uint8_t attack_mode = 0) {
  mir2::CharacterRecord hero;
  hero.account_id = name;
  hero.character_name = name;
  hero.map_id = "0";
  hero.x = x;
  hero.y = y;
  hero.ability.level = 1;
  hero.ability.ac = mir2::make_word(static_cast<std::uint8_t>(ac_min),
                                    static_cast<std::uint8_t>(ac_max));
  hero.ability.hp = hp;
  hero.ability.max_hp = hp;
  hero.ability.mp = 10;
  hero.ability.max_mp = 10;
  hero.ability.max_exp = 100;
  hero.ability.max_weight = 100;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;
  hero.attack_mode = attack_mode;
  if (dress.has_value()) {
    hero.equipped_items[mir2::kEquipDress] = *dress;
  }

  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::spawn_player;
  mail.map_id = "0";
  mail.actor_id = actor_id;
  mail.session_id = session_id;
  mail.character = hero;
  mail.x = hero.x;
  mail.y = hero.y;
  return mail;
}

mir2::MapActor make_map() {
  mir2::LogicBudgetConfig budgets;
  budgets.tick_ms = 20;
  return mir2::MapActor(mir2::MapConfig{"0", "MonsterAttack", {}, 0, 0, 30, 30},
                        budgets, {}, {});
}

mir2::MapActor make_fight_map() {
  mir2::MapConfig config{"0", "MonsterAttackPk", {}, 0, 0, 30, 30};
  config.allow_pk = true;
  config.fight_zone = true;
  mir2::LogicBudgetConfig budgets;
  budgets.tick_ms = 20;
  return mir2::MapActor(config, budgets, {}, {});
}

void spawn(mir2::MapActor& map, const std::vector<mir2::ActorMail>& mails) {
  for (const auto& mail : mails) {
    map.enqueue_mail(mail);
  }
  static_cast<void>(map.tick(1, 0));
}

}  // namespace

int main() {
  {
    mir2::Monster monster(1, "Oma", "0", 0, 0, 1, 20, 7, 2, 7, 0, 0, 0, 0, 1,
                          0, 0, 0, 0, 0, 0, 0, 20, 200, 1, 0, 200);
    monster.mark_legacy_hit_time(1000);
    assert(!monster.legacy_attack_due_by_hit_time(1200));
    assert(monster.legacy_attack_due_by_hit_time(1201));
  }

  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 100;
    constexpr std::uint64_t player_id = 1;
    spawn(map, {make_monster(monster_id, 10, 9, player_id),
                make_player(player_id, 10, "Hero", 10, 10)});

    const auto dispatch = map.legacy_process_monster(monster_id, 2, 1001, 0, 0);
    const auto hit_index = first_packet_index(dispatch, mir2::kSmHit,
                                             static_cast<std::int32_t>(monster_id));
    const auto struck_index = first_packet_index(dispatch, mir2::kSmStruck,
                                                static_cast<std::int32_t>(player_id));
    assert(hit_index.has_value());
    assert(struck_index.has_value());
    assert(*hit_index < *struck_index);

    const auto damage_trace = find_trace(dispatch, "damage");
    assert(damage_trace.has_value());
    assert(damage_trace->value == 2);
    assert(damage_trace->damage == 1);

    const auto player = map.snapshot_player(player_id);
    assert(player.has_value());
    assert(player->ability.hp == 39);
    const auto monster = map.legacy_monster_snapshot(monster_id);
    assert(monster.has_value());
    assert(monster->hit_time_ms == 1001);
    assert(monster->target_focus_time_ms == 1001);
  }

  {
    auto map = make_fight_map();
    constexpr std::uint64_t owner_id = 11;
    constexpr std::uint64_t target_id = 12;
    constexpr std::uint64_t slave_id = 110;
    spawn(map, {make_player(owner_id, 1101, "Owner", 9, 10, 40, 1, 1, std::nullopt, 1),
                make_player(target_id, 1102, "Target", 10, 10),
                make_monster(slave_id, 10, 9, target_id, 20, 2, 7, 40, 200, 200,
                             owner_id)});

    const auto dispatch = map.legacy_process_monster(slave_id, 2, 1001, 0, 0);
    assert(find_trace(dispatch, "pk_block").has_value());
    assert(!find_packet_by_recog(dispatch, mir2::kSmHit,
                                 static_cast<std::int32_t>(slave_id)).has_value());
    assert(!find_packet_by_recog(dispatch, mir2::kSmStruck,
                                 static_cast<std::int32_t>(target_id)).has_value());
  }

  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 110;
    constexpr std::uint64_t player_id = 2;
    spawn(map, {make_monster(monster_id, 10, 8, player_id),
                make_player(player_id, 20, "FarHero", 10, 10)});
    const auto before = map.legacy_monster_snapshot(monster_id);
    assert(before.has_value());

    const auto dispatch = map.legacy_process_monster(monster_id, 2, 1001, 0, 0);
    assert(!find_packet_by_recog(dispatch, mir2::kSmHit,
                                 static_cast<std::int32_t>(monster_id)).has_value());
    const auto after = map.legacy_monster_snapshot(monster_id);
    assert(after.has_value());
    assert(after->hit_time_ms == before->hit_time_ms);
    assert(after->target_x == 10);
    assert(after->target_y == 10);
  }

  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 120;
    constexpr std::uint64_t player_id = 3;
    spawn(map, {make_monster(monster_id, 10, 9, player_id, 20, 2, 7, 40, 200, 500),
                make_player(player_id, 30, "CooldownHero", 10, 10)});

    static_cast<void>(map.legacy_process_monster(monster_id, 2, 1001, 0, 0));
    const auto after_hit = map.legacy_monster_snapshot(monster_id);
    assert(after_hit.has_value());
    assert(after_hit->hit_time_ms == 1001);

    const auto cooldown = map.legacy_process_monster(monster_id, 3, 1501, 0, 0);
    assert(!find_packet_by_recog(cooldown, mir2::kSmHit,
                                 static_cast<std::int32_t>(monster_id)).has_value());
    assert(!find_packet_by_recog(cooldown, mir2::kSmWalk,
                                 static_cast<std::int32_t>(monster_id)).has_value());
    const auto after_cooldown = map.legacy_monster_snapshot(monster_id);
    assert(after_cooldown.has_value());
    assert(after_cooldown->hit_time_ms == after_hit->hit_time_ms);
    assert(after_cooldown->x == after_hit->x);
    assert(after_cooldown->y == after_hit->y);
  }

  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 130;
    constexpr std::uint64_t player_id = 4;
    constexpr std::uint64_t session_id = 40;
    const mir2::LegacyUserItem dress{3001, 1, 500, 1000};
    spawn(map, {make_monster(monster_id, 10, 9, player_id, 0),
                make_player(player_id, session_id, "MissHero", 10, 10, 40, 1, 1, dress)});

    const auto dispatch = map.legacy_process_monster(monster_id, 2, 1001, 0, 0);
    assert(find_packet_by_recog(dispatch, mir2::kSmHit,
                                static_cast<std::int32_t>(monster_id)).has_value());
    assert(!find_packet_by_recog(dispatch, mir2::kSmStruck,
                                 static_cast<std::int32_t>(player_id)).has_value());
    assert(find_trace(dispatch, "miss").has_value());
    assert(!has_packet_for_session(dispatch, mir2::kSmUpdateItem, session_id));
    const auto player = map.snapshot_player(player_id);
    assert(player.has_value());
    assert(player->ability.hp == 40);
  }

  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 140;
    constexpr std::uint64_t player_id = 5;
    constexpr std::uint64_t session_id = 50;
    const mir2::LegacyUserItem dress{3002, 1, 500, 1000};
    spawn(map, {make_monster(monster_id, 10, 9, player_id),
                make_player(player_id, session_id, "ArmorHero", 10, 10, 40, 1, 1, dress)});

    const auto dispatch = map.legacy_process_monster(monster_id, 2, 1001, 0, 0);
    assert(find_packet_by_recog(dispatch, mir2::kSmStruck,
                                static_cast<std::int32_t>(player_id)).has_value());
    const auto item = find_update_item(dispatch, session_id);
    assert(item.has_value());
    assert(item->make_index == 3002);
    assert(item->dura < 500);
  }

  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 150;
    constexpr std::uint64_t master_id = 6;
    constexpr std::uint64_t slave_id = 151;
    spawn(map, {make_player(master_id, 60, "Master", 11, 10),
                make_monster(slave_id, 10, 10, 0, 20, 1, 1, 40, 200, 200, master_id),
                make_monster(monster_id, 10, 9, slave_id)});

    const auto dispatch = map.legacy_process_monster(monster_id, 2, 1001, 0, 0);
    const auto hit_index = first_packet_index(dispatch, mir2::kSmHit,
                                             static_cast<std::int32_t>(monster_id));
    const auto struck_index = first_packet_index(dispatch, mir2::kSmStruck,
                                                static_cast<std::int32_t>(slave_id));
    assert(hit_index.has_value());
    assert(struck_index.has_value());
    assert(*hit_index < *struck_index);
    const auto slave = map.legacy_monster_snapshot(slave_id);
    assert(slave.has_value());
    assert(slave->hp < slave->max_hp);
  }

  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 160;
    constexpr std::uint64_t player_id = 7;
    constexpr std::uint64_t observer_id = 8;
    spawn(map, {make_monster(monster_id, 10, 9, player_id, 20, 20, 20),
                make_player(player_id, 70, "FragileHero", 10, 10, 5, 0, 0),
                make_player(observer_id, 80, "Observer", 11, 10)});

    const auto dispatch = map.legacy_process_monster(monster_id, 2, 1001, 0, 0);
    const auto hit_index = first_packet_index(dispatch, mir2::kSmHit,
                                             static_cast<std::int32_t>(monster_id));
    const auto death_index = first_packet_index(dispatch, mir2::kSmDeath,
                                               static_cast<std::int32_t>(player_id));
    assert(hit_index.has_value());
    assert(death_index.has_value());
    assert(*hit_index < *death_index);
  }

  return 0;
}
