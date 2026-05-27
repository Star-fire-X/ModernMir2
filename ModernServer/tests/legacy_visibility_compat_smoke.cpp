#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

std::optional<mir2::DecodedLegacyGamePacket> find_packet(const mir2::RuntimeDispatch& dispatch,
                                                         std::uint64_t session_id,
                                                         std::uint16_t ident,
                                                         std::optional<std::int32_t> recog = std::nullopt) {
  for (const auto& event : dispatch.session_events) {
    if (event.session_id != session_id) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (!decoded.has_value() || decoded->message.ident != ident) {
      continue;
    }
    if (recog.has_value() && decoded->message.recog != *recog) {
      continue;
    }
    return decoded;
  }
  return std::nullopt;
}

std::optional<std::size_t> packet_index(const mir2::RuntimeDispatch& dispatch,
                                        std::uint64_t session_id, std::uint16_t ident) {
  for (std::size_t index = 0; index < dispatch.session_events.size(); ++index) {
    if (dispatch.session_events[index].session_id != session_id) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(dispatch.session_events[index].packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return index;
    }
  }
  return std::nullopt;
}

mir2::CharacterRecord make_character(std::string name, std::int32_t x, std::int32_t y) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = x;
  character.y = y;
  character.ability.level = 20;
  character.ability.hp = 40;
  character.ability.max_hp = 40;
  character.ability.mp = 12;
  character.ability.max_mp = 12;
  character.ability.max_weight = 50;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  character.attack_mode = 0;
  return character;
}

mir2::CharacterRecord make_attacker(std::string name, std::int32_t x, std::int32_t y) {
  auto character = make_character(std::move(name), x, y);
  character.ability.dc = mir2::make_word(30, 30);
  return character;
}

mir2::CharacterRecord make_dropper(std::string name, std::int32_t x, std::int32_t y,
                                   std::int32_t make_index) {
  auto character = make_character(std::move(name), x, y);
  character.bag_items[0].index = 1;
  character.bag_items[0].make_index = make_index;
  character.bag_items[0].dura = 600;
  character.bag_items[0].dura_max = 1000;
  return character;
}

mir2::RuntimeDispatch enter(mir2::LogicRuntime& runtime, std::uint64_t session_id,
                            mir2::CharacterRecord character) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = session_id;
  command.account_id = character.account_id;
  command.character_name = character.character_name;
  command.map_id = character.map_id;
  command.x = character.x;
  command.y = character.y;
  command.character = std::move(character);
  static_cast<void>(runtime.route_logic_command(command));
  return runtime.tick();
}

void advance(mir2::LogicRuntime& runtime, int ticks = 13) {
  for (int index = 0; index < ticks; ++index) {
    static_cast<void>(runtime.tick());
  }
}

mir2::RuntimeDispatch turn(mir2::LogicRuntime& runtime, std::uint64_t session_id, std::uint8_t dir) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::turn;
  command.session_id = session_id;
  command.dir = dir;
  static_cast<void>(runtime.route_logic_command(command));
  return runtime.tick();
}

mir2::RuntimeDispatch walk(mir2::LogicRuntime& runtime, std::uint64_t session_id,
                           std::int32_t x, std::int32_t y) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::walk;
  command.session_id = session_id;
  command.x = x;
  command.y = y;
  static_cast<void>(runtime.route_logic_command(command));
  return runtime.tick();
}

mir2::RuntimeDispatch drop_item(mir2::LogicRuntime& runtime, std::uint64_t session_id,
                                std::int32_t make_index, std::string name) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::drop_item;
  command.session_id = session_id;
  command.item_make_index = make_index;
  command.text = std::move(name);
  static_cast<void>(runtime.route_logic_command(command));
  return runtime.tick();
}

mir2::RuntimeDispatch attack(mir2::LogicRuntime& runtime, std::uint64_t session_id,
                             std::uint64_t target_actor_id,
                             std::int32_t x, std::int32_t y, std::uint8_t dir) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::attack;
  command.session_id = session_id;
  command.target_actor_id = target_actor_id;
  command.x = x;
  command.y = y;
  command.dir = dir;
  command.game_message.ident = mir2::kCmHit;
  static_cast<void>(runtime.route_logic_command(command));
  return runtime.tick();
}

void assert_ref_broadcast_cache_boundary() {
  mir2::HostConfig config;
  config.budgets.tick_ms = 20;
  config.maps.push_back(mir2::MapConfig{"0", "RefCacheMap", {}, 40, 20, 10, 10});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  static_cast<void>(enter(runtime, 1, make_character("Broadcaster", 10, 10)));
  static_cast<void>(enter(runtime, 2, make_character("EdgeWatcher", 22, 10)));

  advance(runtime);
  auto dispatch = turn(runtime, 1, 2);
  assert(find_packet(dispatch, 2, mir2::kSmTurn).has_value());

  advance(runtime);
  dispatch = turn(runtime, 1, 4);
  assert(!find_packet(dispatch, 2, mir2::kSmTurn).has_value());
}

void assert_dead_actor_enters_as_death() {
  mir2::HostConfig config;
  config.budgets.tick_ms = 20;
  config.maps.push_back(mir2::MapConfig{"0", "DeadShowMap", {}, 30, 30, 10, 10});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  static_cast<void>(enter(runtime, 11, make_attacker("Killer", 10, 10)));
  const auto victim_enter = enter(runtime, 12, make_character("Victim", 10, 11));

  const auto victim_login = find_packet(victim_enter, 12, mir2::kSmNewMap);
  assert(victim_login.has_value());
  const auto victim_actor_id =
      static_cast<std::uint64_t>(static_cast<std::uint32_t>(victim_login->message.recog));

  advance(runtime);
  const auto death = attack(runtime, 11, victim_actor_id, 10, 11, 4);
  assert(find_packet(death, 12, mir2::kSmNowDeath).has_value());

  advance(runtime);
  const auto late_login = enter(runtime, 13, make_character("LateWatcher", 10, 10));
  assert(find_packet(late_login, 13, mir2::kSmDeath,
                     static_cast<std::int32_t>(victim_actor_id)).has_value());
  assert(!find_packet(late_login, 13, mir2::kSmTurn,
                      static_cast<std::int32_t>(victim_actor_id)).has_value());
}

void assert_stale_item_hide_before_new_show() {
  mir2::HostConfig config;
  config.budgets.tick_ms = 20;
  config.maps.push_back(mir2::MapConfig{"0", "ItemDeltaMap", {}, 40, 40, 20, 20});
  config.items.push_back(mir2::ItemConfig{1, "Wooden Sword", 3, 100, 5, 1, 1, 1000, 1, 0, 0});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  static_cast<void>(enter(runtime, 21, make_dropper("LeftDropper", 8, 20, 1001)));
  static_cast<void>(enter(runtime, 22, make_dropper("RightDropper", 33, 20, 1002)));
  static_cast<void>(enter(runtime, 23, make_character("Watcher", 20, 20)));

  advance(runtime);
  static_cast<void>(drop_item(runtime, 21, 1001, "Wooden Sword"));
  advance(runtime);
  static_cast<void>(drop_item(runtime, 22, 1002, "Wooden Sword"));

  advance(runtime);
  const auto dispatch = walk(runtime, 23, 21, 20);
  const auto hide_index = packet_index(dispatch, 23, mir2::kSmItemHide);
  const auto show_index = packet_index(dispatch, 23, mir2::kSmItemShow);
  assert(hide_index.has_value());
  assert(show_index.has_value());
  assert(*hide_index < *show_index);
}

}  // namespace

int main() {
  assert_ref_broadcast_cache_boundary();
  assert_dead_actor_enters_as_death();
  assert_stale_item_hide_before_new_show();
  return 0;
}
