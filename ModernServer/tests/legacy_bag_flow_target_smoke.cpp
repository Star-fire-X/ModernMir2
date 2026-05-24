#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "config/models.hpp"
#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/game_object.hpp"
#include "world/logic_runtime.hpp"

namespace {

int fail(const char* message) {
  std::cerr << "legacy_bag_flow_target_smoke: " << message << '\n';
  return 1;
}

mir2::LogicCommand make_enter(std::uint64_t session_id, mir2::CharacterRecord character) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = session_id;
  command.account_id = character.account_id;
  command.character_name = character.character_name;
  command.map_id = character.map_id;
  command.x = character.x;
  command.y = character.y;
  command.character = std::move(character);
  return command;
}

mir2::LogicCommand make_item_command(mir2::LogicCommandKind kind, std::uint64_t session_id,
                                     std::int32_t make_index, std::string name,
                                     std::int32_t slot = -1) {
  mir2::LogicCommand command;
  command.kind = kind;
  command.session_id = session_id;
  command.item_make_index = make_index;
  command.item_slot = slot;
  command.text = std::move(name);
  return command;
}

mir2::LogicCommand make_pickup(std::uint64_t session_id, std::int32_t x = 10,
                               std::int32_t y = 10) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::pickup_item;
  command.session_id = session_id;
  command.x = x;
  command.y = y;
  return command;
}

mir2::RuntimeDispatch tick_due(mir2::LogicRuntime& runtime, std::uint64_t& now_ms) {
  now_ms += 251;
  return runtime.tick(now_ms);
}

std::vector<std::uint16_t> packet_idents(const mir2::RuntimeDispatch& dispatch) {
  std::vector<std::uint16_t> idents;
  for (const auto& event : dispatch.session_events) {
    if (event.kind != mir2::SessionEventKind::send_packet) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value()) {
      idents.push_back(decoded->message.ident);
    }
  }
  return idents;
}

bool has_packet(const mir2::RuntimeDispatch& dispatch, std::uint16_t ident) {
  const auto idents = packet_idents(dispatch);
  return std::find(idents.begin(), idents.end(), ident) != idents.end();
}

int packet_count(const mir2::RuntimeDispatch& dispatch, std::uint16_t ident) {
  const auto idents = packet_idents(dispatch);
  return static_cast<int>(std::count(idents.begin(), idents.end(), ident));
}

bool has_trace(const mir2::RuntimeDispatch& dispatch, std::string action) {
  return std::any_of(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                     [&](const mir2::LegacyRuntimeTrace& trace) {
                       return trace.stage == "LegacyItem" && trace.action == action;
                     });
}

std::vector<std::uint16_t> filtered_packet_idents(
    const mir2::RuntimeDispatch& dispatch, std::initializer_list<std::uint16_t> relevant) {
  const auto idents = packet_idents(dispatch);
  std::vector<std::uint16_t> filtered;
  for (const auto ident : idents) {
    if (std::find(relevant.begin(), relevant.end(), ident) != relevant.end()) {
      filtered.push_back(ident);
    }
  }
  return filtered;
}

bool matches_packet_order(const mir2::RuntimeDispatch& dispatch,
                          std::initializer_list<std::uint16_t> expected,
                          std::initializer_list<std::uint16_t> relevant) {
  return filtered_packet_idents(dispatch, relevant) ==
         std::vector<std::uint16_t>(expected.begin(), expected.end());
}

std::vector<std::int32_t> bag_make_indices(const mir2::CharacterRecord& character) {
  std::vector<std::int32_t> result;
  for (const auto& item : character.bag_items) {
    if (!mir2::is_empty(item)) {
      result.push_back(item.make_index);
    }
  }
  return result;
}

mir2::LegacyUserItem item(std::int32_t make_index, std::uint16_t index,
                          std::uint16_t dura = 1, std::uint16_t dura_max = 1) {
  mir2::LegacyUserItem result;
  result.make_index = make_index;
  result.index = index;
  result.dura = dura;
  result.dura_max = dura_max;
  return result;
}

mir2::HostConfig make_config() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "BagFlow", {}, 20, 20, 10, 10});
  config.items.push_back(mir2::ItemConfig{1, "Wooden Sword", 3, 100, 5, 1, 1, 1000, 1, 0, 0});
  config.items.push_back(mir2::ItemConfig{2, "Healing Potion", 1, 30, 0, 0, 2, 1, -1, 10, 0});
  config.items.push_back(mir2::ItemConfig{3, "Token", 10, 1, 1, 0, 3, 1, -1, 0, 0});
  return config;
}

mir2::CharacterRecord make_character(std::string name, std::int32_t x = 10,
                                      std::int32_t y = 10) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = x;
  character.y = y;
  character.sex = 0;
  character.hair = 4;
  character.ability.level = 20;
  character.ability.hp = 5;
  character.ability.max_hp = 15;
  character.ability.mp = 3;
  character.ability.max_mp = 10;
  character.ability.max_exp = 100;
  character.ability.max_weight = 100;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  return character;
}

bool test_direct_tlist_compact() {
  std::unordered_map<std::int32_t, mir2::ItemConfig> items;
  auto character = make_character("Compact");
  character.bag_items[0] = item(1001, 1);
  character.bag_items[1] = item(1002, 2);
  mir2::Player player(1, 1, character);

  if (!player.remove_bag_item(1001, {}, items).has_value()) {
    return false;
  }
  if (!player.add_bag_item(item(1003, 3))) {
    return false;
  }
  if (bag_make_indices(player.character()) != std::vector<std::int32_t>{1002, 1003}) {
    return false;
  }

  character = make_character("CompactAt");
  character.bag_items[0] = item(2001, 1);
  character.bag_items[1] = item(2002, 2);
  character.bag_items[2] = item(2003, 3);
  mir2::Player slot_player(2, 2, character);
  if (!slot_player.remove_bag_item_at(1).has_value()) {
    return false;
  }
  return bag_make_indices(slot_player.character()) == std::vector<std::int32_t>{2001, 2003};
}

bool test_consumable_deletes_instance() {
  mir2::LogicRuntime runtime(make_config());
  runtime.initialize();
  auto character = make_character("PotionUser");
  character.bag_items[0] = item(3001, 2, 3, 3);
  static_cast<void>(runtime.route_logic_command(make_enter(301, character)));
  std::uint64_t now_ms = 20;
  static_cast<void>(runtime.tick(now_ms));

  static_cast<void>(runtime.route_logic_command(make_item_command(
      mir2::LogicCommandKind::eat_item, 301, 3001, "Healing Potion")));
  const auto dispatch = tick_due(runtime, now_ms);
  if (!matches_packet_order(dispatch,
                            {mir2::kSmDelItem, mir2::kSmHealthSpellChanged,
                             mir2::kSmWeightChanged, mir2::kSmEatOk},
                            {mir2::kSmDelItem, mir2::kSmHealthSpellChanged,
                             mir2::kSmWeightChanged, mir2::kSmEatOk, mir2::kSmEatFail,
                             mir2::kSmUpdateItem, mir2::kSmAddItem})) {
    return false;
  }
  const auto snapshot = runtime.snapshot_character_actor("PotionUser");
  return snapshot.has_value() && bag_make_indices(*snapshot).empty();
}

bool test_drop_and_pickup_order() {
  mir2::LogicRuntime runtime(make_config());
  runtime.initialize();
  auto character = make_character("GroundUser");
  character.bag_items[0] = item(4001, 3);
  static_cast<void>(runtime.route_logic_command(make_enter(401, character)));
  std::uint64_t now_ms = 20;
  static_cast<void>(runtime.tick(now_ms));

  static_cast<void>(runtime.route_logic_command(make_item_command(
      mir2::LogicCommandKind::drop_item, 401, 4001, "Token")));
  const auto drop = tick_due(runtime, now_ms);
  if (!matches_packet_order(drop,
                            {mir2::kSmItemShow, mir2::kSmDelItem,
                             mir2::kSmWeightChanged, mir2::kSmDropItemSuccess},
                            {mir2::kSmItemShow, mir2::kSmDelItem, mir2::kSmWeightChanged,
                             mir2::kSmDropItemSuccess, mir2::kSmDropItemFail})) {
    return false;
  }

  static_cast<void>(runtime.route_logic_command(make_pickup(401)));
  const auto pickup = tick_due(runtime, now_ms);
  if (!matches_packet_order(pickup,
                            {mir2::kSmItemHide, mir2::kSmAddItem,
                             mir2::kSmWeightChanged},
                            {mir2::kSmItemHide, mir2::kSmAddItem,
                             mir2::kSmWeightChanged, mir2::kSmDelItem})) {
    return false;
  }
  const auto snapshot = runtime.snapshot_character_actor("GroundUser");
  return snapshot.has_value() && bag_make_indices(*snapshot) == std::vector<std::int32_t>{4001};
}

bool test_pickup_weight_failure_keeps_ground_item() {
  mir2::LogicRuntime runtime(make_config());
  runtime.initialize();
  auto character = make_character("HeavyUser");
  character.ability.max_weight = 0;
  character.bag_items[0] = item(5001, 3);
  static_cast<void>(runtime.route_logic_command(make_enter(501, character)));
  std::uint64_t now_ms = 20;
  static_cast<void>(runtime.tick(now_ms));

  static_cast<void>(runtime.route_logic_command(make_item_command(
      mir2::LogicCommandKind::drop_item, 501, 5001, "Token")));
  static_cast<void>(tick_due(runtime, now_ms));

  static_cast<void>(runtime.route_logic_command(make_pickup(501)));
  const auto first_fail = tick_due(runtime, now_ms);
  if (!has_trace(first_fail, "bag_reject") || has_packet(first_fail, mir2::kSmItemHide) ||
      has_packet(first_fail, mir2::kSmAddItem)) {
    return false;
  }

  static_cast<void>(runtime.route_logic_command(make_pickup(501)));
  const auto second_fail = tick_due(runtime, now_ms);
  return has_trace(second_fail, "bag_reject") && !has_trace(second_fail, "empty_cell");
}

bool test_take_on_take_off_order_and_compact() {
  mir2::LogicRuntime runtime(make_config());
  runtime.initialize();
  auto character = make_character("EquipUser");
  character.bag_items[0] = item(6001, 2);
  character.bag_items[1] = item(6002, 1, 600, 1000);
  character.bag_items[2] = item(6003, 2);
  static_cast<void>(runtime.route_logic_command(make_enter(601, character)));
  std::uint64_t now_ms = 20;
  static_cast<void>(runtime.tick(now_ms));

  static_cast<void>(runtime.route_logic_command(make_item_command(
      mir2::LogicCommandKind::take_on_item, 601, 6002, "Wooden Sword", mir2::kEquipWeapon)));
  const auto take_on = tick_due(runtime, now_ms);
  if (!matches_packet_order(take_on,
                            {mir2::kSmDelItem, mir2::kSmUpdateItem,
                             mir2::kSmSendUseItems, mir2::kSmAbility, mir2::kSmSubAbility,
                             mir2::kSmTakeOnOk, mir2::kSmWeightChanged},
                            {mir2::kSmDelItem, mir2::kSmAddItem, mir2::kSmUpdateItem,
                             mir2::kSmSendUseItems, mir2::kSmAbility, mir2::kSmSubAbility,
                             mir2::kSmTakeOnOk, mir2::kSmTakeOnFail,
                             mir2::kSmWeightChanged})) {
    return false;
  }
  auto snapshot = runtime.snapshot_character_actor("EquipUser");
  if (!snapshot.has_value() || snapshot->equipped_items[mir2::kEquipWeapon].make_index != 6002 ||
      bag_make_indices(*snapshot) != std::vector<std::int32_t>{6001, 6003}) {
    return false;
  }

  static_cast<void>(runtime.route_logic_command(make_item_command(
      mir2::LogicCommandKind::take_off_item, 601, 6002, "Wooden Sword", mir2::kEquipWeapon)));
  const auto take_off = tick_due(runtime, now_ms);
  if (!matches_packet_order(take_off,
                            {mir2::kSmTakeOffOk, mir2::kSmAddItem,
                             mir2::kSmSendUseItems, mir2::kSmAbility, mir2::kSmSubAbility,
                             mir2::kSmWeightChanged},
                            {mir2::kSmTakeOffOk, mir2::kSmTakeOffFail, mir2::kSmAddItem,
                             mir2::kSmDelItem, mir2::kSmSendUseItems, mir2::kSmAbility,
                             mir2::kSmSubAbility, mir2::kSmWeightChanged})) {
    return false;
  }
  snapshot = runtime.snapshot_character_actor("EquipUser");
  return snapshot.has_value() && mir2::is_empty(snapshot->equipped_items[mir2::kEquipWeapon]) &&
         bag_make_indices(*snapshot) == std::vector<std::int32_t>{6001, 6003, 6002};
}

}  // namespace

int main() {
  if (!test_direct_tlist_compact()) {
    return fail("direct TList compact semantics");
  }
  if (!test_consumable_deletes_instance()) {
    return fail("consumable deletes instance and packet order");
  }
  if (!test_drop_and_pickup_order()) {
    return fail("drop/pickup packet order");
  }
  if (!test_pickup_weight_failure_keeps_ground_item()) {
    return fail("pickup weight failure keeps ground item");
  }
  if (!test_take_on_take_off_order_and_compact()) {
    return fail("take-on/take-off order and compact semantics");
  }
  return 0;
}
