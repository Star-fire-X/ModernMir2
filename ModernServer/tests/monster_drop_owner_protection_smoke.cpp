#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
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

std::optional<mir2::DecodedLegacyGamePacket> find_packet(const mir2::RuntimeDispatch& dispatch,
                                                         std::uint16_t ident) {
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return decoded;
    }
  }
  return std::nullopt;
}

bool has_owner_reject(const mir2::RuntimeDispatch& dispatch, std::uint64_t owner_actor_id) {
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == "LegacyItem" && trace.action == "owner_reject" &&
        trace.value == static_cast<std::int32_t>(owner_actor_id)) {
      return true;
    }
  }
  return false;
}

mir2::LegacyUserItem make_token() {
  mir2::LegacyUserItem item;
  item.index = 1;
  item.make_index = 7001;
  item.dura = 1;
  item.dura_max = 1;
  return item;
}

mir2::ActorMail make_player(std::uint64_t actor_id, std::uint64_t session_id, std::string name,
                            std::int32_t x, std::int32_t y) {
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
  mail.monster_drop_items.push_back(make_token());
  return mail;
}

mir2::ActorMail make_attack(std::uint64_t killer_id, std::uint64_t session_id,
                            std::uint64_t monster_id) {
  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::attack;
  mail.actor_id = killer_id;
  mail.session_id = session_id;
  mail.target_actor_id = monster_id;
  mail.x = 10;
  mail.y = 9;
  mail.game_message.ident = mir2::kCmHit;
  return mail;
}

mir2::ActorMail make_pickup(std::uint64_t picker_id, std::uint64_t session_id) {
  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::pickup_item;
  mail.actor_id = picker_id;
  mail.session_id = session_id;
  mail.x = 9;
  mail.y = 8;
  return mail;
}

}  // namespace

int main() {
  constexpr std::uint64_t monster_id = 300;
  constexpr std::uint64_t killer_id = 1;
  constexpr std::uint64_t picker_id = 2;
  constexpr std::uint64_t killer_session = 10;
  constexpr std::uint64_t picker_session = 20;

  std::unordered_map<std::int32_t, mir2::ItemConfig> items;
  items.emplace(1, mir2::ItemConfig{1, "Token", 1, 1, 1, 0, 2, 1, -1, 0, 0});

  mir2::LogicBudgetConfig budgets;
  budgets.tick_ms = 20;
  mir2::MapActor map(mir2::MapConfig{"0", "OwnerDrop", {}, 20, 20, 0, 0}, budgets,
                     std::move(items), {});
  static_cast<void>(
      map.legacy_spawn_player(make_player(killer_id, killer_session, "Hero", 10, 10), 1, 0, true));
  static_cast<void>(map.legacy_spawn_player(
      make_player(picker_id, picker_session, "Picker", 9, 8), 1, 0, true));
  map.enqueue_mail(make_monster(monster_id));
  static_cast<void>(map.tick(1, 0));

  CHECK(map.enqueue_legacy_player_command(make_attack(killer_id, killer_session, monster_id), 20));
  const auto kill = map.legacy_process_player(killer_id, 2, 20, false);
  CHECK(find_packet(kill, mir2::kSmDeath).has_value());
  const auto show = find_packet(kill, mir2::kSmItemShow);
  CHECK(show.has_value());
  CHECK(show->message.param == 9);
  CHECK(show->message.tag == 8);

  CHECK(map.enqueue_legacy_player_command(make_pickup(picker_id, picker_session), 1000));
  const auto early_pickup = map.legacy_process_player(picker_id, 50, 1000, false);
  CHECK(has_owner_reject(early_pickup, killer_id));
  CHECK(!find_packet(early_pickup, mir2::kSmItemHide).has_value());
  CHECK(!find_packet(early_pickup, mir2::kSmAddItem).has_value());

  CHECK(map.enqueue_legacy_player_command(make_pickup(picker_id, picker_session), 120040));
  const auto boundary_pickup = map.legacy_process_player(picker_id, 6002, 120040, false);
  CHECK(has_owner_reject(boundary_pickup, killer_id));
  CHECK(!find_packet(boundary_pickup, mir2::kSmItemHide).has_value());

  CHECK(map.enqueue_legacy_player_command(make_pickup(picker_id, picker_session), 121000));
  const auto late_pickup = map.legacy_process_player(picker_id, 6050, 121000, false);
  CHECK(find_packet(late_pickup, mir2::kSmItemHide).has_value());
  CHECK(find_packet(late_pickup, mir2::kSmAddItem).has_value());

  return 0;
}
