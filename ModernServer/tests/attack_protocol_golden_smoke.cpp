#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/map_actor.hpp"

namespace {

constexpr std::int32_t kMainStruckDelayMs = 200;

struct ObservedPacket {
  std::size_t index{0};
  std::int32_t delay_ms{0};
  mir2::DecodedLegacyGamePacket decoded{};
};

template <typename T>
std::optional<T> decode_body(std::string_view body) {
  T value{};
  if (!mir2::legacy_decode_buffer(body, &value, sizeof(value))) {
    return std::nullopt;
  }
  return value;
}

std::vector<ObservedPacket> packets_for(const mir2::RuntimeDispatch& dispatch,
                                        std::uint64_t session_id,
                                        std::uint16_t ident) {
  std::vector<ObservedPacket> packets;
  for (std::size_t index = 0; index < dispatch.session_events.size(); ++index) {
    const auto& event = dispatch.session_events[index];
    if (event.session_id != session_id) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      packets.push_back(ObservedPacket{index, event.delay_ms, *decoded});
    }
  }
  return packets;
}

std::optional<ObservedPacket> packet_for_recog(const mir2::RuntimeDispatch& dispatch,
                                               std::uint64_t session_id,
                                               std::uint16_t ident,
                                               std::int32_t recog) {
  const auto packets = packets_for(dispatch, session_id, ident);
  const auto it = std::find_if(packets.begin(), packets.end(), [&](const ObservedPacket& packet) {
    return packet.decoded.message.recog == recog;
  });
  if (it == packets.end()) {
    return std::nullopt;
  }
  return *it;
}

bool has_trace(const mir2::RuntimeDispatch& dispatch, std::string_view stage,
               std::string_view action) {
  return std::any_of(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                     [&](const mir2::LegacyRuntimeTrace& trace) {
                       return trace.stage == stage && trace.action == action;
                     });
}

const mir2::LegacyRuntimeTrace* find_trace(const mir2::RuntimeDispatch& dispatch,
                                           std::string_view stage,
                                           std::string_view action) {
  const auto it = std::find_if(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                               [&](const mir2::LegacyRuntimeTrace& trace) {
                                 return trace.stage == stage && trace.action == action;
                               });
  return it == dispatch.legacy_traces.end() ? nullptr : &*it;
}

mir2::ActorMail make_monster(std::uint64_t actor_id, std::int32_t hp = 20,
                             std::int32_t speed = 10) {
  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::spawn_monster;
  mail.map_id = "0";
  mail.actor_id = actor_id;
  mail.name = "Oma";
  mail.x = 10;
  mail.y = 9;
  mail.max_hp = hp;
  mail.current_hp = hp;
  mail.attack_power = 3;
  mail.dc_min = 1;
  mail.dc_max = 1;
  mail.accuracy = 20;
  mail.speed = speed;
  mail.exp_reward = 1;
  mail.walk_speed_ms = 200;
  mail.attack_speed_ms = 200;
  mail.monster_ai_profile = mir2::MonsterAiProfile::basic;
  mail.monster_search_rate_ms = 3000;
  mail.dir = 4;
  mail.legacy_spawn_group = true;
  return mail;
}

mir2::ActorMail make_player(std::uint64_t actor_id, std::uint64_t session_id,
                            std::string name, std::int32_t x, std::int32_t y,
                            std::int32_t dc, std::int32_t hp = 40,
                            std::int32_t accuracy = 10, std::int32_t speed = 10) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = x;
  character.y = y;
  character.dir = 0;
  character.attack_mode = 0;
  character.ability.level = 20;
  character.ability.dc = mir2::make_word(dc, dc);
  character.ability.hp = hp;
  character.ability.max_hp = hp;
  character.ability.mp = 10;
  character.ability.max_mp = 10;
  character.ability.max_exp = 100;
  character.ability.max_weight = 100;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  character.ability.reserved1 = accuracy;
  character.ability.exp_count = speed;

  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::spawn_player;
  mail.map_id = "0";
  mail.actor_id = actor_id;
  mail.session_id = session_id;
  mail.character = character;
  mail.x = x;
  mail.y = y;
  return mail;
}

mir2::ActorMail make_attack(std::uint64_t actor_id, std::uint64_t session_id,
                            std::uint64_t target_actor_id) {
  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::attack;
  mail.map_id = "0";
  mail.actor_id = actor_id;
  mail.session_id = session_id;
  mail.target_actor_id = target_actor_id;
  mail.x = 10;
  mail.y = 10;
  mail.dir = 0;
  mail.game_message.ident = mir2::kCmHit;
  return mail;
}

mir2::MapActor make_map(bool fight_zone = false) {
  mir2::MapConfig map_config;
  map_config.id = "0";
  map_config.title = "AttackProtocol";
  map_config.width = 20;
  map_config.height = 20;
  map_config.fight_zone = fight_zone;

  mir2::LogicBudgetConfig budgets;
  budgets.tick_ms = 20;
  return mir2::MapActor(map_config, budgets, {}, {});
}

void assert_hit_before_struck(const mir2::RuntimeDispatch& dispatch, std::uint64_t session_id,
                              std::int32_t attacker_id, std::int32_t target_id) {
  const auto hit = packet_for_recog(dispatch, session_id, mir2::kSmHit, attacker_id);
  const auto struck = packet_for_recog(dispatch, session_id, mir2::kSmStruck, target_id);
  assert(hit.has_value());
  assert(struck.has_value());
  assert(hit->index < struck->index);
  assert(hit->delay_ms == 0);
  assert(struck->delay_ms == kMainStruckDelayMs);
}

}  // namespace

int main() {
  {
    constexpr std::uint64_t monster_id = 200;
    constexpr std::uint64_t player_id = 1;
    constexpr std::uint64_t session_id = 10;

    auto map = make_map();
    map.enqueue_mail(make_monster(monster_id));
    static_cast<void>(map.tick(1, 0));
    static_cast<void>(map.legacy_spawn_player(
        make_player(player_id, session_id, "Hero", 10, 10, 8), 1, 0, true));

    mir2::LegacyRandom random(1);
    map.set_legacy_random(&random);
    assert(map.enqueue_legacy_player_command(make_attack(player_id, session_id, monster_id), 20));
    const auto dispatch = map.legacy_process_player(player_id, 2, 20, false);

    assert_hit_before_struck(dispatch, session_id, static_cast<std::int32_t>(player_id),
                             static_cast<std::int32_t>(monster_id));
    const auto hit = packet_for_recog(dispatch, session_id, mir2::kSmHit,
                                      static_cast<std::int32_t>(player_id));
    assert(hit->decoded.message.param == 10);
    assert(hit->decoded.message.tag == 10);
    assert(hit->decoded.message.series == 0);

    const auto struck = packet_for_recog(dispatch, session_id, mir2::kSmStruck,
                                         static_cast<std::int32_t>(monster_id));
    assert(struck->decoded.message.series > 0);
    const auto struck_body = decode_body<mir2::LegacyMessageBodyWL>(struck->decoded.body);
    assert(struck_body.has_value());
    assert(struck_body->ltag1 == static_cast<std::int32_t>(player_id));
    assert(struck_body->ltag2 == 0);

    const auto monster = map.legacy_monster_snapshot(monster_id);
    assert(monster.has_value());
    assert(monster->last_hitter_id == player_id);
    assert(monster->exp_hitter_id == player_id);
    assert(monster->target_actor_id == player_id);
  }

  {
    constexpr std::uint64_t monster_id = 300;
    constexpr std::uint64_t player_id = 2;
    constexpr std::uint64_t session_id = 20;

    auto map = make_map();
    map.enqueue_mail(make_monster(monster_id));
    static_cast<void>(map.tick(1, 0));
    static_cast<void>(map.legacy_spawn_player(
        make_player(player_id, session_id, "LowAccuracy", 10, 10, 8, 40, 1), 1, 0, true));

    mir2::LegacyRandom random(4);
    map.set_legacy_random(&random);
    assert(map.enqueue_legacy_player_command(make_attack(player_id, session_id, monster_id), 20));
    const auto dispatch = map.legacy_process_player(player_id, 2, 20, false);

    assert(packet_for_recog(dispatch, session_id, mir2::kSmHit,
                            static_cast<std::int32_t>(player_id)).has_value());
    assert(!packet_for_recog(dispatch, session_id, mir2::kSmStruck,
                             static_cast<std::int32_t>(monster_id)).has_value());
    assert(!packet_for_recog(dispatch, session_id, mir2::kSmDeath,
                             static_cast<std::int32_t>(monster_id)).has_value());
    assert(has_trace(dispatch, "LegacyCombat", "miss"));

    const auto monster = map.legacy_monster_snapshot(monster_id);
    assert(monster.has_value());
    assert(monster->hp == monster->max_hp);
    assert(monster->last_hitter_id == 0);
    assert(monster->exp_hitter_id == 0);
    assert(monster->target_actor_id == 0);
  }

  {
    constexpr std::uint64_t attacker_id = 3;
    constexpr std::uint64_t target_id = 4;
    constexpr std::uint64_t attacker_session_id = 30;
    constexpr std::uint64_t target_session_id = 40;

    auto map = make_map(true);
    static_cast<void>(map.legacy_spawn_player(
        make_player(attacker_id, attacker_session_id, "Attacker", 10, 10, 8), 1, 0, true));
    static_cast<void>(map.legacy_spawn_player(
        make_player(target_id, target_session_id, "Target", 10, 9, 1), 1, 0, true));

    mir2::LegacyRandom random(1);
    map.set_legacy_random(&random);
    assert(map.enqueue_legacy_player_command(
        make_attack(attacker_id, attacker_session_id, target_id), 20));
    const auto dispatch = map.legacy_process_player(attacker_id, 2, 20, false);

    assert_hit_before_struck(dispatch, target_session_id, static_cast<std::int32_t>(attacker_id),
                             static_cast<std::int32_t>(target_id));
    const auto struck = packet_for_recog(dispatch, target_session_id, mir2::kSmStruck,
                                         static_cast<std::int32_t>(target_id));
    const auto struck_body = decode_body<mir2::LegacyMessageBodyWL>(struck->decoded.body);
    assert(struck_body.has_value());
    assert(struck_body->ltag1 == static_cast<std::int32_t>(attacker_id));
    assert(struck_body->ltag2 == 0);
  }

  {
    constexpr std::uint64_t monster_id = 400;
    constexpr std::uint64_t player_id = 5;
    constexpr std::uint64_t session_id = 50;

    auto map = make_map();
    map.enqueue_mail(make_monster(monster_id, 20, 320));
    static_cast<void>(map.tick(1, 0));
    static_cast<void>(map.legacy_spawn_player(
        make_player(player_id, session_id, "AccuracyEqualsRoll", 10, 10, 8, 40, 10),
        1, 0, true));

    mir2::LegacyRandom random(1);
    map.set_legacy_random(&random);
    assert(map.enqueue_legacy_player_command(make_attack(player_id, session_id, monster_id), 20));
    const auto dispatch = map.legacy_process_player(player_id, 2, 20, false);

    assert(packet_for_recog(dispatch, session_id, mir2::kSmHit,
                            static_cast<std::int32_t>(player_id)).has_value());
    assert(!packet_for_recog(dispatch, session_id, mir2::kSmStruck,
                             static_cast<std::int32_t>(monster_id)).has_value());
    assert(has_trace(dispatch, "LegacyCombat", "miss"));
  }

  {
    constexpr std::uint64_t monster_id = 500;
    constexpr std::uint64_t player_id = 6;
    constexpr std::uint64_t session_id = 60;

    auto map = make_map();
    map.enqueue_mail(make_monster(monster_id, 20, 0));
    static_cast<void>(map.tick(1, 0));
    static_cast<void>(map.legacy_spawn_player(
        make_player(player_id, session_id, "ZeroSpeedTarget", 10, 10, 8, 40, 1), 1,
        0, true));

    mir2::LegacyRandom random(1);
    map.set_legacy_random(&random);
    assert(map.enqueue_legacy_player_command(make_attack(player_id, session_id, monster_id), 20));
    const auto dispatch = map.legacy_process_player(player_id, 2, 20, false);

    assert_hit_before_struck(dispatch, session_id, static_cast<std::int32_t>(player_id),
                             static_cast<std::int32_t>(monster_id));
    const auto* hit_check = find_trace(dispatch, "LegacyCombat", "hit_check");
    assert(hit_check != nullptr);
    assert(hit_check->label == "range=0");
    assert(hit_check->value == 0);
    assert(hit_check->rng_before == hit_check->rng_after);
  }

  return 0;
}
