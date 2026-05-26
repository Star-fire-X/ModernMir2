#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

void append_dispatch(mir2::RuntimeDispatch& target, mir2::RuntimeDispatch source) {
  target.session_events.insert(target.session_events.end(),
                               std::make_move_iterator(source.session_events.begin()),
                               std::make_move_iterator(source.session_events.end()));
}

mir2::RuntimeDispatch tick_players(mir2::LogicRuntime& runtime) {
  mir2::RuntimeDispatch dispatch;
  for (int i = 0; i < 30; ++i) {
    append_dispatch(dispatch, runtime.tick());
  }
  return dispatch;
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

bool bag_has(const mir2::CharacterRecord& character, std::int32_t make_index) {
  return std::any_of(character.bag_items.begin(), character.bag_items.end(),
                     [&](const mir2::LegacyUserItem& item) {
                       return !mir2::is_empty(item) && item.make_index == make_index;
                     });
}

mir2::LogicCommand enter_command(const mir2::CharacterRecord& character,
                                 std::uint64_t session_id = 7) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = session_id;
  command.account_id = character.account_id;
  command.character_name = character.character_name;
  command.map_id = character.map_id;
  command.x = character.x;
  command.y = character.y;
  command.character = character;
  return command;
}

mir2::LogicCommand merchant_select(std::string action, std::uint64_t session_id = 7) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::merchant_select;
  command.session_id = session_id;
  command.target_actor_id = 1;
  command.text = std::move(action);
  return command;
}

mir2::LogicCommand item_command(mir2::LogicCommandKind kind, std::int32_t make_index,
                                std::string name) {
  mir2::LogicCommand command;
  command.kind = kind;
  command.session_id = 7;
  command.item_make_index = make_index;
  command.text = std::move(name);
  return command;
}

mir2::LogicCommand storage_command(std::int32_t make_index, std::string name) {
  auto command = item_command(mir2::LogicCommandKind::storage_item, make_index, std::move(name));
  command.target_actor_id = 1;
  return command;
}

mir2::LogicCommand walk_command(std::int32_t x, std::int32_t y) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::walk;
  command.session_id = 7;
  command.x = x;
  command.y = y;
  return command;
}

mir2::LogicCommand trade_try_command(std::uint64_t session_id) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::trade_try;
  command.session_id = session_id;
  return command;
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "InterlockMap", {}, 0, 0, 30, 30});
  config.items.push_back(mir2::ItemConfig{1, "Sell Sword", 3, 90, 5, 1, 1, 1000, 1, 0, 0});
  config.items.push_back(mir2::ItemConfig{2, "Potion", 1, 10, 0, 0, 1, 1, -1, 10, 0});
  config.npcs.push_back(mir2::NpcConfig{"merchant_1",
                                         "0",
                                         "Trader",
                                         11,
                                         10,
                                         "merchant_1.txt",
                                         "sell_repair_storage",
                                         {2},
                                         {},
                                         100,
                                         {5}});

  mir2::CharacterRecord hero;
  hero.account_id = "guest";
  hero.character_name = "Hero";
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;
  hero.ability.hp = 5;
  hero.ability.max_hp = 15;
  hero.ability.max_mp = 10;
  hero.ability.max_exp = 100;
  hero.ability.max_weight = 100;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;
  hero.bag_items[0] = mir2::LegacyUserItem{1001, 1, 600, 1000};
  hero.bag_items[1] = mir2::LegacyUserItem{1002, 2, 1, 1};

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  static_cast<void>(runtime.route_logic_command(enter_command(hero)));
  static_cast<void>(tick_players(runtime));

  static_cast<void>(runtime.route_logic_command(merchant_select("@sell")));
  static_cast<void>(tick_players(runtime));
  static_cast<void>(runtime.route_logic_command(
      item_command(mir2::LogicCommandKind::drop_item, 1001, "Sell Sword")));
  const auto sell_drop = tick_players(runtime);
  auto snapshot = runtime.snapshot_character_actor("Hero");
  if (!find_packet(sell_drop, mir2::kSmDropItemFail).has_value() ||
      find_packet(sell_drop, mir2::kSmDelItem).has_value() || !snapshot.has_value() ||
      !bag_has(*snapshot, 1001)) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(merchant_select("@storage")));
  static_cast<void>(tick_players(runtime));
  static_cast<void>(runtime.route_logic_command(
      item_command(mir2::LogicCommandKind::eat_item, 1002, "Potion")));
  const auto storage_eat = tick_players(runtime);
  snapshot = runtime.snapshot_character_actor("Hero");
  if (!find_packet(storage_eat, mir2::kSmEatFail).has_value() ||
      find_packet(storage_eat, mir2::kSmDelItem).has_value() || !snapshot.has_value() ||
      !bag_has(*snapshot, 1002)) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(merchant_select("@repair")));
  static_cast<void>(tick_players(runtime));
  static_cast<void>(runtime.route_logic_command(storage_command(1001, "Sell Sword")));
  const auto repair_storage = tick_players(runtime);
  snapshot = runtime.snapshot_character_actor("Hero");
  if (!find_packet(repair_storage, mir2::kSmStorageFail).has_value() ||
      find_packet(repair_storage, mir2::kSmDelItem).has_value() || !snapshot.has_value() ||
      !bag_has(*snapshot, 1001)) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(walk_command(9, 10)));
  static_cast<void>(tick_players(runtime));
  static_cast<void>(runtime.route_logic_command(
      item_command(mir2::LogicCommandKind::drop_item, 1001, "Sell Sword")));
  const auto drop_after_move = tick_players(runtime);
  snapshot = runtime.snapshot_character_actor("Hero");
  if (!find_packet(drop_after_move, mir2::kSmDelItem).has_value() || !snapshot.has_value() ||
      bag_has(*snapshot, 1001)) {
    return 1;
  }

  mir2::LogicRuntime trade_runtime(config);
  trade_runtime.initialize();
  auto target = hero;
  target.character_name = "Target";
  target.x = 10;
  target.y = 10;
  target.dir = 6;
  auto requester = hero;
  requester.account_id = "guest_peer";
  requester.character_name = "Requester";
  requester.x = 9;
  requester.y = 10;
  requester.dir = 2;
  static_cast<void>(trade_runtime.route_logic_command(enter_command(target, 7)));
  static_cast<void>(trade_runtime.route_logic_command(enter_command(requester, 8)));
  static_cast<void>(tick_players(trade_runtime));

  static_cast<void>(trade_runtime.route_logic_command(merchant_select("@storage", 7)));
  static_cast<void>(tick_players(trade_runtime));
  static_cast<void>(trade_runtime.route_logic_command(trade_try_command(8)));
  const auto target_modal_trade = tick_players(trade_runtime);
  if (!find_packet(target_modal_trade, mir2::kSmDealTryFail).has_value() ||
      find_packet(target_modal_trade, mir2::kSmDealMenu).has_value()) {
    return 1;
  }

  return 0;
}
