#include <algorithm>
#include <cstdint>
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

constexpr int kLegacyTradeStableMs = 1000;
constexpr int kTestTickMs = 10;
constexpr int kTradeStableSafetyTicks = 10;
constexpr int kTradeStableTicks =
    (kLegacyTradeStableMs + kTestTickMs - 1) / kTestTickMs + kTradeStableSafetyTicks;

int fail(int stage) {
  return stage;
}

void append_dispatch(mir2::RuntimeDispatch& target, mir2::RuntimeDispatch source) {
  target.session_events.insert(target.session_events.end(),
                               std::make_move_iterator(source.session_events.begin()),
                               std::make_move_iterator(source.session_events.end()));
}

mir2::RuntimeDispatch tick_players(mir2::LogicRuntime& runtime, int count = 30) {
  mir2::RuntimeDispatch dispatch;
  for (int i = 0; i < count; ++i) {
    append_dispatch(dispatch, runtime.tick());
  }
  return dispatch;
}

mir2::RuntimeDispatch tick_past_trade_stable_window(mir2::LogicRuntime& runtime) {
  return tick_players(runtime, kTradeStableTicks);
}

mir2::LegacyUserItem make_item(std::int32_t index, std::int32_t make_index) {
  mir2::LegacyUserItem item;
  item.index = static_cast<std::uint16_t>(index);
  item.make_index = make_index;
  item.dura = 1000;
  item.dura_max = 1000;
  return item;
}

mir2::HostConfig make_config() {
  mir2::HostConfig config;
  config.budgets.tick_ms = kTestTickMs;
  config.maps.push_back(mir2::MapConfig{"0", "TradeMap", {}, 0, 0, 30, 30});
  config.items.push_back(mir2::ItemConfig{1, "Ruby", 1, 40, 0, 2, 1, 1000, 10, 0, 0});
  config.items.push_back(mir2::ItemConfig{2, "Sapphire", 1, 41, 0, 3, 1, 1000, 10, 0, 0});
  return config;
}

mir2::CharacterRecord make_character(std::string account, std::string name,
                                     std::int32_t x, std::uint8_t dir,
                                     std::int32_t gold, mir2::LegacyUserItem item) {
  mir2::CharacterRecord character;
  character.account_id = std::move(account);
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = x;
  character.y = 10;
  character.dir = dir;
  character.gold = gold;
  character.ability.level = 30;
  character.ability.max_hp = 50;
  character.ability.max_mp = 50;
  character.ability.max_exp = 100;
  character.ability.max_weight = 100;
  character.bag_items[0] = item;
  return character;
}

mir2::LogicCommand enter_command(std::uint64_t session_id, const mir2::CharacterRecord& character) {
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

mir2::LogicCommand trade_command(mir2::LogicCommandKind kind, std::uint64_t session_id,
                                 std::int32_t make_index = 0, std::string text = {},
                                 std::int32_t amount = 0) {
  mir2::LogicCommand command;
  command.kind = kind;
  command.session_id = session_id;
  command.item_make_index = make_index;
  command.text = std::move(text);
  command.amount = amount;
  return command;
}

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

std::vector<mir2::LegacyClientItem> query_bag(mir2::LogicRuntime& runtime,
                                              std::uint64_t session_id) {
  mir2::LogicCommand query;
  query.kind = mir2::LogicCommandKind::query_bag_items;
  query.session_id = session_id;
  static_cast<void>(runtime.route_logic_command(query));
  const auto dispatch = tick_players(runtime);
  const auto packet = find_packet(dispatch, session_id, mir2::kSmBagItems);
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

void enter_pair(mir2::LogicRuntime& runtime) {
  const auto hero_a = make_character("guest_a", "HeroA", 10, 2, 100, make_item(1, 1001));
  const auto hero_b = make_character("guest_b", "HeroB", 11, 6, 50, make_item(2, 2001));
  static_cast<void>(runtime.route_logic_command(enter_command(7, hero_a)));
  static_cast<void>(runtime.route_logic_command(enter_command(8, hero_b)));
  static_cast<void>(runtime.tick());
}

void open_trade(mir2::LogicRuntime& runtime) {
  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_try, 7, 0, "HeroB")));
  static_cast<void>(tick_players(runtime));
}

int count_make_index(const std::vector<mir2::LegacyClientItem>& items, std::int32_t make_index) {
  return static_cast<int>(std::count_if(items.begin(), items.end(), [&](const auto& item) {
    return item.make_index == make_index;
  }));
}

bool has_gold(const mir2::LogicRuntime& runtime, std::string_view name, std::int32_t gold) {
  const auto snapshot = runtime.snapshot_character_actor(std::string(name));
  return snapshot.has_value() && snapshot->gold == gold;
}

int count_snapshot_bag_make_index(const mir2::CharacterRecord& character,
                                  std::int32_t make_index) {
  return static_cast<int>(std::count_if(character.bag_items.begin(), character.bag_items.end(),
                                        [&](const auto& item) {
                                          return item.make_index == make_index;
                                        }));
}

int count_snapshot_bag_items(const mir2::CharacterRecord& character) {
  return static_cast<int>(std::count_if(character.bag_items.begin(), character.bag_items.end(),
                                        [](const auto& item) {
                                          return item.index != 0 || item.make_index != 0;
                                        }));
}

int run_duplicate_accept_case() {
  mir2::LogicRuntime runtime(make_config());
  runtime.initialize();
  enter_pair(runtime);
  open_trade(runtime);

  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_add_item, 7, 1001, "Ruby")));
  static_cast<void>(tick_players(runtime));
  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_set_gold, 8, 0, {}, 7)));
  static_cast<void>(tick_players(runtime));
  static_cast<void>(tick_past_trade_stable_window(runtime));

  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_accept, 7)));
  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_accept, 8)));
  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_accept, 7)));
  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_accept, 8)));
  static_cast<void>(tick_players(runtime));

  const auto bag_a = query_bag(runtime, 7);
  const auto bag_b = query_bag(runtime, 8);
  if (!has_gold(runtime, "HeroA", 107) || !has_gold(runtime, "HeroB", 43) ||
      count_make_index(bag_a, 1001) != 0 || count_make_index(bag_b, 1001) != 1 ||
      count_make_index(bag_b, 2001) != 1) {
    return fail(1);
  }
  return 0;
}

int run_receiver_storage_duplicate_cancel_case() {
  mir2::LogicRuntime runtime(make_config());
  runtime.initialize();
  const auto hero_a = make_character("guest_a", "HeroA", 10, 2, 100, make_item(1, 1001));
  auto hero_b = make_character("guest_b", "HeroB", 11, 6, 50, make_item(2, 2001));
  hero_b.storage_items[0] = make_item(1, 1001);
  static_cast<void>(runtime.route_logic_command(enter_command(7, hero_a)));
  static_cast<void>(runtime.route_logic_command(enter_command(8, hero_b)));
  static_cast<void>(runtime.tick());
  open_trade(runtime);

  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_add_item, 7, 1001, "Ruby")));
  static_cast<void>(tick_players(runtime));
  static_cast<void>(tick_past_trade_stable_window(runtime));

  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_accept, 7)));
  static_cast<void>(tick_players(runtime));
  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_accept, 8)));
  const auto dispatch = tick_players(runtime);

  const auto bag_a = query_bag(runtime, 7);
  const auto bag_b = query_bag(runtime, 8);
  const auto snapshot_b = runtime.snapshot_character_actor("HeroB");
  if (!find_packet(dispatch, 7, mir2::kSmDealCancel).has_value() ||
      !find_packet(dispatch, 8, mir2::kSmDealCancel).has_value() ||
      find_packet(dispatch, 7, mir2::kSmDealSuccess).has_value() ||
      find_packet(dispatch, 8, mir2::kSmDealSuccess).has_value() ||
      !has_gold(runtime, "HeroA", 100) || !has_gold(runtime, "HeroB", 50) ||
      count_make_index(bag_a, 1001) != 1 || count_make_index(bag_b, 2001) != 1 ||
      !snapshot_b.has_value() || snapshot_b->storage_items[0].make_index != 1001) {
    return fail(4);
  }

  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_accept, 7)));
  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_accept, 8)));
  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_cancel, 7)));
  const auto replay_dispatch = tick_players(runtime);
  const auto replay_snapshot_a = runtime.snapshot_character_actor("HeroA");
  const auto replay_snapshot_b = runtime.snapshot_character_actor("HeroB");
  if (find_packet(replay_dispatch, 7, mir2::kSmDealSuccess).has_value() ||
      find_packet(replay_dispatch, 8, mir2::kSmDealSuccess).has_value() ||
      find_packet(replay_dispatch, 7, mir2::kSmAddItem).has_value() ||
      find_packet(replay_dispatch, 8, mir2::kSmAddItem).has_value() ||
      !has_gold(runtime, "HeroA", 100) || !has_gold(runtime, "HeroB", 50) ||
      !replay_snapshot_a.has_value() || !replay_snapshot_b.has_value() ||
      count_snapshot_bag_make_index(*replay_snapshot_a, 1001) != 1 ||
      count_snapshot_bag_items(*replay_snapshot_a) != 1 ||
      count_snapshot_bag_make_index(*replay_snapshot_b, 2001) != 1 ||
      count_snapshot_bag_items(*replay_snapshot_b) != 1 ||
      replay_snapshot_b->storage_items[0].make_index != 1001) {
    return fail(5);
  }
  return 0;
}

int run_cancel_accept_same_frame_case() {
  mir2::LogicRuntime runtime(make_config());
  runtime.initialize();
  enter_pair(runtime);
  open_trade(runtime);

  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_add_item, 7, 1001, "Ruby")));
  static_cast<void>(tick_players(runtime));
  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_set_gold, 7, 0, {}, 5)));
  static_cast<void>(tick_players(runtime));

  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_cancel, 7)));
  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_accept, 8)));
  const auto dispatch = tick_players(runtime);
  if (!find_packet(dispatch, 7, mir2::kSmDealCancel).has_value() ||
      !find_packet(dispatch, 8, mir2::kSmDealCancel).has_value()) {
    return fail(2);
  }

  const auto bag_a = query_bag(runtime, 7);
  const auto bag_b = query_bag(runtime, 8);
  if (!has_gold(runtime, "HeroA", 100) || !has_gold(runtime, "HeroB", 50) ||
      count_make_index(bag_a, 1001) != 1 || count_make_index(bag_b, 1001) != 0 ||
      count_make_index(bag_b, 2001) != 1) {
    return fail(3);
  }
  return 0;
}

}  // namespace

int main() {
  if (const auto result = run_duplicate_accept_case(); result != 0) {
    return result;
  }
  if (const auto result = run_receiver_storage_duplicate_cancel_case(); result != 0) {
    return result + 10;
  }
  if (const auto result = run_cancel_accept_same_frame_case(); result != 0) {
    return result + 20;
  }
  return 0;
}
