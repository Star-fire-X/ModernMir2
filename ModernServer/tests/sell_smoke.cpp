#include <optional>
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

  const auto login_dispatch = runtime.tick();
  if (!find_packet(login_dispatch, mir2::kSmNewMap).has_value()) {
    return 1;
  }

  mir2::LogicCommand click_npc;
  click_npc.kind = mir2::LogicCommandKind::click_npc;
  click_npc.session_id = 7;
  click_npc.target_actor_id = 1;
  static_cast<void>(runtime.route_logic_command(click_npc));
  const auto click_dispatch = runtime.tick();
  const auto merchant_say = find_packet(click_dispatch, mir2::kSmMerchantSay);
  if (!merchant_say.has_value()) {
    return 1;
  }
  const auto merchant_text = decode_merchant_dialog(merchant_say->body);
  if (merchant_text.find("Trader/") != 0 || merchant_text.find("<Sell/@sell>") == std::string::npos ||
      merchant_text.find("<Repair/@repair>") == std::string::npos) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(7, 1, "@sell")));
  const auto sell_dispatch_0 = runtime.tick();
  const auto sell_menu = find_packet(sell_dispatch_0, mir2::kSmSendUserSell);
  if (!sell_menu.has_value() || sell_menu->message.recog != 1) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(
      make_sell_command(mir2::LogicCommandKind::query_sell_price, 7, 1, 1001, "Sell Sword")));
  const auto query_dispatch = runtime.tick();
  const auto sell_price = find_packet(query_dispatch, mir2::kSmSendBuyPrice);
  if (!sell_price.has_value() || sell_price->message.recog != 36) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(
      make_sell_command(mir2::LogicCommandKind::sell_item, 7, 1, 1001, "Sell Sword")));
  const auto sell_dispatch = runtime.tick();
  const auto sell_ok = find_packet(sell_dispatch, mir2::kSmUserSellItemOk);
  const auto sell_weight = find_packet(sell_dispatch, mir2::kSmWeightChanged);
  if (!sell_ok.has_value() || !sell_weight.has_value() || sell_ok->message.recog != 46 ||
      sell_weight->message.recog != 0 || sell_weight->message.param != 0 ||
      sell_weight->message.tag != 0 || sell_weight->message.series != weight_checksum(0, 0, 0)) {
    return 1;
  }

  mir2::LogicCommand bag_query;
  bag_query.kind = mir2::LogicCommandKind::query_bag_items;
  bag_query.session_id = 7;
  static_cast<void>(runtime.route_logic_command(bag_query));
  const auto bag_dispatch = runtime.tick();
  const auto bag_packet = find_packet(bag_dispatch, mir2::kSmBagItems);
  if (!bag_packet.has_value()) {
    return 1;
  }
  if (!decode_bag_items(bag_packet->body).empty()) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(
      make_sell_command(mir2::LogicCommandKind::sell_item, 7, 1, 1001, "Sell Sword")));
  const auto sell_fail_dispatch = runtime.tick();
  if (!find_packet(sell_fail_dispatch, mir2::kSmUserSellItemFail).has_value()) {
    return 1;
  }

  return 0;
}
