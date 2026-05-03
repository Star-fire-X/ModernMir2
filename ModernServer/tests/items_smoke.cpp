#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "config/models.hpp"
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

bool has_packet(const mir2::RuntimeDispatch& dispatch, std::uint16_t ident) {
  return find_packet(dispatch, ident).has_value();
}

std::optional<mir2::LegacyClientItem> decode_client_item(std::string_view body) {
  mir2::LegacyClientItem item;
  if (!mir2::legacy_decode_buffer(body, &item, sizeof(item))) {
    return std::nullopt;
  }
  return item;
}

std::vector<mir2::LegacyClientItem> decode_bag_items(std::string_view body) {
  std::vector<mir2::LegacyClientItem> items;
  for (const auto& part : mir2::util::split(body, '/')) {
    if (part.empty()) {
      continue;
    }
    if (auto item = decode_client_item(part); item.has_value()) {
      items.push_back(*item);
    }
  }
  return items;
}

std::uint16_t weight_checksum(std::uint16_t weight, std::uint16_t wear_weight,
                              std::uint16_t hand_weight) {
  return static_cast<std::uint16_t>(
      (((weight + wear_weight + hand_weight) ^ 0x3A5F) ^ 0x1F35) ^ 0xAA21);
}

mir2::LogicCommand make_item_command(mir2::LogicCommandKind kind, std::uint64_t session_id,
                                     std::int32_t make_index, std::string name,
                                     std::int32_t slot = -1) {
  mir2::LogicCommand command;
  command.kind = kind;
  command.session_id = session_id;
  command.item_make_index = make_index;
  command.item_slot = slot;
  command.text = std::move(name);
  return command;
}

mir2::LogicCommand make_gold_command(std::uint64_t session_id, std::int32_t amount) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::drop_gold;
  command.session_id = session_id;
  command.amount = amount;
  return command;
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "TestMap", {}, 0, 0, 10, 10});
  config.items.push_back(mir2::ItemConfig{1, "Wooden Sword", 3, 100, 5, 1, 1, 1000, 1, 0, 0});
  config.items.push_back(mir2::ItemConfig{2, "Basic Drug", 1, 30, 0, 0, 2, 1, -1, 10, 0});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  mir2::CharacterRecord hero;
  hero.account_id = "guest";
  hero.character_name = "Hero";
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;
  hero.sex = 0;
  hero.hair = 2;
  hero.gold = 500;
  hero.ability.hp = 5;
  hero.ability.mp = 3;
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
  hero.bag_items[1].dura = 1;
  hero.bag_items[1].dura_max = 1;

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
  const auto new_map_packet = find_packet(login_dispatch, mir2::kSmNewMap);
  if (!new_map_packet.has_value()) {
    return 1;
  }
  const auto actor_id = static_cast<std::uint64_t>(static_cast<std::uint32_t>(new_map_packet->message.recog));
  static_cast<void>(runtime.route_logic_command(
      make_item_command(mir2::LogicCommandKind::drop_item, 7, 1001, "Wooden Sword")));
  const auto drop_dispatch = runtime.tick();
  if (!has_packet(drop_dispatch, mir2::kSmItemShow) ||
      !has_packet(drop_dispatch, mir2::kSmDelItem) ||
      !has_packet(drop_dispatch, mir2::kSmDropItemSuccess) ||
      !has_packet(drop_dispatch, mir2::kSmWeightChanged)) {
    return 1;
  }

  const auto item_show = find_packet(drop_dispatch, mir2::kSmItemShow);
  const auto drop_del = find_packet(drop_dispatch, mir2::kSmDelItem);
  const auto drop_ok = find_packet(drop_dispatch, mir2::kSmDropItemSuccess);
  const auto drop_weight = find_packet(drop_dispatch, mir2::kSmWeightChanged);
  if (!item_show.has_value() || !drop_del.has_value() || !drop_ok.has_value() ||
      !drop_weight.has_value()) {
    return 1;
  }
  const auto dropped_item = decode_client_item(drop_del->body);
  if (item_show->message.param != 10 || item_show->message.tag != 10 ||
      !dropped_item.has_value() || dropped_item->make_index != 1001 ||
      mir2::legacy_decode_string(item_show->body) != "Wooden Sword" ||
      drop_ok->message.recog != 1001 ||
      mir2::legacy_decode_string(drop_ok->body) != "Wooden Sword") {
    return 1;
  }
  if (drop_weight->message.recog != 1 || drop_weight->message.param != 0 ||
      drop_weight->message.tag != 0 ||
      drop_weight->message.series != weight_checksum(1, 0, 0)) {
    return 1;
  }
  mir2::LogicCommand pickup;
  pickup.kind = mir2::LogicCommandKind::pickup_item;
  pickup.session_id = 7;
  pickup.x = 10;
  pickup.y = 10;
  static_cast<void>(runtime.route_logic_command(pickup));
  const auto pickup_dispatch = runtime.tick();
  if (!has_packet(pickup_dispatch, mir2::kSmItemHide) ||
      !has_packet(pickup_dispatch, mir2::kSmAddItem) ||
      !has_packet(pickup_dispatch, mir2::kSmWeightChanged)) {
    return 1;
  }

  const auto item_hide = find_packet(pickup_dispatch, mir2::kSmItemHide);
  const auto pickup_add = find_packet(pickup_dispatch, mir2::kSmAddItem);
  const auto pickup_weight = find_packet(pickup_dispatch, mir2::kSmWeightChanged);
  if (!item_hide.has_value() || !pickup_add.has_value() || !pickup_weight.has_value()) {
    return 1;
  }
  if (item_hide->message.recog != item_show->message.recog ||
      item_hide->message.param != 10 || item_hide->message.tag != 10) {
    return 1;
  }
  const auto picked_item = decode_client_item(pickup_add->body);
  if (!picked_item.has_value() || picked_item->make_index != 1001 || picked_item->item.std_mode != 5 ||
      mir2::to_string(picked_item->item.name) != "Wooden Sword") {
    return 1;
  }
  if (pickup_weight->message.recog != 4 || pickup_weight->message.param != 0 ||
      pickup_weight->message.tag != 0 ||
      pickup_weight->message.series != weight_checksum(4, 0, 0)) {
    return 1;
  }
  static_cast<void>(runtime.route_logic_command(make_item_command(
      mir2::LogicCommandKind::take_on_item, 7, 1001, "Wooden Sword", 1)));
  const auto take_on_dispatch = runtime.tick();
  if (!has_packet(take_on_dispatch, mir2::kSmDelItem) ||
      !has_packet(take_on_dispatch, mir2::kSmTakeOnOk) ||
      !has_packet(take_on_dispatch, mir2::kSmUpdateItem) ||
      !has_packet(take_on_dispatch, mir2::kSmAbility) ||
      !has_packet(take_on_dispatch, mir2::kSmSendUseItems) ||
      !has_packet(take_on_dispatch, mir2::kSmWeightChanged)) {
    return 1;
  }

  const auto take_on_del = find_packet(take_on_dispatch, mir2::kSmDelItem);
  const auto take_on_ok = find_packet(take_on_dispatch, mir2::kSmTakeOnOk);
  const auto take_on_update = find_packet(take_on_dispatch, mir2::kSmUpdateItem);
  const auto take_on_weight = find_packet(take_on_dispatch, mir2::kSmWeightChanged);
  if (!take_on_del.has_value() || !take_on_ok.has_value() || !take_on_update.has_value() ||
      !take_on_weight.has_value()) {
    return 1;
  }
  const auto take_on_removed = decode_client_item(take_on_del->body);
  const auto take_on_equipped = decode_client_item(take_on_update->body);
  const auto expected_feature = mir2::make_feature(0, 0, 2, 4);
  if (!take_on_removed.has_value() || take_on_removed->make_index != 1001 ||
      !take_on_equipped.has_value() || take_on_equipped->make_index != 1001 ||
      take_on_equipped->dura != 600 ||
      take_on_ok->message.recog != expected_feature ||
      take_on_weight->message.recog != 1 || take_on_weight->message.param != 0 ||
      take_on_weight->message.tag != 3 ||
      take_on_weight->message.series != weight_checksum(1, 0, 3)) {
    return 1;
  }
  mir2::LogicCommand attack;
  attack.kind = mir2::LogicCommandKind::attack;
  attack.session_id = 7;
  attack.game_message.ident = mir2::kCmHit;
  static_cast<void>(runtime.route_logic_command(attack));
  const auto attack_dispatch_1 = runtime.tick();
  const auto attack_update_1 = find_packet(attack_dispatch_1, mir2::kSmUpdateItem);
  if (!attack_update_1.has_value() || has_packet(attack_dispatch_1, mir2::kSmDuraChange)) {
    return 1;
  }
  const auto attack_item_1 = decode_client_item(attack_update_1->body);
  if (!attack_item_1.has_value() || attack_item_1->make_index != 1001 || attack_item_1->dura != 500) {
    return 1;
  }
  static_cast<void>(runtime.route_logic_command(attack));
  const auto attack_dispatch_2 = runtime.tick();
  const auto attack_update_2 = find_packet(attack_dispatch_2, mir2::kSmUpdateItem);
  const auto attack_dura = find_packet(attack_dispatch_2, mir2::kSmDuraChange);
  if (!attack_update_2.has_value() || !attack_dura.has_value()) {
    return 1;
  }
  const auto attack_item_2 = decode_client_item(attack_update_2->body);
  if (!attack_item_2.has_value() || attack_item_2->make_index != 1001 || attack_item_2->dura != 400 ||
      attack_dura->message.recog != 400 || attack_dura->message.param != 1 ||
      attack_dura->message.tag != 1000 || attack_dura->message.series != 0) {
    return 1;
  }
  static_cast<void>(runtime.route_logic_command(make_item_command(
      mir2::LogicCommandKind::take_off_item, 7, 1001, "Wooden Sword", 1)));
  const auto take_off_dispatch = runtime.tick();
  if (!has_packet(take_off_dispatch, mir2::kSmDelItem) ||
      !has_packet(take_off_dispatch, mir2::kSmTakeOffOk) ||
      !has_packet(take_off_dispatch, mir2::kSmAddItem) ||
      !has_packet(take_off_dispatch, mir2::kSmAbility) ||
      !has_packet(take_off_dispatch, mir2::kSmSendUseItems) ||
      !has_packet(take_off_dispatch, mir2::kSmWeightChanged)) {
    return 1;
  }

  const auto take_off_del = find_packet(take_off_dispatch, mir2::kSmDelItem);
  const auto take_off_ok = find_packet(take_off_dispatch, mir2::kSmTakeOffOk);
  const auto take_off_add = find_packet(take_off_dispatch, mir2::kSmAddItem);
  const auto take_off_weight = find_packet(take_off_dispatch, mir2::kSmWeightChanged);
  if (!take_off_del.has_value() || !take_off_ok.has_value() || !take_off_add.has_value() ||
      !take_off_weight.has_value()) {
    return 1;
  }
  const auto take_off_removed = decode_client_item(take_off_del->body);
  const auto taken_off_item = decode_client_item(take_off_add->body);
  if (!take_off_removed.has_value() || take_off_removed->make_index != 1001 ||
      !taken_off_item.has_value() || taken_off_item->make_index != 1001 ||
      taken_off_item->dura != 400 ||
      take_off_ok->message.recog != mir2::make_feature(0, 0, 0, 4) ||
      take_off_weight->message.recog != 4 || take_off_weight->message.param != 0 ||
      take_off_weight->message.tag != 0 ||
      take_off_weight->message.series != weight_checksum(4, 0, 0)) {
    return 1;
  }
  static_cast<void>(runtime.route_logic_command(
      make_item_command(mir2::LogicCommandKind::eat_item, 7, 1002, "Basic Drug")));
  const auto eat_dispatch = runtime.tick();
  if (!has_packet(eat_dispatch, mir2::kSmDelItem) ||
      !has_packet(eat_dispatch, mir2::kSmEatOk) ||
      !has_packet(eat_dispatch, mir2::kSmHealthSpellChanged) ||
      !has_packet(eat_dispatch, mir2::kSmWeightChanged)) {
    return 1;
  }

  const auto eat_del = find_packet(eat_dispatch, mir2::kSmDelItem);
  const auto health_changed = find_packet(eat_dispatch, mir2::kSmHealthSpellChanged);
  const auto eat_weight = find_packet(eat_dispatch, mir2::kSmWeightChanged);
  if (!eat_del.has_value() || !health_changed.has_value() || !eat_weight.has_value()) {
    return 1;
  }
  const auto eaten_item = decode_client_item(eat_del->body);
  if (!eaten_item.has_value() || eaten_item->make_index != 1002 ||
      health_changed->message.recog != static_cast<std::int32_t>(actor_id) ||
      health_changed->message.param != 15 || health_changed->message.tag != 3 ||
      health_changed->message.series != 15 ||
      eat_weight->message.recog != 3 || eat_weight->message.param != 0 ||
      eat_weight->message.tag != 0 ||
      eat_weight->message.series != weight_checksum(3, 0, 0)) {
    return 1;
  }
  static_cast<void>(runtime.route_logic_command(make_gold_command(7, 120)));
  const auto drop_gold_dispatch = runtime.tick();
  if (!has_packet(drop_gold_dispatch, mir2::kSmItemShow) ||
      !has_packet(drop_gold_dispatch, mir2::kSmGoldChanged)) {
    return 1;
  }

  const auto gold_show = find_packet(drop_gold_dispatch, mir2::kSmItemShow);
  const auto gold_changed = find_packet(drop_gold_dispatch, mir2::kSmGoldChanged);
  if (!gold_show.has_value() || !gold_changed.has_value()) {
    return 1;
  }
  if (gold_show->message.param != 10 || gold_show->message.tag != 10 || gold_show->message.series != 114 ||
      mir2::legacy_decode_string(gold_show->body) != "Gold" || gold_changed->message.recog != 380) {
    return 1;
  }
  mir2::LogicCommand pickup_gold;
  pickup_gold.kind = mir2::LogicCommandKind::pickup_item;
  pickup_gold.session_id = 7;
  pickup_gold.x = 10;
  pickup_gold.y = 10;
  static_cast<void>(runtime.route_logic_command(pickup_gold));
  const auto pickup_gold_dispatch = runtime.tick();
  if (!has_packet(pickup_gold_dispatch, mir2::kSmItemHide) ||
      !has_packet(pickup_gold_dispatch, mir2::kSmGoldChanged)) {
    return 1;
  }

  const auto gold_hide = find_packet(pickup_gold_dispatch, mir2::kSmItemHide);
  const auto gold_changed_back = find_packet(pickup_gold_dispatch, mir2::kSmGoldChanged);
  if (!gold_hide.has_value() || !gold_changed_back.has_value()) {
    return 1;
  }
  if (gold_hide->message.recog != gold_show->message.recog ||
      gold_hide->message.param != 10 || gold_hide->message.tag != 10 ||
      gold_changed_back->message.recog != 500) {
    return 1;
  }
  mir2::LogicCommand query_bag;
  query_bag.kind = mir2::LogicCommandKind::query_bag_items;
  query_bag.session_id = 7;
  static_cast<void>(runtime.route_logic_command(query_bag));
  const auto bag_dispatch = runtime.tick();
  const auto bag_packet = find_packet(bag_dispatch, mir2::kSmBagItems);
  if (!bag_packet.has_value()) {
    return 1;
  }

  const auto bag_items = decode_bag_items(bag_packet->body);
  if (bag_items.size() != 1 || bag_items.front().make_index != 1001 ||
      bag_items.front().item.std_mode != 5 || mir2::to_string(bag_items.front().item.name) != "Wooden Sword") {
    return 1;
  }

  {
    mir2::HostConfig env_config;
    env_config.maps.push_back(mir2::MapConfig{"0", "ItemEnvMap", {}, 20, 20, 10, 10});
    env_config.items.push_back(mir2::ItemConfig{1, "Token", 1, 1, 1, 0, 2, 1, -1, 0, 0});

    mir2::LogicRuntime env_runtime(env_config);
    env_runtime.initialize();

    mir2::CharacterRecord collector;
    collector.account_id = "collector";
    collector.character_name = "Collector";
    collector.map_id = "0";
    collector.x = 10;
    collector.y = 10;
    collector.ability.max_weight = 100;
    for (int i = 0; i < 6; ++i) {
      collector.bag_items[static_cast<std::size_t>(i)].index = 1;
      collector.bag_items[static_cast<std::size_t>(i)].make_index = 3001 + i;
      collector.bag_items[static_cast<std::size_t>(i)].dura = 1;
      collector.bag_items[static_cast<std::size_t>(i)].dura_max = 1;
    }

    mir2::LogicCommand enter_collector;
    enter_collector.kind = mir2::LogicCommandKind::enter_world;
    enter_collector.session_id = 17;
    enter_collector.account_id = collector.account_id;
    enter_collector.character_name = collector.character_name;
    enter_collector.map_id = collector.map_id;
    enter_collector.x = collector.x;
    enter_collector.y = collector.y;
    enter_collector.character = collector;
    static_cast<void>(env_runtime.route_logic_command(enter_collector));
    static_cast<void>(env_runtime.tick());

    auto drop_token = [&](std::int32_t make_index) {
      static_cast<void>(env_runtime.route_logic_command(
          make_item_command(mir2::LogicCommandKind::drop_item, 17, make_index, "Token")));
      return env_runtime.tick();
    };

    static_cast<void>(drop_token(3001));
    static_cast<void>(drop_token(3002));

    mir2::LogicCommand pickup_first;
    pickup_first.kind = mir2::LogicCommandKind::pickup_item;
    pickup_first.session_id = 17;
    pickup_first.x = 10;
    pickup_first.y = 10;
    static_cast<void>(env_runtime.route_logic_command(pickup_first));
    const auto fifo_pickup = env_runtime.tick();
    const auto fifo_add = find_packet(fifo_pickup, mir2::kSmAddItem);
    if (!fifo_add.has_value()) {
      return 1;
    }
    const auto fifo_item = decode_client_item(fifo_add->body);
    if (!fifo_item.has_value() || fifo_item->make_index != 3001) {
      return 1;
    }

    static_cast<void>(drop_token(3001));
    static_cast<void>(drop_token(3003));
    static_cast<void>(drop_token(3004));
    const auto capped_drop = drop_token(3005);
    if (!has_packet(capped_drop, mir2::kSmDropItemFail) ||
        has_packet(capped_drop, mir2::kSmDropItemSuccess)) {
      return 1;
    }

    mir2::LogicCommand query_env_bag;
    query_env_bag.kind = mir2::LogicCommandKind::query_bag_items;
    query_env_bag.session_id = 17;
    static_cast<void>(env_runtime.route_logic_command(query_env_bag));
    const auto env_bag_dispatch = env_runtime.tick();
    const auto env_bag_packet = find_packet(env_bag_dispatch, mir2::kSmBagItems);
    if (!env_bag_packet.has_value()) {
      return 1;
    }
    const auto env_bag_items = decode_bag_items(env_bag_packet->body);
    if (std::none_of(env_bag_items.begin(), env_bag_items.end(),
                     [](const mir2::LegacyClientItem& item) {
                       return item.make_index == 3005;
                     })) {
      return 1;
    }
  }

  {
    mir2::HostConfig gold_config;
    gold_config.maps.push_back(mir2::MapConfig{"0", "GoldEnvMap", {}, 20, 20, 10, 10});
    mir2::LogicRuntime gold_runtime(gold_config);
    gold_runtime.initialize();

    mir2::CharacterRecord banker;
    banker.account_id = "banker";
    banker.character_name = "Banker";
    banker.map_id = "0";
    banker.x = 10;
    banker.y = 10;
    banker.gold = 1000;

    mir2::LogicCommand enter_banker;
    enter_banker.kind = mir2::LogicCommandKind::enter_world;
    enter_banker.session_id = 27;
    enter_banker.account_id = banker.account_id;
    enter_banker.character_name = banker.character_name;
    enter_banker.map_id = banker.map_id;
    enter_banker.x = banker.x;
    enter_banker.y = banker.y;
    enter_banker.character = banker;
    static_cast<void>(gold_runtime.route_logic_command(enter_banker));
    static_cast<void>(gold_runtime.tick());

    static_cast<void>(gold_runtime.route_logic_command(make_gold_command(27, 120)));
    static_cast<void>(gold_runtime.tick());
    static_cast<void>(gold_runtime.route_logic_command(make_gold_command(27, 80)));
    static_cast<void>(gold_runtime.tick());

    mir2::LogicCommand pickup_merged_gold;
    pickup_merged_gold.kind = mir2::LogicCommandKind::pickup_item;
    pickup_merged_gold.session_id = 27;
    pickup_merged_gold.x = 10;
    pickup_merged_gold.y = 10;
    static_cast<void>(gold_runtime.route_logic_command(pickup_merged_gold));
    const auto merged_gold_pickup = gold_runtime.tick();
    const auto merged_gold_changed = find_packet(merged_gold_pickup, mir2::kSmGoldChanged);
    if (!merged_gold_changed.has_value() || merged_gold_changed->message.recog != 1000) {
      return 1;
    }
  }

  return 0;
}
