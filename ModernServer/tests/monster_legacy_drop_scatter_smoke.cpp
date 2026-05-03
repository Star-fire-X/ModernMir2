#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/map_actor.hpp"

namespace {

std::optional<mir2::DecodedLegacyGamePacket> find_packet_by_body(
    const mir2::RuntimeDispatch& dispatch, std::uint16_t ident, const std::string& body) {
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident &&
        mir2::legacy_decode_string(decoded->body) == body) {
      return decoded;
    }
  }
  return std::nullopt;
}

bool has_packet(const mir2::RuntimeDispatch& dispatch, std::uint16_t ident) {
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return true;
    }
  }
  return false;
}

mir2::LegacyUserItem make_token(std::int32_t make_index) {
  mir2::LegacyUserItem item;
  item.index = 1;
  item.make_index = make_index;
  item.dura = 1;
  item.dura_max = 1;
  return item;
}

mir2::ActorMail make_player(std::uint64_t actor_id, std::uint64_t session_id, std::string name,
                            std::int32_t x, std::int32_t y, bool with_token) {
  mir2::CharacterRecord hero;
  hero.account_id = name;
  hero.character_name = std::move(name);
  hero.map_id = "0";
  hero.x = x;
  hero.y = y;
  hero.ability.level = 1;
  hero.ability.dc = mir2::make_word(40, 40);
  hero.ability.hp = 50;
  hero.ability.max_hp = 50;
  hero.ability.mp = 10;
  hero.ability.max_mp = 10;
  hero.ability.max_exp = 100;
  hero.ability.max_weight = 100;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;
  if (with_token) {
    hero.bag_items[0] = make_token(9001);
  }

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

mir2::ActorMail make_monster(std::uint64_t actor_id) {
  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::spawn_monster;
  mail.map_id = "0";
  mail.actor_id = actor_id;
  mail.name = "Oma";
  mail.x = 10;
  mail.y = 9;
  mail.max_hp = 1;
  mail.attack_power = 1;
  mail.dc_min = 1;
  mail.dc_max = 1;
  mail.accuracy = 20;
  mail.exp_reward = 1;
  mail.walk_speed_ms = 200;
  mail.attack_speed_ms = 200;
  mail.monster_ai_profile = mir2::MonsterAiProfile::basic;
  mail.monster_drop_items.push_back(make_token(7001));
  return mail;
}

mir2::ActorMail make_attack(std::uint64_t player_id, std::uint64_t session_id,
                            std::uint64_t monster_id) {
  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::attack;
  mail.actor_id = player_id;
  mail.session_id = session_id;
  mail.target_actor_id = monster_id;
  mail.x = 10;
  mail.y = 9;
  mail.game_message.ident = mir2::kCmHit;
  return mail;
}

mir2::ActorMail make_drop_token(std::uint64_t player_id, std::uint64_t session_id) {
  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::drop_item;
  mail.actor_id = player_id;
  mail.session_id = session_id;
  mail.item_make_index = 9001;
  mail.name = "Blocker";
  return mail;
}

mir2::MapActor make_map() {
  std::unordered_map<std::int32_t, mir2::ItemConfig> items;
  items.emplace(1, mir2::ItemConfig{1, "Token", 1, 1, 1, 0, 2, 1, -1, 0, 0});
  mir2::LogicBudgetConfig budgets;
  budgets.tick_ms = 20;
  return mir2::MapActor(mir2::MapConfig{"0", "Scatter", {}, 0, 0, 20, 20}, budgets,
                        std::move(items), {});
}

}  // namespace

int main() {
  {
    constexpr std::uint64_t monster_id = 300;
    constexpr std::uint64_t player_id = 1;
    constexpr std::uint64_t session_id = 10;

    auto map = make_map();
    map.enqueue_mail(make_monster(monster_id));
    static_cast<void>(map.tick(1, 0));
    static_cast<void>(map.legacy_spawn_player(
        make_player(player_id, session_id, "Hero", 10, 10, false), 1, 0, true));

    assert(map.enqueue_legacy_player_command(make_attack(player_id, session_id, monster_id), 20));
    const auto kill = map.legacy_process_player(player_id, 2, 20, false);
    assert(has_packet(kill, mir2::kSmDeath));
    const auto token = find_packet_by_body(kill, mir2::kSmItemShow, "Token");
    assert(token.has_value());
    assert(token->message.param == 9);
    assert(token->message.tag == 8);
  }

  {
    constexpr std::uint64_t monster_id = 400;
    constexpr std::uint64_t killer_id = 1;
    constexpr std::uint64_t blocker_id = 2;

    auto map = make_map();
    static_cast<void>(
        map.legacy_spawn_player(make_player(killer_id, 10, "Hero", 10, 10, false), 1, 0, true));
    static_cast<void>(
        map.legacy_spawn_player(make_player(blocker_id, 20, "Blocker", 9, 8, true), 1, 0, true));

    assert(map.enqueue_legacy_player_command(make_drop_token(blocker_id, 20), 10));
    const auto dropped = map.legacy_process_player(blocker_id, 2, 10, false);
    assert(find_packet_by_body(dropped, mir2::kSmItemShow, "Token").has_value());

    map.enqueue_mail(make_monster(monster_id));
    static_cast<void>(map.tick(3, 12));

    assert(map.enqueue_legacy_player_command(make_attack(killer_id, 10, monster_id), 20));
    const auto kill = map.legacy_process_player(killer_id, 4, 20, false);
    assert(has_packet(kill, mir2::kSmDeath));
    const auto token = find_packet_by_body(kill, mir2::kSmItemShow, "Token");
    assert(token.has_value());
    assert(token->message.param == 10);
    assert(token->message.tag == 8);
  }

  return 0;
}
