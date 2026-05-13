#include <algorithm>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "util/string_utils.hpp"
#include "world/logic_runtime.hpp"

namespace {

int fail(int stage) {
  std::cerr << "npc_shop_interleaving_fuzz_smoke failed at " << stage << '\n';
  return stage;
}

void append_dispatch(mir2::RuntimeDispatch& target, mir2::RuntimeDispatch source) {
  target.session_events.insert(target.session_events.end(),
                               std::make_move_iterator(source.session_events.begin()),
                               std::make_move_iterator(source.session_events.end()));
}

mir2::RuntimeDispatch tick_players(mir2::LogicRuntime& runtime, int count = 80) {
  mir2::RuntimeDispatch dispatch;
  for (int i = 0; i < count; ++i) {
    append_dispatch(dispatch, runtime.tick());
  }
  return dispatch;
}

mir2::HostConfig make_config(int potion_stock = 1) {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "NpcShopFuzzMap", {}, 0, 0, 20, 20});
  config.items.push_back(mir2::ItemConfig{1, "Potion", 1, 40, 0, 1, 1, 1000, -1, 0, 0});
  config.items.push_back(mir2::ItemConfig{2, "Fuzz Sword", 3, 90, 5, 1, 1, 1000, 1, 0, 0});
  std::vector<std::int32_t> products;
  for (int i = 0; i < potion_stock; ++i) {
    products.push_back(1);
  }
  config.npcs.push_back(mir2::NpcConfig{"merchant_1",
                                         "0",
                                         "Trader",
                                         11,
                                         10,
                                         "merchant_1.txt",
                                         "sell_repair",
                                         std::move(products),
                                         {},
                                         100,
                                         {5}});
  return config;
}

mir2::CharacterRecord make_character(std::int32_t gold, bool with_sword) {
  mir2::CharacterRecord character;
  character.account_id = "guest";
  character.character_name = "Hero";
  character.map_id = "0";
  character.x = 10;
  character.y = 10;
  character.gold = gold;
  character.ability.max_hp = 15;
  character.ability.max_mp = 10;
  character.ability.max_exp = 100;
  character.ability.max_weight = 30;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  if (with_sword) {
    character.bag_items[0].index = 2;
    character.bag_items[0].make_index = 1001;
    character.bag_items[0].dura = 600;
    character.bag_items[0].dura_max = 1000;
  }
  return character;
}

mir2::LogicCommand enter_command(const mir2::CharacterRecord& character) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = 7;
  command.account_id = character.account_id;
  command.character_name = character.character_name;
  command.map_id = character.map_id;
  command.x = character.x;
  command.y = character.y;
  command.character = character;
  return command;
}

mir2::LogicCommand shop_command(mir2::LogicCommandKind kind, std::uint64_t merchant_id,
                                std::int32_t make_index = 0, std::string text = {}) {
  mir2::LogicCommand command;
  command.kind = kind;
  command.session_id = 7;
  command.target_actor_id = merchant_id;
  command.item_make_index = make_index;
  command.text = std::move(text);
  return command;
}

std::optional<mir2::DecodedLegacyGamePacket> find_packet(
    const mir2::RuntimeDispatch& dispatch, std::uint16_t ident) {
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return decoded;
    }
  }
  return std::nullopt;
}

int count_packet(const mir2::RuntimeDispatch& dispatch, std::uint16_t ident) {
  return static_cast<int>(std::count_if(dispatch.session_events.begin(),
                                        dispatch.session_events.end(), [&](const auto& event) {
                                          const auto decoded =
                                              mir2::decode_legacy_game_packet(event.packet);
                                          return decoded.has_value() &&
                                                 decoded->message.ident == ident;
                                        }));
}

std::vector<mir2::LegacyClientItem> query_bag(mir2::LogicRuntime& runtime) {
  mir2::LogicCommand query;
  query.kind = mir2::LogicCommandKind::query_bag_items;
  query.session_id = 7;
  static_cast<void>(runtime.route_logic_command(query));
  const auto dispatch = tick_players(runtime);
  const auto packet = find_packet(dispatch, mir2::kSmBagItems);
  if (!packet.has_value()) {
    return {};
  }

  std::vector<mir2::LegacyClientItem> items;
  for (const auto& part : mir2::util::split(std::string(packet->body), '/')) {
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

int count_make_index(const std::vector<mir2::LegacyClientItem>& items,
                     std::int32_t make_index) {
  return static_cast<int>(std::count_if(items.begin(), items.end(), [&](const auto& item) {
    return item.make_index == make_index;
  }));
}

int count_item_name(const std::vector<mir2::LegacyClientItem>& items,
                    std::string_view name) {
  return static_cast<int>(std::count_if(items.begin(), items.end(), [&](const auto& item) {
    return mir2::to_string(item.item.name) == name;
  }));
}

bool has_gold(const mir2::LogicRuntime& runtime, std::int32_t gold) {
  const auto snapshot = runtime.snapshot_character_actor("Hero");
  return snapshot.has_value() && snapshot->gold == gold;
}

void enter_hero(mir2::LogicRuntime& runtime, std::int32_t gold, bool with_sword) {
  static_cast<void>(runtime.route_logic_command(enter_command(make_character(gold, with_sword))));
  static_cast<void>(tick_players(runtime));
}

int run_invalid_shop_packets_case() {
  mir2::LogicRuntime runtime(make_config(1));
  runtime.initialize();
  enter_hero(runtime, 80, true);
  if (count_make_index(query_bag(runtime), 1001) != 1) {
    return fail(5);
  }

  static_cast<void>(runtime.route_logic_command(
      shop_command(mir2::LogicCommandKind::merchant_select, 999, 0, "@buy")));
  static_cast<void>(runtime.route_logic_command(
      shop_command(mir2::LogicCommandKind::buy_item, 999, 1, "Potion")));
  static_cast<void>(runtime.route_logic_command(
      shop_command(mir2::LogicCommandKind::buy_item, 1, 1, "Wrong Potion")));
  static_cast<void>(runtime.route_logic_command(
      shop_command(mir2::LogicCommandKind::sell_item, 1, 9999, "Fuzz Sword")));
  static_cast<void>(runtime.route_logic_command(
      shop_command(mir2::LogicCommandKind::repair_item, 1, 9999, "Fuzz Sword")));
  const auto dispatch = tick_players(runtime);

  if (count_packet(dispatch, mir2::kSmBuyItemSuccess) != 0 ||
      count_packet(dispatch, mir2::kSmUserSellItemOk) != 0 ||
      count_packet(dispatch, mir2::kSmUserRepairItemOk) != 0 || !has_gold(runtime, 80)) {
    const auto snapshot = runtime.snapshot_character_actor("Hero");
    std::cerr << "invalid packet successes buy="
              << count_packet(dispatch, mir2::kSmBuyItemSuccess)
              << " sell=" << count_packet(dispatch, mir2::kSmUserSellItemOk)
              << " repair=" << count_packet(dispatch, mir2::kSmUserRepairItemOk)
              << " gold=" << (snapshot.has_value() ? snapshot->gold : -1) << '\n';
    return fail(1);
  }
  const auto bag = query_bag(runtime);
  if (count_make_index(bag, 1001) != 1 || count_item_name(bag, "Potion") != 0) {
    std::cerr << "invalid packet bag size=" << bag.size()
              << " sword_count=" << count_make_index(bag, 1001)
              << " potion_count=" << count_item_name(bag, "Potion") << '\n';
    return fail(2);
  }
  return 0;
}

int run_duplicate_buy_case() {
  mir2::LogicRuntime runtime(make_config(1));
  runtime.initialize();
  enter_hero(runtime, 80, false);

  static_cast<void>(runtime.route_logic_command(
      shop_command(mir2::LogicCommandKind::buy_item, 1, 1, "Potion")));
  static_cast<void>(runtime.route_logic_command(
      shop_command(mir2::LogicCommandKind::buy_item, 1, 1, "Potion")));
  const auto dispatch = tick_players(runtime);
  if (count_packet(dispatch, mir2::kSmBuyItemSuccess) != 1 ||
      count_packet(dispatch, mir2::kSmBuyItemFail) != 1 || !has_gold(runtime, 40)) {
    return fail(3);
  }
  if (count_item_name(query_bag(runtime), "Potion") != 1) {
    return fail(4);
  }
  return 0;
}

}  // namespace

int main() {
  if (const auto result = run_invalid_shop_packets_case(); result != 0) {
    return result;
  }
  if (const auto result = run_duplicate_buy_case(); result != 0) {
    return result + 10;
  }
  return 0;
}
