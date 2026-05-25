#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

int fail(std::string_view stage) {
  std::fprintf(stderr, "player_death_operation_guard_smoke failed at %.*s\n",
               static_cast<int>(stage.size()), stage.data());
  return 1;
}

void append_dispatch(mir2::RuntimeDispatch& target, mir2::RuntimeDispatch source) {
  target.session_events.insert(target.session_events.end(),
                               std::make_move_iterator(source.session_events.begin()),
                               std::make_move_iterator(source.session_events.end()));
  target.persist_requests.insert(target.persist_requests.end(),
                                 std::make_move_iterator(source.persist_requests.begin()),
                                 std::make_move_iterator(source.persist_requests.end()));
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

bool has_save_character(const mir2::RuntimeDispatch& dispatch, std::string_view name) {
  return std::any_of(dispatch.persist_requests.begin(), dispatch.persist_requests.end(),
                     [&](const mir2::PersistRequest& request) {
                       return request.kind == mir2::PersistRequestKind::save_character &&
                              request.character_name == name;
                     });
}

mir2::ItemConfig item(std::int32_t id, std::string name, std::int32_t std_mode) {
  mir2::ItemConfig config;
  config.id = id;
  config.name = std::move(name);
  config.std_mode = std_mode;
  config.weight = 1;
  config.price = 10;
  config.looks = id;
  config.dura_max = 1000;
  config.stock = 1;
  config.dc = mir2::make_word(50, 50);
  return config;
}

mir2::LegacyUserItem user_item(std::int32_t index, std::int32_t make_index) {
  mir2::LegacyUserItem item;
  item.index = static_cast<std::uint16_t>(index);
  item.make_index = make_index;
  item.dura = 1000;
  item.dura_max = 1000;
  return item;
}

mir2::CharacterRecord character(std::string account, std::string name,
                                std::int32_t x, std::int32_t y) {
  mir2::CharacterRecord record;
  record.account_id = std::move(account);
  record.character_name = std::move(name);
  record.map_id = "0";
  record.x = x;
  record.y = y;
  record.gold = 100;
  record.ability.level = 30;
  record.ability.hp = 30;
  record.ability.max_hp = 30;
  record.ability.mp = 30;
  record.ability.max_mp = 30;
  record.ability.dc = mir2::make_word(100, 100);
  record.ability.max_exp = 1000;
  record.ability.max_weight = 100;
  record.ability.max_wear_weight = 100;
  record.ability.max_hand_weight = 100;
  record.attack_mode = 0;
  return record;
}

mir2::HostConfig make_config() {
  mir2::HostConfig config;
  mir2::MapConfig map{"0", "DeathGuard", {}, 0, 0, 20, 20};
  map.allow_pk = true;
  map.fight_zone = true;
  config.maps.push_back(map);
  config.items.push_back(item(1, "Potion", 1));
  config.items.push_back(item(2, "Sword", 5));
  config.items.push_back(item(3, "Ring", 22));
  config.items.push_back(item(4, "Stored", 41));
  config.npcs.push_back(mir2::NpcConfig{"merchant", "0", "Trader", 11, 9,
                                         "merchant.txt", "sell_repair", {1, 2}});
  config.npcs.push_back(mir2::NpcConfig{"storage", "0", "Warehouse", 9, 9,
                                         "storage.txt", "storage"});
  return config;
}

mir2::LogicCommand enter(std::uint64_t session_id, mir2::CharacterRecord record) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = session_id;
  command.account_id = record.account_id;
  command.character_name = record.character_name;
  command.map_id = record.map_id;
  command.x = record.x;
  command.y = record.y;
  command.character = std::move(record);
  return command;
}

mir2::LogicCommand command(mir2::LogicCommandKind kind, std::uint64_t session_id) {
  mir2::LogicCommand command;
  command.kind = kind;
  command.session_id = session_id;
  return command;
}

mir2::LogicCommand item_command(mir2::LogicCommandKind kind, std::uint64_t session_id,
                                std::int32_t make_index, std::string text,
                                std::int32_t slot = -1) {
  auto command = ::command(kind, session_id);
  command.item_make_index = make_index;
  command.text = std::move(text);
  command.item_slot = slot;
  return command;
}

mir2::LogicCommand npc_command(mir2::LogicCommandKind kind, std::uint64_t session_id,
                               std::uint64_t npc_id, std::int32_t make_index,
                               std::string text) {
  auto command = item_command(kind, session_id, make_index, std::move(text));
  command.target_actor_id = npc_id;
  return command;
}

std::optional<std::uint64_t> actor_id_from_login(const mir2::RuntimeDispatch& dispatch,
                                                 std::uint64_t session_id) {
  const auto packet = find_packet(dispatch, session_id, mir2::kSmNewMap);
  if (!packet.has_value()) {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(static_cast<std::uint32_t>(packet->message.recog));
}

}  // namespace

int main() {
  mir2::LogicRuntime runtime(make_config());
  runtime.initialize();

  auto killer = character("killer", "Killer", 10, 10);
  static_cast<void>(runtime.route_logic_command(enter(50, killer)));
  const auto killer_login = runtime.tick(1000);
  const auto killer_id = actor_id_from_login(killer_login, 50);
  if (!killer_id.has_value()) {
    return fail("killer enter");
  }

  auto victim = character("victim", "Victim", 10, 9);
  victim.ability.hp = 10;
  victim.bag_items[0] = user_item(1, 1001);
  victim.bag_items[1] = user_item(2, 1002);
  victim.equipped_items[mir2::kEquipRingLeft] = user_item(3, 1003);
  victim.storage_items[0] = user_item(4, 1004);
  static_cast<void>(runtime.route_logic_command(enter(51, victim)));
  const auto victim_login = runtime.tick(1251);
  const auto victim_id = actor_id_from_login(victim_login, 51);
  if (!victim_id.has_value()) {
    return fail("victim enter");
  }

  auto seed_gold = command(mir2::LogicCommandKind::drop_gold, 51);
  seed_gold.amount = 5;
  static_cast<void>(runtime.route_logic_command(seed_gold));
  static_cast<void>(runtime.tick(1502));

  auto attack = command(mir2::LogicCommandKind::attack, 50);
  attack.target_actor_id = *victim_id;
  attack.x = 10;
  attack.y = 9;
  attack.game_message.ident = mir2::kCmHit;
  static_cast<void>(runtime.route_logic_command(attack));
  const auto death = runtime.tick(5000);
  if (!find_packet(death, 51, mir2::kSmNowDeath).has_value()) {
    return fail("death");
  }
  const auto after_death = runtime.snapshot_character_actor("Victim");
  if (!after_death.has_value() || after_death->ability.hp != 0 ||
      after_death->gold != 95 || after_death->bag_items[1].make_index != 1002 ||
      after_death->equipped_items[mir2::kEquipRingLeft].make_index != 1003 ||
      after_death->storage_items[0].make_index != 1004) {
    return fail("death snapshot");
  }

  std::vector<mir2::LogicCommand> commands;
  commands.push_back(item_command(mir2::LogicCommandKind::eat_item, 51, 1001, "Potion"));
  commands.push_back(item_command(mir2::LogicCommandKind::drop_item, 51, 1002, "Sword"));
  auto drop_gold = command(mir2::LogicCommandKind::drop_gold, 51);
  drop_gold.amount = 5;
  commands.push_back(drop_gold);
  auto pickup = command(mir2::LogicCommandKind::pickup_item, 51);
  pickup.x = 10;
  pickup.y = 9;
  commands.push_back(pickup);
  commands.push_back(item_command(mir2::LogicCommandKind::take_on_item, 51, 1002,
                                  "Sword", mir2::kEquipWeapon));
  commands.push_back(item_command(mir2::LogicCommandKind::take_off_item, 51, 1003,
                                  "Ring", mir2::kEquipRingLeft));
  auto dead_attack = command(mir2::LogicCommandKind::attack, 51);
  dead_attack.target_actor_id = *killer_id;
  dead_attack.x = 10;
  dead_attack.y = 10;
  dead_attack.game_message.ident = mir2::kCmHit;
  commands.push_back(dead_attack);
  commands.push_back(command(mir2::LogicCommandKind::trade_try, 51));
  commands.push_back(npc_command(mir2::LogicCommandKind::buy_item, 51, 1, 1, "Potion"));
  commands.push_back(npc_command(mir2::LogicCommandKind::sell_item, 51, 1, 1002, "Sword"));
  commands.push_back(npc_command(mir2::LogicCommandKind::repair_item, 51, 1, 1002, "Sword"));
  commands.push_back(npc_command(mir2::LogicCommandKind::storage_item, 51, 2, 1002, "Sword"));
  commands.push_back(npc_command(mir2::LogicCommandKind::take_back_storage_item, 51, 2,
                                 1004, "Stored"));
  auto dead_click = command(mir2::LogicCommandKind::click_npc, 51);
  dead_click.target_actor_id = 1;
  commands.push_back(dead_click);

  mir2::RuntimeDispatch guard_dispatch;
  auto now_ms = 5251ULL;
  for (auto& action : commands) {
    static_cast<void>(runtime.route_logic_command(action));
    now_ms += 251;
    append_dispatch(guard_dispatch, runtime.tick(now_ms));
  }

  const std::vector<std::uint16_t> forbidden_packets{
      mir2::kSmEatOk, mir2::kSmDropItemSuccess, mir2::kSmGoldChanged,
      mir2::kSmItemShow, mir2::kSmItemHide, mir2::kSmAddItem, mir2::kSmDelItem,
      mir2::kSmUpdateItem, mir2::kSmWeightChanged, mir2::kSmSendUseItems,
      mir2::kSmAbility, mir2::kSmSubAbility, mir2::kSmBagItems,
      mir2::kSmTakeOnOk, mir2::kSmTakeOffOk, mir2::kSmHit, mir2::kSmStruck,
      mir2::kSmDealMenu, mir2::kSmBuyItemSuccess, mir2::kSmUserSellItemOk,
      mir2::kSmUserRepairItemOk, mir2::kSmStorageOk,
      mir2::kSmTakeBackStorageItemOk, mir2::kSmSendGoodsList,
      mir2::kSmSendUserStorageItem};
  for (const auto ident : forbidden_packets) {
    if (find_packet(guard_dispatch, 51, ident).has_value()) {
      return fail("dead command success packet");
    }
  }
  if (has_save_character(guard_dispatch, "Victim")) {
    return fail("dead command save");
  }

  const auto after_guards = runtime.snapshot_character_actor("Victim");
  if (!after_guards.has_value() || after_guards->gold != after_death->gold ||
      after_guards->ability.hp != 0 ||
      after_guards->bag_items[0].make_index != after_death->bag_items[0].make_index ||
      after_guards->bag_items[1].make_index != after_death->bag_items[1].make_index ||
      after_guards->equipped_items[mir2::kEquipRingLeft].make_index != 1003 ||
      after_guards->storage_items[0].make_index != 1004) {
    return fail("dead command state");
  }

  return 0;
}
