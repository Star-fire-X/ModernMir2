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

bool has_slave_exp_trace(const mir2::RuntimeDispatch& dispatch) {
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == "LegacyReward" && trace.action == "slave_exp") {
      return true;
    }
  }
  return false;
}

mir2::LegacyUserItem make_token() {
  mir2::LegacyUserItem item;
  item.index = 1;
  item.make_index = 8101;
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
  hero.ability.level = 10;
  hero.ability.dc = mir2::make_word(1, 1);
  hero.ability.hp = 50;
  hero.ability.max_hp = 50;
  hero.ability.mp = 10;
  hero.ability.max_mp = 10;
  hero.ability.max_exp = 1000;
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
  mail.level = 2;
  mail.max_hp = 5;
  mail.attack_power = 1;
  mail.dc_min = 1;
  mail.dc_max = 1;
  mail.accuracy = 1;
  mail.exp_reward = 10;
  mail.walk_speed_ms = 200;
  mail.attack_speed_ms = 200;
  mail.monster_ai_profile = mir2::MonsterAiProfile::basic;
  mail.monster_drop_items.push_back(make_token());
  return mail;
}

mir2::ActorMail make_slave(std::uint64_t actor_id, std::uint64_t master_id,
                           std::uint64_t target_id) {
  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::spawn_monster;
  mail.map_id = "0";
  mail.actor_id = actor_id;
  mail.name = "__WhiteSkeleton";
  mail.x = 10;
  mail.y = 10;
  mail.level = 1;
  mail.max_hp = 40;
  mail.attack_power = 50;
  mail.dc_min = 50;
  mail.dc_max = 50;
  mail.accuracy = 100;
  mail.exp_reward = 1;
  mail.walk_speed_ms = 1;
  mail.attack_speed_ms = 1;
  mail.monster_ai_profile = mir2::MonsterAiProfile::aggressive;
  mail.master_actor_id = master_id;
  mail.monster_is_slave = true;
  mail.monster_no_item = true;
  mail.slave_make_level = 1;
  mail.slave_exp_level = 0;
  mail.master_royalty_time_ms = 10 * 60 * 1000;
  mail.target_actor_id = target_id;
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
  constexpr std::uint64_t master_id = 1;
  constexpr std::uint64_t picker_id = 2;
  constexpr std::uint64_t slave_id = 3;
  constexpr std::uint64_t monster_id = 4;

  std::unordered_map<std::int32_t, mir2::ItemConfig> items;
  items.emplace(1, mir2::ItemConfig{1, "Token", 1, 1, 1, 0, 2, 1, -1, 0, 0});

  mir2::LogicBudgetConfig budgets;
  budgets.tick_ms = 20;
  mir2::MapActor map(mir2::MapConfig{"0", "SlaveOwner", {}, 0, 0, 20, 20}, budgets,
                     std::move(items), {});
  static_cast<void>(
      map.legacy_spawn_player(make_player(master_id, 10, "Master", 10, 11), 1, 0, true));
  static_cast<void>(
      map.legacy_spawn_player(make_player(picker_id, 20, "Picker", 9, 8), 1, 0, true));
  map.enqueue_mail(make_monster(monster_id));
  map.enqueue_mail(make_slave(slave_id, master_id, monster_id));
  static_cast<void>(map.tick(1, 0));

  const auto kill = map.legacy_process_monster(slave_id, 2, 1000, 0, 0);
  assert(find_packet(kill, mir2::kSmDeath).has_value());
  assert(has_slave_exp_trace(kill));
  assert(find_packet(kill, mir2::kSmItemShow).has_value());

  assert(map.enqueue_legacy_player_command(make_pickup(picker_id, 20), 1020));
  const auto early_pickup = map.legacy_process_player(picker_id, 3, 1020, false);
  assert(has_owner_reject(early_pickup, master_id));
  assert(!find_packet(early_pickup, mir2::kSmAddItem).has_value());

  return 0;
}
