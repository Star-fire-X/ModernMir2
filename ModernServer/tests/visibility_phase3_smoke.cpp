#include <cassert>
#include <cstdint>
#include <optional>
#include <string>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"
#include "world/map_actor.hpp"

namespace {

std::optional<mir2::DecodedLegacyGamePacket> find_packet(
    const mir2::RuntimeDispatch& dispatch, std::uint64_t session_id, std::uint16_t ident) {
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

mir2::CharacterRecord make_character(std::string name, std::int32_t x, std::int32_t y) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = x;
  character.y = y;
  character.ability.hp = 20;
  character.ability.max_hp = 20;
  character.ability.max_weight = 30;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  return character;
}

void enter(mir2::LogicRuntime& runtime, std::uint64_t session_id,
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
  static_cast<void>(runtime.tick());
}

void advance(mir2::LogicRuntime& runtime, int ticks = 12) {
  for (int index = 0; index < ticks; ++index) {
    static_cast<void>(runtime.tick());
  }
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

mir2::RuntimeDispatch pickup_item(mir2::LogicRuntime& runtime, std::uint64_t session_id,
                                  std::int32_t x, std::int32_t y) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::pickup_item;
  command.session_id = session_id;
  command.x = x;
  command.y = y;
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

}  // namespace

int main() {
  {
    mir2::HostConfig config;
    config.budgets.tick_ms = 20;
    config.maps.push_back(mir2::MapConfig{"0", "ItemVisibilityMap", {}, 0, 0, 60, 60});
    config.items.push_back(mir2::ItemConfig{1, "Wooden Sword", 3, 100, 5, 1, 1, 1000, 1, 0, 0});

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    auto dropper = make_character("Dropper", 20, 20);
    dropper.bag_items[0].index = 1;
    dropper.bag_items[0].make_index = 1001;
    dropper.bag_items[0].dura = 600;
    dropper.bag_items[0].dura_max = 1000;
    enter(runtime, 81, dropper);
    enter(runtime, 82, make_character("Near", 21, 20));
    enter(runtime, 83, make_character("Far", 40, 40));

    advance(runtime);
    auto dispatch = drop_item(runtime, 81, 1001, "Wooden Sword");
    assert(find_packet(dispatch, 81, mir2::kSmItemShow).has_value());
    assert(find_packet(dispatch, 82, mir2::kSmItemShow).has_value());
    assert(!find_packet(dispatch, 83, mir2::kSmItemShow).has_value());

    dispatch = pickup_item(runtime, 81, 20, 20);
    assert(find_packet(dispatch, 81, mir2::kSmItemHide).has_value());
    assert(find_packet(dispatch, 82, mir2::kSmItemHide).has_value());
    assert(!find_packet(dispatch, 83, mir2::kSmItemHide).has_value());
  }

  {
    mir2::HostConfig config;
    config.budgets.tick_ms = 20;
    mir2::MapConfig map{"0", "SameMapGate", {}, 0, 0, 60, 60};
    map.gates.push_back(mir2::MapGateConfig{10, 10, "0", 30, 30, false});
    config.maps.push_back(map);

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    enter(runtime, 91, make_character("Traveler", 9, 10));
    enter(runtime, 92, make_character("OldWatcher", 10, 11));
    enter(runtime, 93, make_character("NewWatcher", 31, 30));

    advance(runtime);
    const auto dispatch = walk(runtime, 91, 10, 10);
    assert(find_packet(dispatch, 91, mir2::kSmClearObjects).has_value());
    assert(find_packet(dispatch, 91, mir2::kSmChangeMap).has_value());
    assert(find_packet(dispatch, 92, mir2::kSmDisappear).has_value());
    assert(find_packet(dispatch, 93, mir2::kSmTurn).has_value());
  }

  {
    mir2::MapActor map(mir2::MapConfig{"0", "EventVisibilityMap", {}, 0, 0, 60, 60},
                       mir2::LogicBudgetConfig{}, {}, {});
    mir2::ActorMail spawn;
    spawn.kind = mir2::ActorMailKind::spawn_player;
    spawn.actor_id = 1;
    spawn.session_id = 1;
    spawn.map_id = "0";
    spawn.x = 10;
    spawn.y = 10;
    spawn.character = make_character("EventWatcher", 10, 10);
    static_cast<void>(map.legacy_spawn_player(spawn, 1, 0, true));

    mir2::RuntimeDispatch dispatch;
    assert(map.legacy_add_event_object(9001, 11, 10, 0, &dispatch));
    assert(!map.legacy_add_event_object(9002, 12, 10, 0, false, &dispatch,
                                        mir2::LegacyEventType::stone_mine));
    assert(map.legacy_player_tracks_event(1, 9001));
    assert(dispatch.session_events.empty());

    map.legacy_remove_event_object(9001, 11, 10, &dispatch);
    assert(!map.legacy_player_tracks_event(1, 9001));
    assert(dispatch.session_events.empty());
  }

  return 0;
}
