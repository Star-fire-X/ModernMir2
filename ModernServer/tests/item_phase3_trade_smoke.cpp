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
#include "protocol/canonical_legacy_command.hpp"
#include "protocol/client_v1_legacy_command_decoder.hpp"
#include "util/string_utils.hpp"
#include "world/logic_runtime.hpp"

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

std::vector<mir2::LegacyClientItem> decode_client_items(std::string_view body) {
  std::vector<mir2::LegacyClientItem> items;
  for (const auto& part : mir2::util::split(std::string(body), '/')) {
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

bool has_save_character(const mir2::RuntimeDispatch& dispatch, std::string_view name) {
  for (const auto& request : dispatch.persist_requests) {
    if (request.kind == mir2::PersistRequestKind::save_character &&
        request.character_name == name) {
      return true;
    }
  }
  return false;
}

bool has_raw_text_for(const mir2::RuntimeDispatch& dispatch, std::uint64_t session_id,
                      std::string_view text) {
  for (const auto& event : dispatch.session_events) {
    if (event.session_id != session_id) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == mir2::kSmHear &&
        mir2::legacy_decode_string(decoded->body).find(text) != std::string::npos) {
      return true;
    }
    const auto found =
        std::search(event.packet.body.begin(), event.packet.body.end(), text.begin(), text.end());
    if (found != event.packet.body.end()) {
      return true;
    }
  }
  return false;
}

bool has_packet_message(const mir2::RuntimeDispatch& dispatch, std::uint64_t session_id,
                        std::uint16_t ident, std::int32_t recog, std::int32_t param = 0,
                        std::int32_t tag = 0) {
  const auto packet = find_packet(dispatch, session_id, ident);
  return packet.has_value() && packet->message.recog == recog &&
         packet->message.param == param && packet->message.tag == tag;
}

void append_dispatch(mir2::RuntimeDispatch& target, mir2::RuntimeDispatch source) {
  target.session_events.insert(target.session_events.end(),
                               std::make_move_iterator(source.session_events.begin()),
                               std::make_move_iterator(source.session_events.end()));
  target.persist_requests.insert(target.persist_requests.end(),
                                 std::make_move_iterator(source.persist_requests.begin()),
                                 std::make_move_iterator(source.persist_requests.end()));
}

mir2::RuntimeDispatch tick_players(mir2::LogicRuntime& runtime, int count = 30) {
  mir2::RuntimeDispatch dispatch;
  for (int i = 0; i < count; ++i) {
    append_dispatch(dispatch, runtime.tick());
  }
  return dispatch;
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
                                     std::int32_t x, std::int32_t gold,
                                     mir2::LegacyUserItem item) {
  mir2::CharacterRecord character;
  character.account_id = std::move(account);
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = x;
  character.y = 10;
  character.dir = x <= 10 ? 2 : 6;
  character.gold = gold;
  character.ability.level = 30;
  character.ability.max_hp = 50;
  character.ability.max_mp = 50;
  character.ability.max_exp = 100;
  character.ability.max_weight = 100;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
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

mir2::LogicCommand turn_command(std::uint64_t session_id, std::int32_t x, std::int32_t y,
                                std::uint8_t dir) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::turn;
  command.session_id = session_id;
  command.x = x;
  command.y = y;
  command.dir = dir;
  return command;
}

mir2::LogicCommand walk_command(std::uint64_t session_id, std::int32_t x, std::int32_t y) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::walk;
  command.session_id = session_id;
  command.x = x;
  command.y = y;
  return command;
}

mir2::LogicCommand client_v1_trade_accept(std::uint64_t session_id) {
  return mir2::to_logic_command(mir2::decode_client_v1_trade_accept_command(session_id));
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
  return decode_client_items(packet->body);
}

}  // namespace

int main() {
  auto fail = [](int stage) { return stage; };

  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "TradeMap", {}, 0, 0, 30, 30});
  config.items.push_back(mir2::ItemConfig{1, "Ruby", 1, 40, 0, 2, 1, 1000, 10, 0, 0});
  config.items.push_back(mir2::ItemConfig{2, "Sapphire", 1, 41, 0, 3, 1, 1000, 10, 0, 0});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  const auto ruby = make_item(1, 1001);
  const auto sapphire = make_item(2, 2001);
  auto hero_a = make_character("guest_a", "HeroA", 10, 100, ruby);
  auto hero_b = make_character("guest_b", "HeroB", 11, 50, sapphire);

  static_cast<void>(runtime.route_logic_command(enter_command(7, hero_a)));
  static_cast<void>(runtime.route_logic_command(enter_command(8, hero_b)));
  const auto login = runtime.tick();
  if (!find_packet(login, 7, mir2::kSmNewMap).has_value() ||
      !find_packet(login, 8, mir2::kSmNewMap).has_value()) {
    return fail(1);
  }

  static_cast<void>(runtime.route_logic_command(turn_command(8, 11, 10, 0)));
  static_cast<void>(tick_players(runtime));
  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_try, 7, 0, "HeroB")));
  const auto not_facing = tick_players(runtime);
  if (!find_packet(not_facing, 7, mir2::kSmDealTryFail).has_value()) {
    return fail(2);
  }
  static_cast<void>(runtime.route_logic_command(turn_command(8, 11, 10, 6)));
  static_cast<void>(tick_players(runtime));

  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_try, 7, 0, "HeroB")));
  const auto open = tick_players(runtime);
  if (!find_packet(open, 7, mir2::kSmDealMenu).has_value() ||
      !find_packet(open, 8, mir2::kSmDealMenu).has_value()) {
    return fail(3);
  }

  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_add_item, 7, 1001, "Ruby")));
  const auto add_a = tick_players(runtime);
  if (!find_packet(add_a, 7, mir2::kSmDelItem).has_value() ||
      !find_packet(add_a, 7, mir2::kSmWeightChanged).has_value() ||
      has_save_character(add_a, "HeroA")) {
    return fail(4);
  }

  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_set_gold, 7, 0, {}, 15)));
  const auto gold_a_offer = tick_players(runtime);
  const auto hero_a_after_gold_offer = runtime.snapshot_character_actor("HeroA");
  if (!hero_a_after_gold_offer.has_value() || hero_a_after_gold_offer->gold != 85 ||
      !has_packet_message(gold_a_offer, 7, mir2::kSmDealChangeGoldOk, 15, 85, 0) ||
      !has_packet_message(gold_a_offer, 8, mir2::kSmDealRemoteChangeGold, 15)) {
    return fail(5);
  }

  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_add_item, 8, 2001, "Sapphire")));
  const auto add_b = tick_players(runtime);
  if (!find_packet(add_b, 8, mir2::kSmDelItem).has_value() ||
      !find_packet(add_b, 8, mir2::kSmWeightChanged).has_value() ||
      has_save_character(add_b, "HeroB")) {
    return fail(6);
  }

  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_set_gold, 8, 0, {}, 7)));
  const auto gold_b_offer = tick_players(runtime);
  const auto hero_b_after_gold_offer = runtime.snapshot_character_actor("HeroB");
  if (!hero_b_after_gold_offer.has_value() || hero_b_after_gold_offer->gold != 43 ||
      !has_packet_message(gold_b_offer, 8, mir2::kSmDealChangeGoldOk, 7, 43, 0) ||
      !has_packet_message(gold_b_offer, 7, mir2::kSmDealRemoteChangeGold, 7)) {
    return fail(7);
  }
  static_cast<void>(tick_players(runtime, 60));

  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_accept, 7)));
  const auto first_accept = tick_players(runtime);
  if (find_packet(first_accept, 7, mir2::kSmAddItem).has_value() ||
      find_packet(first_accept, 8, mir2::kSmAddItem).has_value()) {
    return fail(8);
  }

  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_accept, 8)));
  const auto commit = tick_players(runtime);
  const auto add_to_a = find_packet(commit, 7, mir2::kSmAddItem);
  const auto add_to_b = find_packet(commit, 8, mir2::kSmAddItem);
  const auto gold_a = find_packet(commit, 7, mir2::kSmGoldChanged);
  const auto gold_b = find_packet(commit, 8, mir2::kSmGoldChanged);
  if (!add_to_a.has_value() || !add_to_b.has_value() ||
      !gold_a.has_value() || !gold_b.has_value() ||
      gold_a->message.recog != 92 || gold_b->message.recog != 58 ||
      !has_save_character(commit, "HeroA") || !has_save_character(commit, "HeroB")) {
    return fail(9);
  }

  const auto bag_a = query_bag(runtime, 7);
  const auto bag_b = query_bag(runtime, 8);
  if (bag_a.size() != 1 || bag_b.size() != 1 ||
      bag_a.front().make_index != 2001 || mir2::to_string(bag_a.front().item.name) != "Sapphire" ||
      bag_b.front().make_index != 1001 || mir2::to_string(bag_b.front().item.name) != "Ruby") {
    return fail(10);
  }

  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_try, 7, 0, "HeroB")));
  static_cast<void>(tick_players(runtime));
  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_add_item, 7, 2001, "Sapphire")));
  static_cast<void>(tick_players(runtime));
  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_remove_item, 7, 2001, "Sapphire")));
  static_cast<void>(tick_players(runtime));
  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_accept, 7)));
  const auto remove_window_cancel = tick_players(runtime);
  if (!has_raw_text_for(remove_window_cancel, 7, "Trade cancelled.") ||
      !has_raw_text_for(remove_window_cancel, 8, "Trade cancelled.") ||
      !find_packet(remove_window_cancel, 7, mir2::kSmDealCancel).has_value() ||
      !find_packet(remove_window_cancel, 8, mir2::kSmDealCancel).has_value()) {
    return fail(11);
  }

  auto bag_after_remove_window = query_bag(runtime, 7);
  if (bag_after_remove_window.size() != 1 || bag_after_remove_window.front().make_index != 2001) {
    return fail(12);
  }

  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_try, 7, 0, "HeroB")));
  static_cast<void>(tick_players(runtime));
  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_add_item, 7, 2001, "Sapphire")));
  static_cast<void>(tick_players(runtime));
  static_cast<void>(runtime.route_logic_command(client_v1_trade_accept(7)));
  const auto early_accept = tick_players(runtime);
  if (!find_packet(early_accept, 7, mir2::kSmAddItem).has_value() ||
      !has_save_character(early_accept, "HeroA") ||
      !has_raw_text_for(early_accept, 7, "Trade cancelled.") ||
      !has_raw_text_for(early_accept, 8, "Trade cancelled.") ||
      !find_packet(early_accept, 7, mir2::kSmDealCancel).has_value() ||
      !find_packet(early_accept, 8, mir2::kSmDealCancel).has_value()) {
    return fail(13);
  }

  const auto bag_after_early_accept = query_bag(runtime, 7);
  if (bag_after_early_accept.size() != 1 || bag_after_early_accept.front().make_index != 2001) {
    return fail(14);
  }

  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_try, 7, 0, "HeroB")));
  static_cast<void>(tick_players(runtime));
  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_set_gold, 7, 0, {}, 999)));
  const auto gold_fail = tick_players(runtime);
  const auto hero_a_after_gold_fail = runtime.snapshot_character_actor("HeroA");
  if (!hero_a_after_gold_fail.has_value() || hero_a_after_gold_fail->gold != 92 ||
      !has_packet_message(gold_fail, 7, mir2::kSmDealChangeGoldFail, 0, 92, 0)) {
    return fail(15);
  }
  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_set_gold, 7, 0, {}, 5)));
  const auto gold_reserve = tick_players(runtime);
  const auto hero_a_after_gold_reserve = runtime.snapshot_character_actor("HeroA");
  if (!hero_a_after_gold_reserve.has_value() || hero_a_after_gold_reserve->gold != 87 ||
      !has_packet_message(gold_reserve, 7, mir2::kSmDealChangeGoldOk, 5, 87, 0)) {
    return fail(16);
  }
  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_accept, 7)));
  const auto gold_window_cancel = tick_players(runtime);
  const auto hero_a_after_gold_cancel = runtime.snapshot_character_actor("HeroA");
  const auto hero_b_after_gold_cancel = runtime.snapshot_character_actor("HeroB");
  if (!hero_a_after_gold_cancel.has_value() || !hero_b_after_gold_cancel.has_value() ||
      hero_a_after_gold_cancel->gold != 92 || hero_b_after_gold_cancel->gold != 58 ||
      !has_raw_text_for(gold_window_cancel, 7, "Trade cancelled.") ||
      !has_raw_text_for(gold_window_cancel, 8, "Trade cancelled.") ||
      !find_packet(gold_window_cancel, 7, mir2::kSmDealCancel).has_value() ||
      !find_packet(gold_window_cancel, 8, mir2::kSmDealCancel).has_value()) {
    return fail(17);
  }

  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_try, 7, 0, "HeroB")));
  static_cast<void>(tick_players(runtime));
  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_add_item, 7, 2001, "Sapphire")));
  static_cast<void>(tick_players(runtime));
  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_cancel, 7)));
  const auto cancel = tick_players(runtime);
  if (!find_packet(cancel, 7, mir2::kSmAddItem).has_value() ||
      !has_save_character(cancel, "HeroA") ||
      !find_packet(cancel, 7, mir2::kSmDealCancel).has_value() ||
      !find_packet(cancel, 8, mir2::kSmDealCancel).has_value()) {
    return fail(18);
  }

  const auto bag_after_cancel = query_bag(runtime, 7);
  if (bag_after_cancel.size() != 1 || bag_after_cancel.front().make_index != 2001) {
    return fail(19);
  }

  static_cast<void>(runtime.route_logic_command(turn_command(7, 10, 10, 2)));
  static_cast<void>(tick_players(runtime));
  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_try, 7, 0, "HeroB")));
  static_cast<void>(tick_players(runtime));
  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_add_item, 7, 2001, "Sapphire")));
  static_cast<void>(tick_players(runtime));
  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_set_gold, 7, 0, {}, 3)));
  static_cast<void>(tick_players(runtime));
  static_cast<void>(runtime.route_logic_command(walk_command(7, 9, 10)));
  const auto move_cancel = tick_players(runtime);
  const auto hero_a_after_move_cancel = runtime.snapshot_character_actor("HeroA");
  if (!hero_a_after_move_cancel.has_value() || hero_a_after_move_cancel->gold != 92 ||
      !find_packet(move_cancel, 7, mir2::kSmDealCancel).has_value() ||
      !find_packet(move_cancel, 8, mir2::kSmDealCancel).has_value() ||
      !find_packet(move_cancel, 7, mir2::kSmAddItem).has_value() ||
      !has_save_character(move_cancel, "HeroA")) {
    return fail(20);
  }

  const auto bag_after_move_cancel = query_bag(runtime, 7);
  if (bag_after_move_cancel.size() != 1 || bag_after_move_cancel.front().make_index != 2001) {
    return fail(21);
  }

  return 0;
}
