#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/map_actor.hpp"

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

bool has_trace(const mir2::RuntimeDispatch& dispatch, const std::string& action) {
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == "MonsterCombat" && trace.action == action) {
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

std::optional<mir2::DecodedLegacyGamePacket> find_packet_for_session(
    const mir2::RuntimeDispatch& dispatch, std::uint16_t ident, std::uint64_t session_id) {
  for (const auto& event : dispatch.session_events) {
    if (event.session_id != session_id) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return decoded;
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

mir2::ActorMail make_monster(std::uint64_t actor_id, std::int32_t accuracy) {
  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::spawn_monster;
  mail.map_id = "0";
  mail.actor_id = actor_id;
  mail.name = "Oma";
  mail.x = 10;
  mail.y = 9;
  mail.max_hp = 20;
  mail.attack_power = 7;
  mail.dc_min = 2;
  mail.dc_max = 7;
  mail.exp_reward = 1;
  mail.accuracy = accuracy;
  mail.walk_speed_ms = 200;
  mail.attack_speed_ms = 200;
  mail.monster_ai_profile = mir2::MonsterAiProfile::aggressive;
  mail.monster_search_rate_ms = 1500;
  mail.dir = 4;
  mail.legacy_spawn_group = true;
  return mail;
}

mir2::ActorMail make_player(std::uint64_t actor_id, std::uint64_t session_id,
                            std::optional<mir2::LegacyUserItem> dress = std::nullopt) {
  mir2::CharacterRecord hero;
  hero.account_id = "guest";
  hero.character_name = "Hero";
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;
  hero.ability.level = 1;
  hero.ability.ac = mir2::make_word(1, 1);
  hero.ability.hp = 40;
  hero.ability.max_hp = 40;
  hero.ability.mp = 10;
  hero.ability.max_mp = 10;
  hero.ability.max_exp = 100;
  hero.ability.max_weight = 100;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;
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
  return mir2::MapActor(mir2::MapConfig{"0", "MonsterCombat", {}, 0, 0, 20, 20},
                        budgets, {}, {});
}

}  // namespace

int main() {
  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 100;
    constexpr std::uint64_t player_id = 1;
    map.enqueue_mail(make_monster(monster_id, 20));
    map.enqueue_mail(make_player(player_id, 10));
    static_cast<void>(map.tick(1, 0));

    const auto dispatch = map.legacy_process_monster(monster_id, 2, 1001, 0, 0);
    assert(find_packet_by_recog(dispatch, mir2::kSmHit,
                                static_cast<std::int32_t>(monster_id)).has_value());
    assert(find_packet_by_recog(dispatch, mir2::kSmStruck,
                                static_cast<std::int32_t>(player_id)).has_value());
    assert(has_trace(dispatch, "hit_check"));
    assert(has_trace(dispatch, "attack_power_roll"));
    assert(has_trace(dispatch, "armor_roll"));
    const auto damage_trace = find_trace(dispatch, "damage");
    assert(damage_trace.has_value());
    assert(damage_trace->value == 2);
    assert(damage_trace->damage == 1);

    const auto player = map.snapshot_player(player_id);
    assert(player.has_value());
    assert(player->ability.hp == 39);
  }

  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 102;
    constexpr std::uint64_t player_id = 3;
    constexpr std::uint64_t session_id = 30;
    map.enqueue_mail(make_monster(monster_id, 20));
    map.enqueue_mail(make_player(player_id, session_id,
                                 mir2::LegacyUserItem{3001, 1, 500, 1000}));
    static_cast<void>(map.tick(1, 0));

    const auto dispatch = map.legacy_process_monster(monster_id, 2, 1001, 0, 0);
    assert(find_packet_by_recog(dispatch, mir2::kSmStruck,
                                static_cast<std::int32_t>(player_id)).has_value());
    const auto update = find_packet_for_session(dispatch, mir2::kSmUpdateItem, session_id);
    assert(update.has_value());
    const auto item = decode_body<mir2::LegacyClientItem>(update->body);
    assert(item.has_value());
    assert(item->make_index == 3001);
    assert(item->dura < 500);
  }

  {
    auto map = make_map();
    constexpr std::uint64_t monster_id = 101;
    constexpr std::uint64_t player_id = 2;
    map.enqueue_mail(make_monster(monster_id, 0));
    map.enqueue_mail(make_player(player_id, 20));
    static_cast<void>(map.tick(1, 0));

    const auto dispatch = map.legacy_process_monster(monster_id, 2, 1001, 0, 0);
    assert(find_packet_by_recog(dispatch, mir2::kSmHit,
                                static_cast<std::int32_t>(monster_id)).has_value());
    assert(!find_packet_by_recog(dispatch, mir2::kSmStruck,
                                 static_cast<std::int32_t>(player_id)).has_value());
    assert(has_trace(dispatch, "miss"));

    const auto player = map.snapshot_player(player_id);
    assert(player.has_value());
    assert(player->ability.hp == 40);
  }

  return 0;
}
