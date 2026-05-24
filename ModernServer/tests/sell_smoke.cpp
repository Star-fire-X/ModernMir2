#include <optional>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "util/string_utils.hpp"
#include "world/legacy_map_environment.hpp"
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

std::vector<mir2::LegacyClientItem> decode_bag_items(std::string_view body) {
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

std::string decode_merchant_dialog(std::string_view body) {
  return mir2::legacy_decode_string(body);
}

std::uint16_t weight_checksum(std::uint16_t weight, std::uint16_t wear_weight,
                              std::uint16_t hand_weight) {
  return static_cast<std::uint16_t>(
      (((weight + wear_weight + hand_weight) ^ 0x3A5F) ^ 0x1F35) ^ 0xAA21);
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

void append_dispatch(mir2::RuntimeDispatch& target, mir2::RuntimeDispatch source) {
  target.session_events.insert(target.session_events.end(),
                               std::make_move_iterator(source.session_events.begin()),
                               std::make_move_iterator(source.session_events.end()));
  target.persist_requests.insert(target.persist_requests.end(),
                                 std::make_move_iterator(source.persist_requests.begin()),
                                 std::make_move_iterator(source.persist_requests.end()));
}

mir2::RuntimeDispatch run_legacy_ticks(mir2::LogicRuntime& runtime) {
  mir2::RuntimeDispatch dispatch;
  for (int i = 0; i < 30; ++i) {
    append_dispatch(dispatch, runtime.tick());
  }
  return dispatch;
}

mir2::LogicCommand make_sell_command(mir2::LogicCommandKind kind, std::uint64_t session_id,
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

mir2::LogicCommand make_menu_command(std::uint64_t session_id, std::uint64_t merchant_id,
                                     std::string action) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::merchant_select;
  command.session_id = session_id;
  command.target_actor_id = merchant_id;
  command.text = std::move(action);
  return command;
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "SellMap", {}, 0, 0, 10, 10});
  config.items.push_back(mir2::ItemConfig{1, "Sell Sword", 3, 90, 5, 1, 1, 1000, 1, 0, 0});
  config.npcs.push_back(mir2::NpcConfig{"merchant_1",
                                         "0",
                                         "Trader",
                                         11,
                                         10,
                                         "merchant_1.txt",
                                         "sell_repair",
                                         {},
                                         {},
                                         100,
                                         {5}});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  mir2::CharacterRecord hero;
  hero.account_id = "guest";
  hero.character_name = "Hero";
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;
  hero.gold = 10;
  hero.ability.max_hp = 15;
  hero.ability.max_mp = 10;
  hero.ability.max_exp = 100;
  hero.ability.max_weight = 30;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;
  hero.bag_items[0].index = 1;
  hero.bag_items[0].make_index = 1001;
  hero.bag_items[0].dura = 600;
  hero.bag_items[0].dura_max = 1000;

  mir2::LogicCommand enter;
  enter.kind = mir2::LogicCommandKind::enter_world;
  enter.session_id = 7;
  enter.account_id = "guest";
  enter.character_name = "Hero";
  enter.map_id = "0";
  enter.x = 10;
  enter.y = 10;
  enter.character = hero;
  static_cast<void>(runtime.route_logic_command(enter));

  const auto login_dispatch = run_legacy_ticks(runtime);
  if (!find_packet(login_dispatch, mir2::kSmNewMap).has_value()) {
    return 1;
  }

  mir2::LogicCommand click_npc;
  click_npc.kind = mir2::LogicCommandKind::click_npc;
  click_npc.session_id = 7;
  click_npc.target_actor_id = 1;
  static_cast<void>(runtime.route_logic_command(click_npc));
  const auto click_dispatch = run_legacy_ticks(runtime);
  const auto merchant_say = find_packet(click_dispatch, mir2::kSmMerchantSay);
  const auto direct_sell_menu = find_packet(click_dispatch, mir2::kSmSendUserSell);
  if (!merchant_say.has_value() && !direct_sell_menu.has_value()) {
    return 1;
  }
  mir2::RuntimeDispatch sell_dispatch_0 = click_dispatch;
  if (merchant_say.has_value()) {
    const auto merchant_text = decode_merchant_dialog(merchant_say->body);
    if (merchant_text.find("Trader/") != 0 || merchant_text.find("<Sell/@sell>") == std::string::npos ||
        merchant_text.find("<Repair/@repair>") == std::string::npos) {
      return 1;
    }

    static_cast<void>(runtime.route_logic_command(make_menu_command(7, 1, "@sell")));
    sell_dispatch_0 = run_legacy_ticks(runtime);
  }

  const auto sell_menu = find_packet(sell_dispatch_0, mir2::kSmSendUserSell);
  if (!sell_menu.has_value() || sell_menu->message.recog != 1) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(
      make_sell_command(mir2::LogicCommandKind::query_sell_price, 7, 1, 1001, "Sell Sword")));
  const auto query_dispatch = run_legacy_ticks(runtime);
  const auto sell_price = find_packet(query_dispatch, mir2::kSmSendBuyPrice);
  if (!sell_price.has_value() || sell_price->message.recog != 36) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(
      make_sell_command(mir2::LogicCommandKind::sell_item, 7, 1, 1001, "Sell Sword")));
  const auto sell_dispatch = run_legacy_ticks(runtime);
  const auto sell_ok = find_packet(sell_dispatch, mir2::kSmUserSellItemOk);
  const auto sell_weight = find_packet(sell_dispatch, mir2::kSmWeightChanged);
  if (!sell_ok.has_value() || !sell_weight.has_value() || sell_ok->message.recog != 46 ||
      sell_weight->message.recog != 0 || sell_weight->message.param != 0 ||
      sell_weight->message.tag != 0 || sell_weight->message.series != weight_checksum(0, 0, 0) ||
      !has_save_character(sell_dispatch, "Hero")) {
    return 1;
  }

  mir2::LogicCommand bag_query;
  bag_query.kind = mir2::LogicCommandKind::query_bag_items;
  bag_query.session_id = 7;
  static_cast<void>(runtime.route_logic_command(bag_query));
  const auto bag_dispatch = run_legacy_ticks(runtime);
  const auto bag_packet = find_packet(bag_dispatch, mir2::kSmBagItems);
  if (!bag_packet.has_value()) {
    return 1;
  }
  if (!decode_bag_items(bag_packet->body).empty()) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(
      make_sell_command(mir2::LogicCommandKind::sell_item, 7, 1, 1001, "Sell Sword")));
  const auto sell_fail_dispatch = run_legacy_ticks(runtime);
  if (!find_packet(sell_fail_dispatch, mir2::kSmUserSellItemFail).has_value()) {
    return 1;
  }

  mir2::LogicRuntime capped_runtime(config);
  capped_runtime.initialize();

  mir2::CharacterRecord capped_hero = hero;
  capped_hero.character_name = "CappedHero";
  capped_hero.gold = mir2::kLegacyBagGold - 10;
  capped_hero.bag_items[0].make_index = 2001;
  capped_hero.bag_items[1] = capped_hero.bag_items[0];
  capped_hero.bag_items[1].make_index = 2002;

  mir2::LogicCommand capped_enter = enter;
  capped_enter.session_id = 8;
  capped_enter.character_name = "CappedHero";
  capped_enter.character = capped_hero;
  static_cast<void>(capped_runtime.route_logic_command(capped_enter));
  if (!find_packet(run_legacy_ticks(capped_runtime), mir2::kSmNewMap).has_value()) {
    return 1;
  }

  static_cast<void>(capped_runtime.route_logic_command(
      make_sell_command(mir2::LogicCommandKind::sell_item, 8, 1, 2001, "Sell Sword")));
  const auto capped_sell_dispatch = run_legacy_ticks(capped_runtime);
  if (!find_packet(capped_sell_dispatch, mir2::kSmUserSellItemFail).has_value() ||
      find_packet(capped_sell_dispatch, mir2::kSmWeightChanged).has_value() ||
      has_save_character(capped_sell_dispatch, "CappedHero")) {
    return 1;
  }

  mir2::LogicCommand capped_bag_query;
  capped_bag_query.kind = mir2::LogicCommandKind::query_bag_items;
  capped_bag_query.session_id = 8;
  static_cast<void>(capped_runtime.route_logic_command(capped_bag_query));
  const auto capped_bag_dispatch = run_legacy_ticks(capped_runtime);
  const auto capped_bag_packet = find_packet(capped_bag_dispatch, mir2::kSmBagItems);
  if (!capped_bag_packet.has_value()) {
    return 1;
  }
  const auto capped_items = decode_bag_items(capped_bag_packet->body);
  if (capped_items.size() != 2 || capped_items[0].make_index != 2001 ||
      capped_items[1].make_index != 2002) {
    return 1;
  }

  return 0;
}
