#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "config/models.hpp"
#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "util/string_utils.hpp"
#include "world/logic_runtime.hpp"

namespace {

int fail(const char* stage) {
  std::cerr << "legacy_bag_wire_abi_smoke failed at " << stage << '\n';
  return 1;
}

std::size_t encoded_client_item_size() {
  const mir2::LegacyClientItem item{};
  return mir2::legacy_encode_buffer(&item, sizeof(item)).size();
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
  mir2::LegacyClientItem item{};
  if (!mir2::legacy_decode_buffer(body, &item, sizeof(item))) {
    return std::nullopt;
  }
  return item;
}

std::vector<mir2::LegacyClientItem> decode_bag_items(std::string_view body,
                                                     std::size_t expected_encoded_size) {
  std::vector<mir2::LegacyClientItem> items;
  for (const auto& part : mir2::util::split(body, '/')) {
    if (part.empty()) {
      continue;
    }
    if (part.size() != expected_encoded_size) {
      return {};
    }
    if (auto item = decode_client_item(part); item.has_value()) {
      items.push_back(*item);
    }
  }
  return items;
}

mir2::RuntimeDispatch tick_player_due(mir2::LogicRuntime& runtime, std::uint64_t& now_ms) {
  now_ms += 251;
  return runtime.tick(now_ms);
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

bool check_item_packet(const mir2::DecodedLegacyGamePacket& packet, std::uint16_t ident,
                       std::uint64_t actor_id, std::int32_t make_index,
                       std::string_view name, std::uint8_t std_mode, std::uint16_t dura,
                       std::uint16_t dura_max, std::size_t expected_encoded_size) {
  if (packet.message.ident != ident ||
      packet.message.recog != static_cast<std::int32_t>(actor_id) ||
      packet.message.param != 0 || packet.message.tag != 0 || packet.message.series != 1 ||
      packet.body.size() != expected_encoded_size) {
    return false;
  }
  const auto item = decode_client_item(packet.body);
  return item.has_value() && item->make_index == make_index &&
         mir2::to_string(item->item.name) == name && item->item.std_mode == std_mode &&
         item->dura == dura && item->dura_max == dura_max && sizeof(*item) == 84;
}

bool check_abi() {
  return sizeof(mir2::LegacyStdItem) == 76 &&
         offsetof(mir2::LegacyStdItem, std_mode) == 15 &&
         offsetof(mir2::LegacyStdItem, item_desc) == 20 &&
         offsetof(mir2::LegacyStdItem, looks) == 22 &&
         offsetof(mir2::LegacyStdItem, need) == 36 &&
         offsetof(mir2::LegacyStdItem, need_level) == 37 &&
         offsetof(mir2::LegacyStdItem, price) == 40 &&
         offsetof(mir2::LegacyStdItem, stock) == 44 &&
         offsetof(mir2::LegacyStdItem, atk_spd) == 48 &&
         offsetof(mir2::LegacyStdItem, undead) == 53 &&
         offsetof(mir2::LegacyStdItem, hp_add) == 56 &&
         offsetof(mir2::LegacyStdItem, mp_add) == 60 &&
         offsetof(mir2::LegacyStdItem, exp_add) == 64 &&
         offsetof(mir2::LegacyStdItem, eff_type1) == 68 &&
         offsetof(mir2::LegacyStdItem, eff_value2) == 73 &&
         sizeof(mir2::LegacyClientItem) == 84 &&
         offsetof(mir2::LegacyClientItem, make_index) == 76 &&
         offsetof(mir2::LegacyClientItem, dura) == 80 &&
         offsetof(mir2::LegacyClientItem, dura_max) == 82;
}

}  // namespace

int main() {
  if (!check_abi()) {
    return fail("abi");
  }
  const auto expected_encoded_size = encoded_client_item_size();

  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "WireAbi", {}, 0, 0, 10, 10});
  config.items.push_back(mir2::ItemConfig{1, "Wooden Sword", 3, 100, 5, 1, 1, 1000, 1, 0, 0});
  config.items.push_back(mir2::ItemConfig{2, "Basic Drug", 1, 30, 0, 0, 2, 1, -1, 10, 0});

  mir2::CharacterRecord hero;
  hero.account_id = "wire";
  hero.character_name = "Hero";
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;
  hero.ability.max_hp = 15;
  hero.ability.max_mp = 10;
  hero.ability.max_weight = 30;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;
  hero.bag_items[0] = mir2::LegacyUserItem{1001, 1, 600, 1000};
  hero.bag_items[1] = mir2::LegacyUserItem{1002, 2, 1, 1};

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  static_cast<void>(runtime.route_logic_command(make_enter(7, hero)));
  std::uint64_t now_ms = 20;
  const auto enter_dispatch = runtime.tick(now_ms);
  const auto new_map = find_packet(enter_dispatch, mir2::kSmNewMap);
  if (!new_map.has_value()) {
    return fail("enter new map");
  }
  const auto actor_id = static_cast<std::uint64_t>(static_cast<std::uint32_t>(new_map->message.recog));

  mir2::LogicCommand query_bag;
  query_bag.kind = mir2::LogicCommandKind::query_bag_items;
  query_bag.session_id = 7;
  static_cast<void>(runtime.route_logic_command(query_bag));
  const auto bag_dispatch = tick_player_due(runtime, now_ms);
  const auto bag_packet = find_packet(bag_dispatch, mir2::kSmBagItems);
  if (!bag_packet.has_value() || bag_packet->message.recog != static_cast<std::int32_t>(actor_id) ||
      bag_packet->message.series != 2) {
    return fail("bag packet header");
  }
  const auto bag_items = decode_bag_items(bag_packet->body, expected_encoded_size);
  if (bag_items.size() != 2 || bag_items[0].make_index != 1001 ||
      bag_items[1].make_index != 1002 || mir2::to_string(bag_items[0].item.name) != "Wooden Sword" ||
      bag_items[0].item.std_mode != 5 || bag_items[0].item.price != 100 ||
      bag_packet->body.size() != bag_packet->message.series * (expected_encoded_size + 1) ||
      bag_packet->body.back() != '/') {
    return fail("bag packet body");
  }

  static_cast<void>(runtime.route_logic_command(
      make_item_command(mir2::LogicCommandKind::drop_item, 7, 1001, "Wooden Sword")));
  const auto drop_dispatch = tick_player_due(runtime, now_ms);
  const auto del_packet = find_packet(drop_dispatch, mir2::kSmDelItem);
  if (!del_packet.has_value() ||
      !check_item_packet(*del_packet, mir2::kSmDelItem, actor_id, 1001, "Wooden Sword", 5, 600,
                         1000, expected_encoded_size)) {
    return fail("del item packet");
  }

  mir2::LogicCommand pickup;
  pickup.kind = mir2::LogicCommandKind::pickup_item;
  pickup.session_id = 7;
  pickup.x = 10;
  pickup.y = 10;
  static_cast<void>(runtime.route_logic_command(pickup));
  const auto pickup_dispatch = tick_player_due(runtime, now_ms);
  const auto add_packet = find_packet(pickup_dispatch, mir2::kSmAddItem);
  if (!add_packet.has_value() ||
      !check_item_packet(*add_packet, mir2::kSmAddItem, actor_id, 1001, "Wooden Sword", 5, 600,
                         1000, expected_encoded_size)) {
    return fail("add item packet");
  }

  static_cast<void>(runtime.route_logic_command(make_item_command(
      mir2::LogicCommandKind::take_on_item, 7, 1001, "Wooden Sword", 1)));
  const auto take_on_dispatch = tick_player_due(runtime, now_ms);
  const auto update_packet = find_packet(take_on_dispatch, mir2::kSmUpdateItem);
  if (!update_packet.has_value() || !check_item_packet(*update_packet, mir2::kSmUpdateItem,
                                                       actor_id, 1001, "Wooden Sword", 5, 600,
                                                       1000, expected_encoded_size)) {
    return fail("update item packet");
  }

  return 0;
}
