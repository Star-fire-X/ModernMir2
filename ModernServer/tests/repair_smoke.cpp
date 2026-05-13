#include <cstddef>
#include <optional>
#include <iterator>
#include <string>
#include <string_view>

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

std::optional<std::size_t> packet_index(const mir2::RuntimeDispatch& dispatch,
                                        std::uint16_t ident) {
  for (std::size_t index = 0; index < dispatch.session_events.size(); ++index) {
    const auto decoded = mir2::decode_legacy_game_packet(dispatch.session_events[index].packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return index;
    }
  }
  return std::nullopt;
}

std::optional<mir2::LegacyClientItem> decode_client_item(std::string_view body) {
  mir2::LegacyClientItem item;
  if (!mir2::legacy_decode_buffer(body, &item, sizeof(item))) {
    return std::nullopt;
  }
  return item;
}

std::optional<mir2::LegacyClientItem> first_bag_item(std::string_view body) {
  for (const auto& part : mir2::util::split(body, '/')) {
    if (part.empty()) {
      continue;
    }
    return decode_client_item(part);
  }
  return std::nullopt;
}

std::optional<mir2::CharacterRecord> saved_character(const mir2::RuntimeDispatch& dispatch,
                                                     std::string_view name) {
  for (const auto& request : dispatch.persist_requests) {
    if (request.kind == mir2::PersistRequestKind::save_character &&
        request.character_name == name) {
      return request.character;
    }
  }
  return std::nullopt;
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

mir2::LogicCommand make_repair_command(mir2::LogicCommandKind kind, std::uint64_t session_id,
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
  config.maps.push_back(mir2::MapConfig{"0", "RepairMap", {}, 0, 0, 10, 10});
  config.items.push_back(mir2::ItemConfig{1, "Repair Sword", 3, 90, 5, 1, 1, 1000, 1, 0, 0});
  config.items.push_back(mir2::ItemConfig{2, "Repair Axe", 3, 90, 6, 1, 1, 1000, 1, 0, 0});
  config.items.push_back(mir2::ItemConfig{3, "Repair Armor", 3, 90, 10, 1, 1, 1000, 0, 0, 0});
  config.items.push_back(mir2::ItemConfig{4, "No Repair Gem", 1, 90, 43, 1, 1, 1000, -1, 0, 0});
  config.items.push_back(mir2::ItemConfig{5, "Cheap Armor", 3, 11, 10, 1, 1, 1000, 0, 0, 0});
  config.npcs.push_back(
      mir2::NpcConfig{"merchant_1", "0", "Repairman", 11, 10, "merchant_1.txt", "repair"});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  mir2::CharacterRecord hero;
  hero.account_id = "guest";
  hero.character_name = "Hero";
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;
  hero.gold = 100;
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
  hero.bag_items[1].index = 2;
  hero.bag_items[1].make_index = 1002;
  hero.bag_items[1].dura = 600;
  hero.bag_items[1].dura_max = 1000;
  hero.bag_items[2].index = 3;
  hero.bag_items[2].make_index = 1003;
  hero.bag_items[2].dura = 600;
  hero.bag_items[2].dura_max = 1000;
  hero.bag_items[3].index = 4;
  hero.bag_items[3].make_index = 1004;
  hero.bag_items[3].dura = 600;
  hero.bag_items[3].dura_max = 1000;
  hero.bag_items[4].index = 5;
  hero.bag_items[4].make_index = 1005;
  hero.bag_items[4].dura = 850;
  hero.bag_items[4].dura_max = 1000;

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
  const auto repair_menu = find_packet(click_dispatch, mir2::kSmSendUserRepair);
  if (!repair_menu.has_value() || repair_menu->message.recog != 1) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_repair_command(
      mir2::LogicCommandKind::query_repair_cost, 7, 1, 1001, "Repair Sword")));
  const auto query_dispatch = run_legacy_ticks(runtime);
  const auto repair_cost = find_packet(query_dispatch, mir2::kSmSendRepairCost);
  if (!repair_cost.has_value() || repair_cost->message.recog != 10) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(
      make_repair_command(mir2::LogicCommandKind::repair_item, 7, 1, 1001, "Repair Sword")));
  const auto repair_dispatch = run_legacy_ticks(runtime);
  const auto repair_ok = find_packet(repair_dispatch, mir2::kSmUserRepairItemOk);
  const auto repair_ok_index = packet_index(repair_dispatch, mir2::kSmUserRepairItemOk);
  const auto repair_gold_index = packet_index(repair_dispatch, mir2::kSmGoldChanged);
  const auto repair_save = saved_character(repair_dispatch, "Hero");
  if (!repair_ok.has_value() || repair_ok->message.recog != 90 || repair_ok->message.param != 987 ||
      repair_ok->message.tag != 987 || !repair_ok_index.has_value() ||
      !repair_gold_index.has_value() || *repair_ok_index >= *repair_gold_index ||
      !repair_save.has_value() || repair_save->gold != 90 ||
      repair_save->bag_items[0].dura != 987 || repair_save->bag_items[0].dura_max != 987) {
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

  const auto repaired_item = first_bag_item(bag_packet->body);
  if (!repaired_item.has_value() || repaired_item->make_index != 1001 || repaired_item->dura != 987 ||
      repaired_item->dura_max != 987) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(
      make_repair_command(mir2::LogicCommandKind::repair_item, 7, 1, 9999, "Missing Sword")));
  const auto repair_fail_dispatch = run_legacy_ticks(runtime);
  if (!find_packet(repair_fail_dispatch, mir2::kSmUserRepairItemFail).has_value()) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_repair_command(
      mir2::LogicCommandKind::query_repair_cost, 7, 1, 1005, "Cheap Armor")));
  const auto cheap_query_dispatch = run_legacy_ticks(runtime);
  const auto cheap_cost = find_packet(cheap_query_dispatch, mir2::kSmSendRepairCost);
  if (!cheap_cost.has_value() || cheap_cost->message.recog != 1) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(
      make_repair_command(mir2::LogicCommandKind::repair_item, 7, 1, 1005, "Cheap Armor")));
  const auto cheap_repair_dispatch = run_legacy_ticks(runtime);
  const auto cheap_ok = find_packet(cheap_repair_dispatch, mir2::kSmUserRepairItemOk);
  const auto cheap_save = saved_character(cheap_repair_dispatch, "Hero");
  if (!cheap_ok.has_value() || cheap_ok->message.recog != 89 ||
      cheap_ok->message.param != 995 || cheap_ok->message.tag != 995 ||
      !cheap_save.has_value() || cheap_save->gold != 89 ||
      cheap_save->bag_items[4].dura != 995 || cheap_save->bag_items[4].dura_max != 995) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(7, 1, "@s_repair")));
  const auto special_menu_dispatch = run_legacy_ticks(runtime);
  const auto special_menu = find_packet(special_menu_dispatch, mir2::kSmSendUserRepair);
  if (!special_menu.has_value() || special_menu->message.recog != 1) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_repair_command(
      mir2::LogicCommandKind::query_repair_cost, 7, 1, 1002, "Repair Axe")));
  const auto special_query_dispatch = run_legacy_ticks(runtime);
  const auto special_cost = find_packet(special_query_dispatch, mir2::kSmSendRepairCost);
  if (!special_cost.has_value() || special_cost->message.recog != 30) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(
      make_repair_command(mir2::LogicCommandKind::repair_item, 7, 1, 1002, "Repair Axe")));
  const auto special_repair_dispatch = run_legacy_ticks(runtime);
  const auto special_ok = find_packet(special_repair_dispatch, mir2::kSmUserRepairItemOk);
  const auto special_save = saved_character(special_repair_dispatch, "Hero");
  if (!special_ok.has_value() || special_ok->message.recog != 59 ||
      special_ok->message.param != 1000 || special_ok->message.tag != 1000 ||
      !special_save.has_value() || special_save->gold != 59 ||
      special_save->bag_items[1].dura != 1000 || special_save->bag_items[1].dura_max != 1000) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_repair_command(
      mir2::LogicCommandKind::query_repair_cost, 7, 1, 1003, "Repair Armor")));
  const auto special_non_weapon_query = run_legacy_ticks(runtime);
  const auto special_non_weapon_cost =
      find_packet(special_non_weapon_query, mir2::kSmSendRepairCost);
  if (!special_non_weapon_cost.has_value() || special_non_weapon_cost->message.recog != -1) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(
      make_repair_command(mir2::LogicCommandKind::repair_item, 7, 1, 1004, "No Repair Gem")));
  const auto no_repair_dispatch = run_legacy_ticks(runtime);
  if (!find_packet(no_repair_dispatch, mir2::kSmUserRepairItemFail).has_value()) {
    return 1;
  }

  mir2::LogicRuntime fail_runtime(config);
  fail_runtime.initialize();

  mir2::CharacterRecord fail_hero = hero;
  fail_hero.character_name = "FailHero";
  fail_hero.gold = 0;
  fail_hero.bag_items[0].make_index = 3001;

  mir2::LogicCommand fail_enter = enter;
  fail_enter.session_id = 8;
  fail_enter.character_name = "FailHero";
  fail_enter.character = fail_hero;
  static_cast<void>(fail_runtime.route_logic_command(fail_enter));
  if (!find_packet(run_legacy_ticks(fail_runtime), mir2::kSmNewMap).has_value()) {
    return 1;
  }

  static_cast<void>(fail_runtime.route_logic_command(
      make_repair_command(mir2::LogicCommandKind::repair_item, 8, 999, 3001, "Repair Sword")));
  const auto invalid_merchant_dispatch = run_legacy_ticks(fail_runtime);
  if (!find_packet(invalid_merchant_dispatch, mir2::kSmUserRepairItemFail).has_value() ||
      saved_character(invalid_merchant_dispatch, "FailHero").has_value()) {
    return 1;
  }

  static_cast<void>(fail_runtime.route_logic_command(
      make_repair_command(mir2::LogicCommandKind::repair_item, 8, 1, 3001, "Repair Sword")));
  const auto no_gold_dispatch = run_legacy_ticks(fail_runtime);
  if (!find_packet(no_gold_dispatch, mir2::kSmUserRepairItemFail).has_value() ||
      find_packet(no_gold_dispatch, mir2::kSmGoldChanged).has_value() ||
      saved_character(no_gold_dispatch, "FailHero").has_value()) {
    return 1;
  }

  mir2::LogicCommand fail_bag_query;
  fail_bag_query.kind = mir2::LogicCommandKind::query_bag_items;
  fail_bag_query.session_id = 8;
  static_cast<void>(fail_runtime.route_logic_command(fail_bag_query));
  const auto fail_bag_dispatch = run_legacy_ticks(fail_runtime);
  const auto fail_bag_packet = find_packet(fail_bag_dispatch, mir2::kSmBagItems);
  if (!fail_bag_packet.has_value()) {
    return 1;
  }
  const auto failed_item = first_bag_item(fail_bag_packet->body);
  if (!failed_item.has_value() || failed_item->make_index != 3001 ||
      failed_item->dura != 600 || failed_item->dura_max != 1000) {
    return 1;
  }

  return 0;
}
