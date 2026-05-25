#include <cstdint>
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

struct TradeEvent {
  std::uint64_t session_id{0};
  std::uint16_t ident{0};
  std::int32_t recog{0};
  std::int32_t param{0};
  std::int32_t tag{0};
  std::int32_t item_make_index{0};
  std::string body_text{};
};

int fail(int stage) {
  return stage;
}

void append_dispatch(mir2::RuntimeDispatch& target, mir2::RuntimeDispatch source) {
  target.session_events.insert(target.session_events.end(), source.session_events.begin(),
                               source.session_events.end());
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

std::optional<mir2::LegacyClientItem> decode_client_item(std::string_view body) {
  mir2::LegacyClientItem item;
  if (!mir2::legacy_decode_buffer(body, &item, sizeof(item))) {
    return std::nullopt;
  }
  return item;
}

void collect_trade_events(const mir2::RuntimeDispatch& dispatch, std::vector<TradeEvent>& events) {
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (!decoded.has_value() || decoded->message.ident < mir2::kSmDealMenu ||
        decoded->message.ident > mir2::kSmDealSuccess) {
      continue;
    }
    TradeEvent trace;
    trace.session_id = event.session_id;
    trace.ident = decoded->message.ident;
    trace.recog = decoded->message.recog;
    trace.param = decoded->message.param;
    trace.tag = decoded->message.tag;
    if (trace.ident == mir2::kSmDealMenu) {
      trace.body_text = mir2::legacy_decode_string(decoded->body);
    } else if (trace.ident == mir2::kSmDealRemoteAddItem) {
      const auto item = decode_client_item(decoded->body);
      if (item.has_value()) {
        trace.item_make_index = item->make_index;
      }
    }
    events.push_back(std::move(trace));
  }
}

bool same_event(const TradeEvent& actual, const TradeEvent& expected) {
  return actual.session_id == expected.session_id && actual.ident == expected.ident &&
         actual.recog == expected.recog && actual.param == expected.param &&
         actual.tag == expected.tag && actual.item_make_index == expected.item_make_index &&
         actual.body_text == expected.body_text;
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.budgets.tick_ms = kTestTickMs;
  config.maps.push_back(mir2::MapConfig{"0", "TradeMap", {}, 0, 0, 30, 30});
  config.items.push_back(mir2::ItemConfig{1, "Ruby", 1, 40, 0, 2, 1, 1000, 10, 0, 0});
  config.items.push_back(mir2::ItemConfig{2, "Sapphire", 1, 41, 0, 3, 1, 1000, 10, 0, 0});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  const auto hero_a = make_character("guest_a", "HeroA", 10, 2, 100, make_item(1, 1001));
  const auto hero_b = make_character("guest_b", "HeroB", 11, 6, 50, make_item(2, 2001));
  static_cast<void>(runtime.route_logic_command(enter_command(7, hero_a)));
  static_cast<void>(runtime.route_logic_command(enter_command(8, hero_b)));
  static_cast<void>(runtime.tick());

  std::vector<TradeEvent> actual;

  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_try, 7, 0, "HeroB")));
  collect_trade_events(tick_players(runtime), actual);

  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_add_item, 7, 1001, "Ruby")));
  collect_trade_events(tick_players(runtime), actual);

  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_set_gold, 8, 0, {}, 7)));
  collect_trade_events(tick_players(runtime), actual);

  static_cast<void>(tick_past_trade_stable_window(runtime));
  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_accept, 7)));
  collect_trade_events(tick_players(runtime), actual);

  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_accept, 8)));
  collect_trade_events(tick_players(runtime), actual);

  const std::vector<TradeEvent> expected{
      {7, mir2::kSmDealMenu, 0, 0, 0, 0, "HeroB"},
      {8, mir2::kSmDealMenu, 0, 0, 0, 0, "HeroA"},
      {7, mir2::kSmDealAddItemOk, 0, 0, 0, 0, {}},
      {8, mir2::kSmDealRemoteAddItem, 1, 0, 0, 1001, {}},
      {8, mir2::kSmDealChangeGoldOk, 7, 43, 0, 0, {}},
      {7, mir2::kSmDealRemoteChangeGold, 7, 0, 0, 0, {}},
      {8, mir2::kSmDealSuccess, 0, 0, 0, 0, {}},
      {7, mir2::kSmDealSuccess, 0, 0, 0, 0, {}},
  };

  if (actual.size() != expected.size()) {
    return fail(1);
  }
  for (std::size_t index = 0; index < expected.size(); ++index) {
    if (!same_event(actual[index], expected[index])) {
      return fail(static_cast<int>(index + 2));
    }
  }

  const auto hero_a_after = runtime.snapshot_character_actor("HeroA");
  const auto hero_b_after = runtime.snapshot_character_actor("HeroB");
  if (!hero_a_after.has_value() || !hero_b_after.has_value() ||
      hero_a_after->gold != 107 || hero_b_after->gold != 43) {
    return fail(20);
  }

  return 0;
}
