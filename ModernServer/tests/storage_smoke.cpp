#include <optional>
#include <iostream>
#include <string>
#include <vector>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "util/string_utils.hpp"
#include "world/logic_runtime.hpp"

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

std::vector<mir2::LegacyClientItem> decode_items(std::string_view body) {
  std::vector<mir2::LegacyClientItem> items;
  for (const auto& part : mir2::util::split(body, '/')) {
    if (part.empty()) {
      continue;
    }
    mir2::LegacyClientItem item;
    if (mir2::legacy_decode_buffer(part, &item, sizeof(item))) {
      items.push_back(item);
    }
  }
  return items;
}

std::uint16_t weight_checksum(std::uint16_t weight, std::uint16_t wear_weight,
                              std::uint16_t hand_weight) {
  return static_cast<std::uint16_t>(
      (((weight + wear_weight + hand_weight) ^ 0x3A5F) ^ 0x1F35) ^ 0xAA21);
}

mir2::LogicCommand make_storage_command(mir2::LogicCommandKind kind, std::uint64_t session_id,
                                        std::uint64_t merchant_id, std::int32_t make_index,
                                        std::string item_name) {
  mir2::LogicCommand command;
  command.kind = kind;
  command.session_id = session_id;
  command.target_actor_id = merchant_id;
  command.item_make_index = make_index;
  command.text = std::move(item_name);
  return command;
}

mir2::RuntimeDispatch tick_until_packet(mir2::LogicRuntime& runtime, std::uint16_t ident) {
  mir2::RuntimeDispatch dispatch;
  for (int i = 0; i < 30; ++i) {
    dispatch = runtime.tick();
    if (find_packet(dispatch, ident).has_value()) {
      break;
    }
  }
  return dispatch;
}

}  // namespace

int main() {
  auto fail = [](const char* message) {
    std::cerr << "storage_smoke failed at " << message << '\n';
    return 1;
  };

  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "StorageMap", {}, 0, 0, 20, 20});
  config.items.push_back(mir2::ItemConfig{1, "Storage Sword", 3, 90, 5, 1, 1, 1000, 1, 0, 0});
  config.npcs.push_back(
      mir2::NpcConfig{"storage_1", "0", "Warehouse Keeper", 11, 10, "storage_1.txt", "storage"});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  mir2::CharacterRecord hero;
  hero.account_id = "guest";
  hero.character_name = "Hero";
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;
  hero.ability.max_hp = 15;
  hero.ability.max_mp = 10;
  hero.ability.max_exp = 100;
  hero.ability.max_weight = 30;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;
  hero.bag_items[0].index = 1;
  hero.bag_items[0].make_index = 1001;
  hero.bag_items[0].dura = 1000;
  hero.bag_items[0].dura_max = 1000;

  mir2::LogicCommand enter;
  enter.kind = mir2::LogicCommandKind::enter_world;
  enter.session_id = 8;
  enter.account_id = "guest";
  enter.character_name = "Hero";
  enter.map_id = "0";
  enter.x = 10;
  enter.y = 10;
  enter.character = hero;
  static_cast<void>(runtime.route_logic_command(enter));

  const auto login_dispatch = runtime.tick();
  if (!find_packet(login_dispatch, mir2::kSmNewMap).has_value()) {
    return 1;
  }
  for (int i = 0; i < 4; ++i) {
    static_cast<void>(runtime.tick());
  }

  mir2::LogicCommand click_npc;
  click_npc.kind = mir2::LogicCommandKind::click_npc;
  click_npc.session_id = 8;
  std::uint64_t merchant_id = 0;
  mir2::RuntimeDispatch first_click_dispatch;
  std::optional<mir2::DecodedLegacyGamePacket> storage_menu;
  std::optional<mir2::DecodedLegacyGamePacket> initial_storage;
  for (std::uint64_t candidate = 1; candidate <= 100; ++candidate) {
    click_npc.target_actor_id = candidate;
    static_cast<void>(runtime.route_logic_command(click_npc));
    first_click_dispatch = runtime.tick();
    storage_menu = find_packet(first_click_dispatch, mir2::kSmSendUserStorageItem);
    initial_storage = find_packet(first_click_dispatch, mir2::kSmSaveItemList);
    if (storage_menu.has_value() && initial_storage.has_value()) {
      merchant_id = candidate;
      break;
    }
  }
  if (!storage_menu.has_value() || !initial_storage.has_value() ||
      storage_menu->message.recog != 1 || initial_storage->message.recog != 1 ||
      initial_storage->message.series != 0 ||
      !decode_items(initial_storage->body).empty()) {
    return 1;
  }

  mir2::RuntimeDispatch store_dispatch;
  for (std::uint64_t candidate = 1; candidate <= 100; ++candidate) {
    static_cast<void>(runtime.route_logic_command(make_storage_command(
        mir2::LogicCommandKind::storage_item, 8, candidate, 1001, "Storage Sword")));
    store_dispatch = tick_until_packet(runtime, mir2::kSmStorageOk);
    if (find_packet(store_dispatch, mir2::kSmStorageOk).has_value()) {
      merchant_id = candidate;
      break;
    }
  }
  const auto storage_ok = find_packet(store_dispatch, mir2::kSmStorageOk);
  const auto store_delete = find_packet(store_dispatch, mir2::kSmDelItem);
  const auto store_weight = find_packet(store_dispatch, mir2::kSmWeightChanged);
  if (!storage_ok.has_value() || !store_delete.has_value() || !store_weight.has_value() ||
      store_weight->message.recog != 0 ||
      store_weight->message.param != 0 || store_weight->message.tag != 0 ||
      store_weight->message.series != weight_checksum(0, 0, 0) ||
      store_dispatch.persist_requests.empty()) {
    return fail("store ok");
  }

  mir2::LogicRuntime full_runtime(config);
  full_runtime.initialize();
  auto full_hero = hero;
  full_hero.character_name = "FullStorageHero";
  full_hero.bag_items[0].make_index = 2001;
  full_hero.bag_items[1] = full_hero.bag_items[0];
  full_hero.bag_items[1].make_index = 2002;
  for (std::size_t index = 0; index < full_hero.storage_items.size(); ++index) {
    full_hero.storage_items[index] = full_hero.bag_items[0];
    full_hero.storage_items[index].make_index = static_cast<std::int32_t>(3000 + index);
  }

  mir2::LogicCommand full_enter = enter;
  full_enter.session_id = 9;
  full_enter.character_name = "FullStorageHero";
  full_enter.character = full_hero;
  static_cast<void>(full_runtime.route_logic_command(full_enter));
  if (!find_packet(full_runtime.tick(), mir2::kSmNewMap).has_value()) {
    return fail("full storage login");
  }
  for (int i = 0; i < 4; ++i) {
    static_cast<void>(full_runtime.tick());
  }

  std::uint64_t full_merchant_id = 0;
  for (std::uint64_t candidate = 1; candidate <= 100; ++candidate) {
    click_npc.session_id = 9;
    click_npc.target_actor_id = candidate;
    static_cast<void>(full_runtime.route_logic_command(click_npc));
    const auto click_dispatch = full_runtime.tick();
    if (find_packet(click_dispatch, mir2::kSmSendUserStorageItem).has_value()) {
      full_merchant_id = candidate;
      break;
    }
  }
  if (full_merchant_id == 0) {
    return fail("full storage merchant");
  }

  mir2::RuntimeDispatch full_dispatch;
  for (std::uint64_t candidate = 1; candidate <= 100; ++candidate) {
    static_cast<void>(full_runtime.route_logic_command(make_storage_command(
        mir2::LogicCommandKind::storage_item, 9, candidate, 2001, "Storage Sword")));
    full_dispatch = tick_until_packet(full_runtime, mir2::kSmStorageFull);
    if (find_packet(full_dispatch, mir2::kSmStorageFull).has_value()) {
      break;
    }
  }
  if (!find_packet(full_dispatch, mir2::kSmStorageFull).has_value() ||
      find_packet(full_dispatch, mir2::kSmDelItem).has_value()) {
    return fail("full storage result");
  }

  const auto full_snapshot = full_runtime.snapshot_character_actor("FullStorageHero");
  if (!full_snapshot.has_value() ||
      full_snapshot->bag_items[0].make_index != 2001 ||
      full_snapshot->bag_items[1].make_index != 2002) {
    return fail("full storage bag order");
  }

  return 0;
}
