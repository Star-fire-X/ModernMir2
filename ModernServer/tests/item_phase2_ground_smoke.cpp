#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

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

mir2::LogicCommand make_drop(std::uint64_t session_id, std::int32_t make_index,
                             std::string name) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::drop_item;
  command.session_id = session_id;
  command.item_make_index = make_index;
  command.text = std::move(name);
  return command;
}

mir2::LogicCommand make_pickup(std::uint64_t session_id, std::int32_t x, std::int32_t y) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::pickup_item;
  command.session_id = session_id;
  command.x = x;
  command.y = y;
  return command;
}

mir2::LogicCommand make_gold(std::uint64_t session_id, std::int32_t amount) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::drop_gold;
  command.session_id = session_id;
  command.amount = amount;
  return command;
}

std::optional<mir2::DecodedLegacyGamePacket> find_packet(const mir2::RuntimeDispatch& dispatch,
                                                         std::uint16_t ident) {
  for (const auto& event : dispatch.session_events) {
    if (event.kind != mir2::SessionEventKind::send_packet) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return decoded;
    }
  }
  return std::nullopt;
}

std::optional<mir2::LegacyClientItem> decode_client_item(std::string_view body) {
  mir2::LegacyClientItem item;
  if (!mir2::legacy_decode_buffer(body, &item, sizeof(item))) {
    return std::nullopt;
  }
  return item;
}

bool has_trace(const mir2::RuntimeDispatch& dispatch, std::string action) {
  return std::any_of(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                     [&](const mir2::LegacyRuntimeTrace& trace) {
                       return trace.stage == "LegacyItem" && trace.action == action;
                     });
}

mir2::RuntimeDispatch tick_due(mir2::LogicRuntime& runtime, std::uint64_t& now_ms,
                               std::uint64_t delta_ms = 251) {
  now_ms += delta_ms;
  return runtime.tick(now_ms);
}

mir2::CharacterRecord make_character(std::string name, std::uint64_t make_index,
                                     std::uint16_t item_index = 1,
                                     std::uint16_t dura = 1,
                                     std::uint16_t dura_max = 1) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = 10;
  character.y = 10;
  character.ability.hp = 20;
  character.ability.max_hp = 20;
  character.ability.max_weight = 100;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  if (make_index != 0) {
    character.bag_items[0].make_index = static_cast<std::int32_t>(make_index);
    character.bag_items[0].index = item_index;
    character.bag_items[0].dura = dura;
    character.bag_items[0].dura_max = dura_max;
  }
  return character;
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "GroundMap", {}, 20, 20, 10, 10});
  mir2::ItemConfig token{1, "Token", 1, 1, 1, 0, 2, 1, -1, 0, 0};
  token.ani_count = 3;
  config.items.push_back(token);
  config.items.push_back(mir2::ItemConfig{2, "Raw Meat", 1, 1, 40, 0, 2, 1, -1, 0, 0});
  config.items.push_back(mir2::ItemConfig{3, "Event Token", 1, 1, 51, 0, 2, 1, -1, 0, 0});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  static_cast<void>(runtime.route_logic_command(make_enter(101, make_character("Hero", 1001))));
  std::uint64_t now_ms = 20;
  static_cast<void>(runtime.tick(now_ms));

  static_cast<void>(runtime.route_logic_command(make_drop(101, 1001, "Token")));
  const auto drop = tick_due(runtime, now_ms);
  assert(find_packet(drop, mir2::kSmItemShow).has_value());
  assert(find_packet(drop, mir2::kSmDelItem).has_value());

  static_cast<void>(runtime.route_logic_command(make_pickup(101, 10, 10)));
  const auto first_pickup = tick_due(runtime, now_ms);
  assert(find_packet(first_pickup, mir2::kSmItemHide).has_value());
  assert(find_packet(first_pickup, mir2::kSmAddItem).has_value());
  assert(!first_pickup.persist_requests.empty());

  static_cast<void>(runtime.route_logic_command(make_pickup(101, 10, 10)));
  const auto duplicate_pickup = tick_due(runtime, now_ms);
  assert(has_trace(duplicate_pickup, "empty_cell"));
  assert(!find_packet(duplicate_pickup, mir2::kSmAddItem).has_value());

  static_cast<void>(runtime.route_logic_command(make_drop(101, 1001, "Token")));
  const auto throttled_drop = tick_due(runtime, now_ms);
  assert(find_packet(throttled_drop, mir2::kSmDropItemFail).has_value());
  assert(!find_packet(throttled_drop, mir2::kSmDelItem).has_value());
  auto token_snapshot = runtime.snapshot_character_actor("Hero");
  assert(token_snapshot.has_value());
  assert(token_snapshot->bag_items[0].make_index == 1001);

  static_cast<void>(tick_due(runtime, now_ms, 3001));
  static_cast<void>(runtime.route_logic_command(make_drop(101, 1001, "Token")));
  static_cast<void>(tick_due(runtime, now_ms));
  const auto expired = tick_due(runtime, now_ms, 60ULL * 60ULL * 1000ULL + 1ULL);
  assert(find_packet(expired, mir2::kSmItemHide).has_value());
  static_cast<void>(runtime.route_logic_command(make_pickup(101, 10, 10)));
  const auto expired_pickup = tick_due(runtime, now_ms);
  assert(has_trace(expired_pickup, "empty_cell"));

  auto snapshot = runtime.snapshot_character_actor("Hero");
  assert(snapshot.has_value());
  snapshot->gold = 1000;
  snapshot->bag_items = {};

  mir2::LogicRuntime gold_runtime(config);
  gold_runtime.initialize();
  static_cast<void>(gold_runtime.route_logic_command(make_enter(201, *snapshot)));
  std::uint64_t gold_now_ms = 20;
  static_cast<void>(gold_runtime.tick(gold_now_ms));
  static_cast<void>(gold_runtime.route_logic_command(make_gold(201, 120)));
  static_cast<void>(tick_due(gold_runtime, gold_now_ms));
  static_cast<void>(gold_runtime.route_logic_command(make_gold(201, 80)));
  static_cast<void>(tick_due(gold_runtime, gold_now_ms));
  static_cast<void>(gold_runtime.route_logic_command(make_pickup(201, 10, 10)));
  const auto merged_pickup = tick_due(gold_runtime, gold_now_ms);
  const auto gold_changed = find_packet(merged_pickup, mir2::kSmGoldChanged);
  assert(gold_changed.has_value());
  assert(gold_changed->message.recog == 1000);
  assert(find_packet(merged_pickup, mir2::kSmItemHide).has_value());

  mir2::LogicRuntime meat_runtime(config);
  meat_runtime.initialize();
  static_cast<void>(meat_runtime.route_logic_command(
      make_enter(301, make_character("MeatHero", 3001, 2, 1500, 4000))));
  std::uint64_t meat_now_ms = 20;
  static_cast<void>(meat_runtime.tick(meat_now_ms));
  static_cast<void>(meat_runtime.route_logic_command(make_drop(301, 3001, "Raw Meat")));
  const auto meat_drop = tick_due(meat_runtime, meat_now_ms);
  const auto meat_del = find_packet(meat_drop, mir2::kSmDelItem);
  assert(meat_del.has_value());
  const auto deleted_meat = decode_client_item(meat_del->body);
  assert(deleted_meat.has_value());
  assert(deleted_meat->dura == 0);
  static_cast<void>(meat_runtime.route_logic_command(make_pickup(301, 10, 10)));
  const auto meat_pickup = tick_due(meat_runtime, meat_now_ms);
  const auto meat_add = find_packet(meat_pickup, mir2::kSmAddItem);
  assert(meat_add.has_value());
  const auto picked_meat = decode_client_item(meat_add->body);
  assert(picked_meat.has_value());
  assert(picked_meat->dura == 0);

  mir2::LogicRuntime event_runtime(config);
  event_runtime.initialize();
  static_cast<void>(event_runtime.route_logic_command(
      make_enter(401, make_character("EventHero", 4001, 3))));
  std::uint64_t event_now_ms = 20;
  static_cast<void>(event_runtime.tick(event_now_ms));
  static_cast<void>(event_runtime.route_logic_command(make_drop(401, 4001, "Event Token")));
  const auto event_drop = tick_due(event_runtime, event_now_ms);
  assert(find_packet(event_drop, mir2::kSmDropItemFail).has_value());
  assert(!find_packet(event_drop, mir2::kSmDelItem).has_value());
  const auto event_snapshot = event_runtime.snapshot_character_actor("EventHero");
  assert(event_snapshot.has_value());
  assert(event_snapshot->bag_items[0].make_index == 4001);

  return 0;
}
