#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "storage/repository.hpp"
#include "util/string_utils.hpp"
#include "world/logic_runtime.hpp"

namespace {

constexpr std::uint64_t kSessionId = 81;

mir2::ItemConfig make_item_config(std::int32_t id, std::string name, std::int32_t weight,
                                  std::int32_t std_mode = 5) {
  mir2::ItemConfig item;
  item.id = id;
  item.name = std::move(name);
  item.weight = weight;
  item.price = 100;
  item.std_mode = std_mode;
  item.shape = 1;
  item.looks = id;
  item.dura_max = 1000;
  return item;
}

mir2::NpcConfig make_storage_npc(std::int32_t x, std::int32_t y) {
  mir2::NpcConfig npc;
  npc.id = "storage_1";
  npc.map_id = "0";
  npc.name = "Warehouse Keeper";
  npc.x = x;
  npc.y = y;
  npc.script = "storage_1.txt";
  npc.service = "storage";
  return npc;
}

mir2::HostConfig make_host_config(std::int32_t npc_x = 11, std::int32_t npc_y = 10,
                                  std::int32_t item1_std_mode = 5,
                                  bool legacy_approval_mode = false) {
  mir2::HostConfig config;
  config.runtime.legacy_approval_mode = legacy_approval_mode;
  config.maps.push_back(mir2::MapConfig{"0", "StorageMap", {}, 0, 0, 80, 80});
  config.items.push_back(make_item_config(1, "Storage Sword", 3, item1_std_mode));
  config.items.push_back(make_item_config(2, "Storage Ring", 1));
  config.items.push_back(make_item_config(3, "Storage Potion", 1));
  config.items.push_back(make_item_config(4, "Storage Gem", 2));
  config.npcs.push_back(make_storage_npc(npc_x, npc_y));
  return config;
}

mir2::LegacyUserItem make_user_item(std::uint16_t index, std::int32_t make_index) {
  mir2::LegacyUserItem item;
  item.index = index;
  item.make_index = make_index;
  item.dura = static_cast<std::uint16_t>(1000 + index);
  item.dura_max = static_cast<std::uint16_t>(2000 + index);
  for (std::size_t i = 0; i < item.desc.size(); ++i) {
    item.desc[i] = static_cast<std::uint8_t>((make_index + static_cast<std::int32_t>(i)) & 0xff);
  }
  item.color_r = static_cast<std::uint8_t>(10 + index);
  item.color_g = static_cast<std::uint8_t>(20 + index);
  item.color_b = static_cast<std::uint8_t>(30 + index);
  const std::string prefix = "PR" + std::to_string(make_index);
  std::copy(prefix.begin(), prefix.end(), item.prefix.begin());
  return item;
}

mir2::CharacterRecord make_character(std::int32_t x = 10, std::int32_t y = 10) {
  mir2::CharacterRecord character;
  character.account_id = "guest";
  character.character_name = "StorageHero";
  character.map_id = "0";
  character.x = x;
  character.y = y;
  character.ability.max_hp = 15;
  character.ability.max_mp = 10;
  character.ability.max_exp = 100;
  character.ability.max_weight = 200;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  return character;
}

std::vector<mir2::DecodedLegacyGamePacket> collect_packets(const mir2::RuntimeDispatch& dispatch,
                                                           std::uint64_t session_id = 0) {
  std::vector<mir2::DecodedLegacyGamePacket> packets;
  for (const auto& event : dispatch.session_events) {
    if (session_id != 0 && event.session_id != session_id) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value()) {
      packets.push_back(*decoded);
    }
  }
  return packets;
}

std::optional<mir2::DecodedLegacyGamePacket> find_packet(
    const mir2::RuntimeDispatch& dispatch, std::uint16_t ident, std::uint64_t session_id = 0) {
  for (const auto& packet : collect_packets(dispatch, session_id)) {
    if (packet.message.ident == ident) {
      return packet;
    }
  }
  return std::nullopt;
}

std::vector<mir2::LegacyClientItem> decode_storage_items(std::string_view body) {
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

bool same_user_item_bytes(const mir2::LegacyUserItem& lhs, const mir2::LegacyUserItem& rhs) {
  return std::memcmp(&lhs, &rhs, sizeof(lhs)) == 0;
}

bool enter_world(mir2::LogicRuntime& runtime, const mir2::CharacterRecord& character,
                 std::uint64_t session_id = kSessionId) {
  mir2::LogicCommand enter;
  enter.kind = mir2::LogicCommandKind::enter_world;
  enter.session_id = session_id;
  enter.account_id = character.account_id;
  enter.character_name = character.character_name;
  enter.map_id = character.map_id;
  enter.x = character.x;
  enter.y = character.y;
  enter.character = character;
  static_cast<void>(runtime.route_logic_command(enter));

  const auto login_dispatch = runtime.tick();
  if (!find_packet(login_dispatch, mir2::kSmNewMap, session_id).has_value()) {
    return false;
  }
  for (int i = 0; i < 4; ++i) {
    static_cast<void>(runtime.tick());
  }
  return true;
}

mir2::LogicCommand make_storage_command(mir2::LogicCommandKind kind, std::uint64_t merchant_id,
                                        std::int32_t make_index, std::string item_name,
                                        std::uint64_t session_id = kSessionId) {
  mir2::LogicCommand command;
  command.kind = kind;
  command.session_id = session_id;
  command.target_actor_id = merchant_id;
  command.item_make_index = make_index;
  command.text = std::move(item_name);
  return command;
}

mir2::RuntimeDispatch route_storage_command(mir2::LogicRuntime& runtime,
                                            const mir2::LogicCommand& command,
                                            std::uint16_t expected_ident) {
  static_cast<void>(runtime.route_logic_command(command));
  mir2::RuntimeDispatch dispatch;
  for (int i = 0; i < 32; ++i) {
    dispatch = runtime.tick();
    if (find_packet(dispatch, expected_ident, command.session_id).has_value()) {
      break;
    }
  }
  return dispatch;
}

std::optional<std::uint64_t> find_storage_merchant(mir2::LogicRuntime& runtime,
                                                   mir2::RuntimeDispatch* opened_dispatch = nullptr,
                                                   std::uint64_t session_id = kSessionId) {
  for (std::uint64_t candidate = 1; candidate <= 100; ++candidate) {
    mir2::LogicCommand click;
    click.kind = mir2::LogicCommandKind::click_npc;
    click.session_id = session_id;
    click.target_actor_id = candidate;
    static_cast<void>(runtime.route_logic_command(click));
    for (int tick = 0; tick < 16; ++tick) {
      const auto dispatch = runtime.tick();
      const auto storage_menu = find_packet(dispatch, mir2::kSmSendUserStorageItem, session_id);
      const auto storage_list = find_packet(dispatch, mir2::kSmSaveItemList, session_id);
      if (storage_menu.has_value() && storage_list.has_value() &&
          storage_menu->message.recog == storage_list->message.recog &&
          storage_list->message.recog > 0) {
        if (opened_dispatch != nullptr) {
          *opened_dispatch = dispatch;
        }
        return static_cast<std::uint64_t>(storage_list->message.recog);
      }
    }
  }
  return std::nullopt;
}

mir2::RuntimeDispatch query_storage_items(mir2::LogicRuntime& runtime, std::uint64_t merchant_id) {
  mir2::LogicCommand query;
  query.kind = mir2::LogicCommandKind::query_storage_items;
  query.session_id = kSessionId;
  query.target_actor_id = merchant_id;
  static_cast<void>(runtime.route_logic_command(query));
  mir2::RuntimeDispatch dispatch;
  for (int i = 0; i < 32; ++i) {
    dispatch = runtime.tick();
    if (find_packet(dispatch, mir2::kSmSaveItemList, kSessionId).has_value()) {
      break;
    }
  }
  return dispatch;
}

const mir2::PersistRequest* find_save_request(const mir2::RuntimeDispatch& dispatch) {
  for (const auto& request : dispatch.persist_requests) {
    if (request.kind == mir2::PersistRequestKind::save_character) {
      return &request;
    }
  }
  return nullptr;
}

template <std::size_t N>
const mir2::LegacyUserItem* find_user_item(
    const std::array<mir2::LegacyUserItem, N>& items, std::int32_t make_index) {
  const auto it = std::find_if(items.begin(), items.end(),
                               [&](const mir2::LegacyUserItem& item) {
                                 return item.make_index == make_index;
                               });
  return it != items.end() ? &*it : nullptr;
}

template <std::size_t N>
std::size_t non_empty_count(const std::array<mir2::LegacyUserItem, N>& items) {
  return static_cast<std::size_t>(std::count_if(
      items.begin(), items.end(), [](const mir2::LegacyUserItem& item) {
        return !mir2::is_empty(item);
      }));
}

bool storage_list_encoding_skips_empty_slots_and_keeps_array_order() {
  auto config = make_host_config();
  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  auto character = make_character();
  character.storage_items[0] = make_user_item(1, 1001);
  character.storage_items[2] = make_user_item(2, 1002);
  character.storage_items[4] = make_user_item(3, 1003);
  if (!enter_world(runtime, character)) {
    return false;
  }

  mir2::RuntimeDispatch opened;
  const auto merchant_id = find_storage_merchant(runtime, &opened);
  const auto storage_list = find_packet(opened, mir2::kSmSaveItemList, kSessionId);
  if (!merchant_id.has_value() || !storage_list.has_value() ||
      storage_list->message.recog != static_cast<std::int32_t>(*merchant_id) ||
      storage_list->message.series != 3 ||
      std::count(storage_list->body.begin(), storage_list->body.end(), '/') != 3) {
    return false;
  }

  const auto items = decode_storage_items(storage_list->body);
  return items.size() == 3 && items[0].make_index == 1001 && items[1].make_index == 1002 &&
         items[2].make_index == 1003 && mir2::to_string(items[0].item.name) == "Storage Sword" &&
         mir2::to_string(items[1].item.name) == "Storage Ring" &&
         mir2::to_string(items[2].item.name) == "Storage Potion";
}

bool storage_blob_round_trips_fifty_slots() {
  static_assert(sizeof(std::array<mir2::LegacyUserItem, mir2::kMaxSaveItems>) ==
                mir2::kMaxSaveItems * sizeof(mir2::LegacyUserItem));

  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto temp_root =
      std::filesystem::temp_directory_path() / "mir2_storage_legacy_characterization_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root, ignored);

  mir2::Repository repository(temp_root / "mir2.sqlite");
  repository.ensure_schema(source_root / "schema" / "mir2.sql");
  repository.seed_runtime();

  auto character = make_character();
  character.character_name = "StorageBlob";
  character.storage_items[0] = make_user_item(1, 2001);
  character.storage_items[10] = make_user_item(2, 2010);
  character.storage_items[mir2::kMaxSaveItems - 1] = make_user_item(3, 2049);
  if (!repository.create_character(character)) {
    return false;
  }

  const auto loaded = repository.load_character(character.account_id, character.character_name);
  return loaded.has_value() &&
         std::memcmp(character.storage_items.data(), loaded->storage_items.data(),
                     character.storage_items.size() * sizeof(mir2::LegacyUserItem)) == 0;
}

bool storage_npc_uses_rectangle_interaction_range() {
  {
    auto config = make_host_config(35, 35);
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    if (!enter_world(runtime, make_character(20, 20))) {
      return false;
    }
    if (!find_storage_merchant(runtime).has_value()) {
      return false;
    }
  }

  {
    auto config = make_host_config(36, 20);
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    if (!enter_world(runtime, make_character(20, 20))) {
      return false;
    }
    if (find_storage_merchant(runtime).has_value()) {
      return false;
    }
  }

  return true;
}

bool simple_deposit_withdraw_preserves_user_item_bytes() {
  auto config = make_host_config();
  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  auto character = make_character();
  const auto original = make_user_item(1, 3001);
  character.bag_items[0] = original;
  if (!enter_world(runtime, character)) {
    return false;
  }

  const auto merchant_id = find_storage_merchant(runtime);
  if (!merchant_id.has_value()) {
    return false;
  }

  const auto deposit_dispatch =
      route_storage_command(runtime,
                            make_storage_command(mir2::LogicCommandKind::storage_item,
                                                 *merchant_id, original.make_index, "Storage Sword"),
                            mir2::kSmStorageOk);
  if (!find_packet(deposit_dispatch, mir2::kSmStorageOk, kSessionId).has_value()) {
    std::cerr << "deposit ok packet missing\n";
    return false;
  }

  const auto* deposit_save = find_save_request(deposit_dispatch);
  const auto* stored = deposit_save != nullptr
                           ? find_user_item(deposit_save->character.storage_items,
                                           original.make_index)
                           : nullptr;
  if (stored == nullptr || !same_user_item_bytes(*stored, original)) {
    std::cerr << "deposit save item mismatch: has_save=" << (deposit_save != nullptr)
              << " has_stored=" << (stored != nullptr) << "\n";
    return false;
  }

  const auto withdraw_dispatch = route_storage_command(
      runtime,
      make_storage_command(mir2::LogicCommandKind::take_back_storage_item, *merchant_id,
                           original.make_index, "Storage Sword"),
      mir2::kSmTakeBackStorageItemOk);
  const auto ok = find_packet(withdraw_dispatch, mir2::kSmTakeBackStorageItemOk, kSessionId);
  if (!ok.has_value() || ok->message.recog != original.make_index) {
    std::cerr << "withdraw packet expectation failed: has_ok=" << ok.has_value();
    if (ok.has_value()) {
      std::cerr << " recog=" << ok->message.recog;
    }
    std::cerr << "\n";
    return false;
  }

  const auto* withdraw_save = find_save_request(withdraw_dispatch);
  const auto* bag_item = withdraw_save != nullptr
                             ? find_user_item(withdraw_save->character.bag_items,
                                              original.make_index)
                             : nullptr;
  if (bag_item == nullptr || !same_user_item_bytes(*bag_item, original)) {
    std::cerr << "withdraw save item mismatch: has_save=" << (withdraw_save != nullptr)
              << " has_bag_item=" << (bag_item != nullptr) << "\n";
  }
  return bag_item != nullptr && same_user_item_bytes(*bag_item, original);
}

bool storage_capacity_limit_returns_full_without_moving_bag_item() {
  auto config = make_host_config();
  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  auto character = make_character();
  for (std::size_t i = 0; i < mir2::kRuntimeMaxStorageItems; ++i) {
    character.storage_items[i] = make_user_item(1, 4000 + static_cast<std::int32_t>(i));
  }
  const auto bag_item = make_user_item(1, 4999);
  character.bag_items[0] = bag_item;
  if (!enter_world(runtime, character)) {
    return false;
  }

  const auto merchant_id = find_storage_merchant(runtime);
  if (!merchant_id.has_value()) {
    return false;
  }

  const auto dispatch =
      route_storage_command(runtime,
                            make_storage_command(mir2::LogicCommandKind::storage_item,
                                                 *merchant_id, bag_item.make_index,
                                                 "Storage Sword"),
                            mir2::kSmStorageFull);
  if (!find_packet(dispatch, mir2::kSmStorageFull, kSessionId).has_value() ||
      find_packet(dispatch, mir2::kSmStorageOk, kSessionId).has_value() ||
      find_save_request(dispatch) != nullptr) {
    return false;
  }

  const auto snapshot = runtime.snapshot_character_actor(character.character_name);
  if (!snapshot.has_value() ||
      non_empty_count(snapshot->storage_items) != mir2::kRuntimeMaxStorageItems) {
    return false;
  }
  const auto* remaining_bag_item = find_user_item(snapshot->bag_items, bag_item.make_index);
  return remaining_bag_item != nullptr &&
         same_user_item_bytes(*remaining_bag_item, bag_item) &&
         find_user_item(snapshot->storage_items, bag_item.make_index) == nullptr;
}

bool overweight_withdraw_fails_without_moving_storage_item() {
  auto config = make_host_config();
  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  auto character = make_character();
  character.ability.max_weight = 0;
  const auto stored = make_user_item(1, 5101);
  character.storage_items[0] = stored;
  if (!enter_world(runtime, character)) {
    return false;
  }

  const auto merchant_id = find_storage_merchant(runtime);
  if (!merchant_id.has_value()) {
    return false;
  }

  const auto dispatch = route_storage_command(
      runtime,
      make_storage_command(mir2::LogicCommandKind::take_back_storage_item, *merchant_id,
                           stored.make_index, "Storage Sword"),
      mir2::kSmTakeBackStorageItemFail);
  if (!find_packet(dispatch, mir2::kSmTakeBackStorageItemFail, kSessionId).has_value() ||
      find_packet(dispatch, mir2::kSmTakeBackStorageItemFullBag, kSessionId).has_value() ||
      find_packet(dispatch, mir2::kSmTakeBackStorageItemOk, kSessionId).has_value() ||
      find_save_request(dispatch) != nullptr) {
    return false;
  }

  const auto snapshot = runtime.snapshot_character_actor(character.character_name);
  const auto* remaining = snapshot.has_value()
                              ? find_user_item(snapshot->storage_items, stored.make_index)
                              : nullptr;
  return remaining != nullptr && same_user_item_bytes(*remaining, stored) &&
         find_user_item(snapshot->bag_items, stored.make_index) == nullptr;
}

bool full_bag_withdraw_returns_fullbag_without_moving_storage_item() {
  auto config = make_host_config();
  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  auto character = make_character();
  const auto stored = make_user_item(1, 5201);
  character.storage_items[0] = stored;
  for (std::size_t i = 0; i < mir2::kMaxBagItems; ++i) {
    character.bag_items[i] = make_user_item(2, 5202 + static_cast<std::int32_t>(i));
  }
  if (!enter_world(runtime, character)) {
    return false;
  }

  const auto merchant_id = find_storage_merchant(runtime);
  if (!merchant_id.has_value()) {
    return false;
  }

  const auto dispatch = route_storage_command(
      runtime,
      make_storage_command(mir2::LogicCommandKind::take_back_storage_item, *merchant_id,
                           stored.make_index, "Storage Sword"),
      mir2::kSmTakeBackStorageItemFullBag);
  if (!find_packet(dispatch, mir2::kSmTakeBackStorageItemFullBag, kSessionId).has_value() ||
      find_packet(dispatch, mir2::kSmTakeBackStorageItemOk, kSessionId).has_value() ||
      find_save_request(dispatch) != nullptr) {
    return false;
  }

  const auto snapshot = runtime.snapshot_character_actor(character.character_name);
  const auto* remaining = snapshot.has_value()
                              ? find_user_item(snapshot->storage_items, stored.make_index)
                              : nullptr;
  return remaining != nullptr && same_user_item_bytes(*remaining, stored) &&
         find_user_item(snapshot->bag_items, stored.make_index) == nullptr;
}

bool storage_delete_compacts_before_next_deposit() {
  auto config = make_host_config();
  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  auto character = make_character();
  character.storage_items[0] = make_user_item(1, 5301);
  character.storage_items[1] = make_user_item(2, 5302);
  character.storage_items[2] = make_user_item(3, 5303);
  character.bag_items[0] = make_user_item(4, 5304);
  if (!enter_world(runtime, character)) {
    return false;
  }

  const auto merchant_id = find_storage_merchant(runtime);
  if (!merchant_id.has_value()) {
    return false;
  }
  static_cast<void>(route_storage_command(
      runtime,
      make_storage_command(mir2::LogicCommandKind::take_back_storage_item, *merchant_id, 5302,
                           "Storage Ring"),
      mir2::kSmTakeBackStorageItemOk));
  static_cast<void>(route_storage_command(
      runtime,
      make_storage_command(mir2::LogicCommandKind::storage_item, *merchant_id, 5304,
                           "Storage Gem"),
      mir2::kSmStorageOk));

  const auto list_dispatch = query_storage_items(runtime, *merchant_id);
  const auto list = find_packet(list_dispatch, mir2::kSmSaveItemList, kSessionId);
  const auto items = list.has_value() ? decode_storage_items(list->body)
                                      : std::vector<mir2::LegacyClientItem>{};
  return items.size() == 3 && items[0].make_index == 5301 && items[1].make_index == 5303 &&
         items[2].make_index == 5304;
}

bool storage_item_name_matching_is_case_insensitive() {
  auto config = make_host_config();
  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  auto character = make_character();
  character.bag_items[0] = make_user_item(1, 5401);
  if (!enter_world(runtime, character)) {
    return false;
  }

  const auto merchant_id = find_storage_merchant(runtime);
  if (!merchant_id.has_value()) {
    return false;
  }
  const auto dispatch =
      route_storage_command(runtime,
                            make_storage_command(mir2::LogicCommandKind::storage_item,
                                                 *merchant_id, 5401, "storage sword"),
                            mir2::kSmStorageOk);
  return find_packet(dispatch, mir2::kSmStorageOk, kSessionId).has_value() &&
         find_save_request(dispatch) != nullptr;
}

bool storage_empty_name_is_not_makeindex_wildcard() {
  auto config = make_host_config();
  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  auto character = make_character();
  const auto bag_item = make_user_item(1, 5501);
  character.bag_items[0] = bag_item;
  if (!enter_world(runtime, character)) {
    return false;
  }

  const auto merchant_id = find_storage_merchant(runtime);
  if (!merchant_id.has_value()) {
    return false;
  }
  const auto dispatch =
      route_storage_command(runtime,
                            make_storage_command(mir2::LogicCommandKind::storage_item,
                                                 *merchant_id, bag_item.make_index, ""),
                            mir2::kSmStorageFail);
  if (!find_packet(dispatch, mir2::kSmStorageFail, kSessionId).has_value() ||
      find_packet(dispatch, mir2::kSmStorageOk, kSessionId).has_value() ||
      find_save_request(dispatch) != nullptr) {
    return false;
  }

  const auto snapshot = runtime.snapshot_character_actor(character.character_name);
  const auto* remaining_bag_item = snapshot.has_value()
                                       ? find_user_item(snapshot->bag_items, bag_item.make_index)
                                       : nullptr;
  return remaining_bag_item != nullptr &&
         same_user_item_bytes(*remaining_bag_item, bag_item) &&
         find_user_item(snapshot->storage_items, bag_item.make_index) == nullptr;
}

bool stdmode51_storage_deposit_fails_without_moving_bag_item() {
  auto config = make_host_config(11, 10, 51);
  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  auto character = make_character();
  const auto event_item = make_user_item(1, 5601);
  character.bag_items[0] = event_item;
  if (!enter_world(runtime, character)) {
    return false;
  }

  const auto merchant_id = find_storage_merchant(runtime);
  if (!merchant_id.has_value()) {
    return false;
  }
  const auto dispatch =
      route_storage_command(runtime,
                            make_storage_command(mir2::LogicCommandKind::storage_item,
                                                 *merchant_id, event_item.make_index,
                                                 "Storage Sword"),
                            mir2::kSmStorageFail);
  if (!find_packet(dispatch, mir2::kSmStorageFail, kSessionId).has_value() ||
      find_packet(dispatch, mir2::kSmStorageOk, kSessionId).has_value() ||
      find_save_request(dispatch) != nullptr) {
    return false;
  }

  const auto snapshot = runtime.snapshot_character_actor(character.character_name);
  const auto* remaining_bag_item = snapshot.has_value()
                                       ? find_user_item(snapshot->bag_items, event_item.make_index)
                                       : nullptr;
  return remaining_bag_item != nullptr &&
         same_user_item_bytes(*remaining_bag_item, event_item) &&
         find_user_item(snapshot->storage_items, event_item.make_index) == nullptr;
}

bool existing_stdmode51_storage_item_can_be_withdrawn() {
  auto config = make_host_config(11, 10, 51);
  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  auto character = make_character();
  const auto event_item = make_user_item(1, 5701);
  character.storage_items[0] = event_item;
  if (!enter_world(runtime, character)) {
    return false;
  }

  const auto merchant_id = find_storage_merchant(runtime);
  if (!merchant_id.has_value()) {
    return false;
  }
  const auto dispatch = route_storage_command(
      runtime,
      make_storage_command(mir2::LogicCommandKind::take_back_storage_item, *merchant_id,
                           event_item.make_index, "Storage Sword"),
      mir2::kSmTakeBackStorageItemOk);
  const auto ok = find_packet(dispatch, mir2::kSmTakeBackStorageItemOk, kSessionId);
  if (!ok.has_value() || ok->message.recog != event_item.make_index) {
    return false;
  }

  const auto* save = find_save_request(dispatch);
  const auto* bag_item = save != nullptr
                             ? find_user_item(save->character.bag_items,
                                              event_item.make_index)
                             : nullptr;
  return bag_item != nullptr && same_user_item_bytes(*bag_item, event_item) &&
         find_user_item(save->character.storage_items, event_item.make_index) == nullptr;
}

bool approval_mode_storage_deposit_fails_without_moving_bag_item() {
  auto config = make_host_config(11, 10, 5, true);
  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  auto character = make_character();
  const auto bag_item = make_user_item(1, 5801);
  character.bag_items[0] = bag_item;
  if (!enter_world(runtime, character)) {
    return false;
  }

  const auto merchant_id = find_storage_merchant(runtime);
  if (!merchant_id.has_value()) {
    return false;
  }
  const auto dispatch =
      route_storage_command(runtime,
                            make_storage_command(mir2::LogicCommandKind::storage_item,
                                                 *merchant_id, bag_item.make_index,
                                                 "Storage Sword"),
                            mir2::kSmStorageFail);
  if (!find_packet(dispatch, mir2::kSmStorageFail, kSessionId).has_value() ||
      find_packet(dispatch, mir2::kSmStorageOk, kSessionId).has_value() ||
      find_save_request(dispatch) != nullptr) {
    return false;
  }

  const auto snapshot = runtime.snapshot_character_actor(character.character_name);
  const auto* remaining_bag_item = snapshot.has_value()
                                       ? find_user_item(snapshot->bag_items, bag_item.make_index)
                                       : nullptr;
  return remaining_bag_item != nullptr &&
         same_user_item_bytes(*remaining_bag_item, bag_item) &&
         find_user_item(snapshot->storage_items, bag_item.make_index) == nullptr;
}

bool approval_mode_storage_withdraw_fails_without_moving_storage_item() {
  auto config = make_host_config(11, 10, 5, true);
  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  auto character = make_character();
  const auto stored = make_user_item(1, 5901);
  character.storage_items[0] = stored;
  if (!enter_world(runtime, character)) {
    return false;
  }

  const auto merchant_id = find_storage_merchant(runtime);
  if (!merchant_id.has_value()) {
    return false;
  }
  const auto dispatch = route_storage_command(
      runtime,
      make_storage_command(mir2::LogicCommandKind::take_back_storage_item, *merchant_id,
                           stored.make_index, "Storage Sword"),
      mir2::kSmTakeBackStorageItemFail);
  if (!find_packet(dispatch, mir2::kSmTakeBackStorageItemFail, kSessionId).has_value() ||
      find_packet(dispatch, mir2::kSmTakeBackStorageItemOk, kSessionId).has_value() ||
      find_packet(dispatch, mir2::kSmTakeBackStorageItemFullBag, kSessionId).has_value() ||
      find_save_request(dispatch) != nullptr) {
    return false;
  }

  const auto snapshot = runtime.snapshot_character_actor(character.character_name);
  const auto* remaining = snapshot.has_value()
                              ? find_user_item(snapshot->storage_items, stored.make_index)
                              : nullptr;
  return remaining != nullptr && same_user_item_bytes(*remaining, stored) &&
         find_user_item(snapshot->bag_items, stored.make_index) == nullptr;
}

}  // namespace

int main() {
  if (!storage_list_encoding_skips_empty_slots_and_keeps_array_order()) {
    std::cerr << "storage_list_encoding_skips_empty_slots_and_keeps_array_order failed\n";
    return 1;
  }
  if (!storage_blob_round_trips_fifty_slots()) {
    std::cerr << "storage_blob_round_trips_fifty_slots failed\n";
    return 1;
  }
  if (!storage_npc_uses_rectangle_interaction_range()) {
    std::cerr << "storage_npc_uses_rectangle_interaction_range failed\n";
    return 1;
  }
  if (!simple_deposit_withdraw_preserves_user_item_bytes()) {
    std::cerr << "simple_deposit_withdraw_preserves_user_item_bytes failed\n";
    return 1;
  }
  if (!storage_capacity_limit_returns_full_without_moving_bag_item()) {
    std::cerr << "storage_capacity_limit_returns_full_without_moving_bag_item failed\n";
    return 1;
  }
  if (!overweight_withdraw_fails_without_moving_storage_item()) {
    std::cerr << "overweight_withdraw_fails_without_moving_storage_item failed\n";
    return 1;
  }
  if (!full_bag_withdraw_returns_fullbag_without_moving_storage_item()) {
    std::cerr << "full_bag_withdraw_returns_fullbag_without_moving_storage_item failed\n";
    return 1;
  }
  if (!storage_delete_compacts_before_next_deposit()) {
    std::cerr << "storage_delete_compacts_before_next_deposit failed\n";
    return 1;
  }
  if (!storage_item_name_matching_is_case_insensitive()) {
    std::cerr << "storage_item_name_matching_is_case_insensitive failed\n";
    return 1;
  }
  if (!storage_empty_name_is_not_makeindex_wildcard()) {
    std::cerr << "storage_empty_name_is_not_makeindex_wildcard failed\n";
    return 1;
  }
  if (!stdmode51_storage_deposit_fails_without_moving_bag_item()) {
    std::cerr << "stdmode51_storage_deposit_fails_without_moving_bag_item failed\n";
    return 1;
  }
  if (!existing_stdmode51_storage_item_can_be_withdrawn()) {
    std::cerr << "existing_stdmode51_storage_item_can_be_withdrawn failed\n";
    return 1;
  }
  if (!approval_mode_storage_deposit_fails_without_moving_bag_item()) {
    std::cerr << "approval_mode_storage_deposit_fails_without_moving_bag_item failed\n";
    return 1;
  }
  if (!approval_mode_storage_withdraw_fails_without_moving_storage_item()) {
    std::cerr << "approval_mode_storage_withdraw_fails_without_moving_storage_item failed\n";
    return 1;
  }
  return 0;
}
