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

std::vector<mir2::LegacyClientItem> decode_client_items(std::string_view body, bool outer_decode) {
  std::vector<mir2::LegacyClientItem> items;
  const auto decoded = outer_decode ? mir2::legacy_decode_string(body) : std::string(body);
  for (const auto& part : mir2::util::split(decoded, '/')) {
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

mir2::LogicCommand make_buy_command(std::uint64_t session_id, std::uint64_t merchant_id,
                                    std::int32_t make_index, std::string item_name) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::buy_item;
  command.session_id = session_id;
  command.target_actor_id = merchant_id;
  command.item_make_index = make_index;
  command.text = std::move(item_name);
  return command;
}

mir2::LogicCommand make_detail_command(std::uint64_t session_id, std::uint64_t merchant_id,
                                       std::int32_t top_line, std::string item_name) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::query_detail_goods;
  command.session_id = session_id;
  command.target_actor_id = merchant_id;
  command.item_make_index = top_line;
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
  auto fail = [](int stage) { return stage; };

  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "BuyMap", {}, 0, 0, 10, 10});
  config.items.push_back(mir2::ItemConfig{1, "Potion", 1, 40, 0, 1, 1, 1000, -1, 0, 0});
  config.items.push_back(mir2::ItemConfig{2, "Bronze Sword", 5, 120, 5, 1, 2, 1000, 1, 0, 0});
  config.npcs.push_back(mir2::NpcConfig{"merchant_1", "0", "Trader", 11, 10, "merchant_1.txt",
                                         "sell_repair", {1, 1, 2}});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  mir2::CharacterRecord hero;
  hero.account_id = "guest";
  hero.character_name = "Hero";
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;
  hero.gold = 200;
  hero.ability.max_hp = 15;
  hero.ability.max_mp = 10;
  hero.ability.max_exp = 100;
  hero.ability.max_weight = 30;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;

  mir2::LogicCommand enter;
  enter.kind = mir2::LogicCommandKind::enter_world;
  enter.session_id = 9;
  enter.account_id = "guest";
  enter.character_name = "Hero";
  enter.map_id = "0";
  enter.x = 10;
  enter.y = 10;
  enter.character = hero;
  static_cast<void>(runtime.route_logic_command(enter));

  const auto login_dispatch = runtime.tick();
  if (!find_packet(login_dispatch, mir2::kSmNewMap).has_value()) {
    return fail(1);
  }

  mir2::LogicCommand click_npc;
  click_npc.kind = mir2::LogicCommandKind::click_npc;
  click_npc.session_id = 9;
  click_npc.target_actor_id = 1;
  static_cast<void>(runtime.route_logic_command(click_npc));
  const auto dialog_dispatch = runtime.tick();
  const auto merchant_say = find_packet(dialog_dispatch, mir2::kSmMerchantSay);
  if (!merchant_say.has_value()) {
    return fail(2);
  }
  const auto merchant_text = decode_merchant_dialog(merchant_say->body);
  if (merchant_text.find("Trader/") != 0 || merchant_text.find("<Buy/@buy>") == std::string::npos ||
      merchant_text.find("<Sell/@sell>") == std::string::npos ||
      merchant_text.find("<Repair/@repair>") == std::string::npos) {
    return fail(3);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(9, 1, "@buy")));
  const auto shop_dispatch = runtime.tick();
  const auto goods_list = find_packet(shop_dispatch, mir2::kSmSendGoodsList);
  if (!goods_list.has_value() || goods_list->message.recog != 1 || goods_list->message.param != 2) {
    return fail(4);
  }
  const auto goods_body = mir2::legacy_decode_string(goods_list->body);
  if (goods_body.find("Potion/0/40/2/") == std::string::npos ||
      goods_body.find("Bronze Sword/1/120/1/") == std::string::npos) {
    return fail(5);
  }

  static_cast<void>(runtime.route_logic_command(make_detail_command(9, 1, 0, "Bronze Sword")));
  const auto detail_dispatch = runtime.tick();
  const auto detail_packet = find_packet(detail_dispatch, mir2::kSmSendDetailGoodsList);
  if (!detail_packet.has_value() || detail_packet->message.recog != 1 || detail_packet->message.param != 1) {
    return fail(6);
  }
  const auto detail_items = decode_client_items(detail_packet->body, true);
  if (detail_items.size() != 1 || mir2::to_string(detail_items.front().item.name) != "Bronze Sword" ||
      detail_items.front().dura_max != 120) {
    return fail(7);
  }
  const auto sword_make_index = detail_items.front().make_index;

  static_cast<void>(runtime.route_logic_command(make_buy_command(9, 1, sword_make_index, "Bronze Sword")));
  const auto sword_buy_dispatch = runtime.tick();
  const auto add_sword = find_packet(sword_buy_dispatch, mir2::kSmAddItem);
  const auto sword_buy_ok = find_packet(sword_buy_dispatch, mir2::kSmBuyItemSuccess);
  const auto sword_weight = find_packet(sword_buy_dispatch, mir2::kSmWeightChanged);
  if (!add_sword.has_value() || !sword_buy_ok.has_value() || !sword_weight.has_value() ||
      sword_buy_ok->message.recog != 80 ||
      mir2::make_long(sword_buy_ok->message.param, sword_buy_ok->message.tag) != sword_make_index ||
      sword_weight->message.recog != 5 || sword_weight->message.param != 0 ||
      sword_weight->message.tag != 0 ||
      sword_weight->message.series != weight_checksum(5, 0, 0)) {
    return fail(8);
  }

  static_cast<void>(runtime.route_logic_command(make_buy_command(9, 1, 2, "Potion")));
  const auto potion_buy_dispatch = runtime.tick();
  const auto potion_buy_ok = find_packet(potion_buy_dispatch, mir2::kSmBuyItemSuccess);
  if (!potion_buy_ok.has_value() || potion_buy_ok->message.recog != 40) {
    return fail(9);
  }

  static_cast<void>(runtime.route_logic_command(click_npc));
  const auto dialog_after_buy = runtime.tick();
  if (!find_packet(dialog_after_buy, mir2::kSmMerchantSay).has_value()) {
    return fail(10);
  }
  static_cast<void>(runtime.route_logic_command(make_menu_command(9, 1, "@buy")));
  const auto shop_after_buy = runtime.tick();
  const auto goods_after_buy = find_packet(shop_after_buy, mir2::kSmSendGoodsList);
  if (!goods_after_buy.has_value()) {
    return fail(11);
  }
  const auto goods_after_body = mir2::legacy_decode_string(goods_after_buy->body);
  if (goods_after_body.find("Potion/0/40/1/") == std::string::npos ||
      goods_after_body.find("Bronze Sword") != std::string::npos) {
    return fail(12);
  }

  static_cast<void>(runtime.route_logic_command(make_buy_command(9, 1, 1, "Potion")));
  const auto potion_buy_dispatch_2 = runtime.tick();
  const auto potion_buy_ok_2 = find_packet(potion_buy_dispatch_2, mir2::kSmBuyItemSuccess);
  if (!potion_buy_ok_2.has_value() || potion_buy_ok_2->message.recog != 0) {
    return fail(13);
  }

  static_cast<void>(runtime.route_logic_command(make_buy_command(9, 1, 0, "Potion")));
  const auto potion_buy_fail = runtime.tick();
  const auto potion_buy_fail_packet = find_packet(potion_buy_fail, mir2::kSmBuyItemFail);
  if (!potion_buy_fail_packet.has_value() || potion_buy_fail_packet->message.recog != 1) {
    return fail(14);
  }

  mir2::LogicCommand bag_query;
  bag_query.kind = mir2::LogicCommandKind::query_bag_items;
  bag_query.session_id = 9;
  static_cast<void>(runtime.route_logic_command(bag_query));
  const auto bag_dispatch = runtime.tick();
  const auto bag_packet = find_packet(bag_dispatch, mir2::kSmBagItems);
  if (!bag_packet.has_value()) {
    return fail(15);
  }
  const auto bag_items = decode_client_items(bag_packet->body, false);
  if (bag_items.size() != 3) {
    return fail(16);
  }

  return 0;
}
