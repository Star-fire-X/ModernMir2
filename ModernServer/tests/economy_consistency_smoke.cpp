#include <algorithm>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "util/string_utils.hpp"
#include "world/legacy_item_rules.hpp"
#include "world/legacy_map_environment.hpp"
#include "world/logic_runtime.hpp"

namespace {

constexpr int kLegacyTradeStableMs = 1000;
constexpr int kTestTickMs = 10;
constexpr int kTradeStableSafetyTicks = 10;
constexpr int kTradeStableTicks =
    (kLegacyTradeStableMs + kTestTickMs - 1) / kTestTickMs + kTradeStableSafetyTicks;

int fail(std::string_view stage) {
  std::cerr << "economy_consistency_smoke failed at " << stage << '\n';
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

mir2::RuntimeDispatch tick_players(mir2::LogicRuntime& runtime, int count = 40) {
  mir2::RuntimeDispatch dispatch;
  for (int i = 0; i < count; ++i) {
    append_dispatch(dispatch, runtime.tick());
  }
  return dispatch;
}

mir2::RuntimeDispatch tick_past_trade_stable_window(mir2::LogicRuntime& runtime) {
  return tick_players(runtime, kTradeStableTicks);
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

bool has_merchant_save(const mir2::RuntimeDispatch& dispatch) {
  return std::any_of(dispatch.persist_requests.begin(), dispatch.persist_requests.end(),
                     [](const mir2::PersistRequest& request) {
                       return request.kind == mir2::PersistRequestKind::save_merchant_state;
                     });
}

mir2::LegacyUserItem make_item(std::int32_t index, std::int32_t make_index) {
  mir2::LegacyUserItem item;
  item.index = static_cast<std::uint16_t>(index);
  item.make_index = make_index;
  item.dura = 1000;
  item.dura_max = 1000;
  return item;
}

mir2::LogicCommand enter_command(std::uint64_t session_id,
                                 const mir2::CharacterRecord& character) {
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

mir2::LogicCommand npc_command(mir2::LogicCommandKind kind, std::uint64_t session_id,
                               std::uint64_t npc_id, std::string text,
                               std::int32_t make_index = 0) {
  mir2::LogicCommand command;
  command.kind = kind;
  command.session_id = session_id;
  command.target_actor_id = npc_id;
  command.text = std::move(text);
  command.item_make_index = make_index;
  return command;
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

int count_bag_make_index(mir2::LogicRuntime& runtime, std::uint64_t session_id,
                         std::int32_t make_index) {
  const auto bag = query_bag(runtime, session_id);
  return static_cast<int>(std::count_if(bag.begin(), bag.end(), [&](const auto& item) {
    return item.make_index == make_index;
  }));
}

std::optional<mir2::LegacyClientItem> decode_client_item(std::string_view body) {
  mir2::LegacyClientItem item;
  if (!mir2::legacy_decode_buffer(body, &item, sizeof(item))) {
    return std::nullopt;
  }
  return item;
}

std::optional<std::pair<std::int32_t, std::int32_t>> item_show_position(
    const mir2::RuntimeDispatch& dispatch, std::string_view name) {
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == mir2::kSmItemShow &&
        mir2::legacy_decode_string(decoded->body) == name) {
      return std::pair{decoded->message.param, decoded->message.tag};
    }
  }
  return std::nullopt;
}

mir2::LogicCommand attack_command(std::uint64_t session_id,
                                  std::uint64_t target_actor_id) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::attack;
  command.session_id = session_id;
  command.target_actor_id = target_actor_id;
  command.x = 10;
  command.y = 9;
  command.game_message.ident = mir2::kCmHit;
  return command;
}

mir2::LogicCommand pickup_command(std::uint64_t session_id, std::int32_t x,
                                  std::int32_t y) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::pickup_item;
  command.session_id = session_id;
  command.x = x;
  command.y = y;
  return command;
}

mir2::HostConfig make_trade_config() {
  mir2::HostConfig config;
  config.budgets.tick_ms = kTestTickMs;
  config.maps.push_back(mir2::MapConfig{"0", "TradeMap", {}, 0, 0, 30, 30});
  config.items.push_back(mir2::ItemConfig{1, "Ruby", 1, 40, 0, 2, 1, 1000, 10, 0, 0});
  config.items.push_back(mir2::ItemConfig{2, "Sapphire", 1, 41, 0, 3, 1, 1000, 10, 0, 0});
  return config;
}

mir2::CharacterRecord make_trade_character(std::string account, std::string name,
                                           std::int32_t x, std::uint8_t dir,
                                           mir2::LegacyUserItem item) {
  mir2::CharacterRecord character;
  character.account_id = std::move(account);
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = x;
  character.y = 10;
  character.dir = dir;
  character.gold = 100;
  character.ability.level = 30;
  character.ability.max_hp = 50;
  character.ability.max_mp = 50;
  character.ability.max_exp = 100;
  character.ability.max_weight = 100;
  character.bag_items[0] = item;
  return character;
}

int trade_duplicate_make_index_cancels_and_restores() {
  mir2::LogicRuntime runtime(make_trade_config());
  runtime.initialize();
  auto hero_a = make_trade_character("a", "HeroA", 10, 2, make_item(1, 1001));
  auto hero_b = make_trade_character("b", "HeroB", 11, 6, make_item(2, 2001));
  hero_b.storage_items[0] = make_item(1, 1001);
  static_cast<void>(runtime.route_logic_command(enter_command(7, hero_a)));
  static_cast<void>(runtime.route_logic_command(enter_command(8, hero_b)));
  static_cast<void>(tick_players(runtime));

  static_cast<void>(runtime.route_logic_command(
      trade_command(mir2::LogicCommandKind::trade_try, 7, 0, "HeroB")));
  static_cast<void>(tick_players(runtime));
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

  const auto hero_a_after = runtime.snapshot_character_actor("HeroA");
  const auto hero_b_after = runtime.snapshot_character_actor("HeroB");
  if (!find_packet(dispatch, 7, mir2::kSmDealCancel).has_value() ||
      !find_packet(dispatch, 8, mir2::kSmDealCancel).has_value() ||
      find_packet(dispatch, 7, mir2::kSmDealSuccess).has_value() ||
      find_packet(dispatch, 8, mir2::kSmDealSuccess).has_value() ||
      !hero_a_after.has_value() || !hero_b_after.has_value() ||
      hero_a_after->gold != 100 || hero_b_after->gold != 100 ||
      count_bag_make_index(runtime, 7, 1001) != 1 ||
      count_bag_make_index(runtime, 8, 2001) != 1 ||
      hero_b_after->storage_items[0].make_index != 1001) {
    return fail("trade duplicate make-index restore");
  }
  return 0;
}

int buy_failure_keeps_merchant_order_and_player_state() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "ShopMap", {}, 0, 0, 20, 20});
  config.items.push_back(mir2::ItemConfig{1, "Potion", 1, 40, 0, 1, 1, 1000, -1, 0, 0});
  config.items.push_back(mir2::ItemConfig{2, "Elixir", 1, 60, 0, 1, 2, 1000, -1, 0, 0});
  config.npcs.push_back(mir2::NpcConfig{"merchant", "0", "Trader", 11, 10, "merchant.txt",
                                         "sell_repair", {1, 2}});
  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  mir2::CharacterRecord hero;
  hero.account_id = "shop";
  hero.character_name = "ShopHero";
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;
  hero.gold = 0;
  hero.ability.max_hp = 10;
  hero.ability.max_mp = 10;
  hero.ability.max_exp = 100;
  hero.ability.max_weight = 100;
  static_cast<void>(runtime.route_logic_command(enter_command(9, hero)));
  static_cast<void>(tick_players(runtime));

  static_cast<void>(runtime.route_logic_command(
      npc_command(mir2::LogicCommandKind::buy_item, 9, 1, "Potion")));
  const auto failed_buy = tick_players(runtime);
  if (!find_packet(failed_buy, 9, mir2::kSmBuyItemFail).has_value() ||
      has_save_character(failed_buy, "ShopHero") || has_merchant_save(failed_buy) ||
      !runtime.snapshot_character_actor("ShopHero").has_value() ||
      runtime.snapshot_character_actor("ShopHero")->gold != 0 ||
      !query_bag(runtime, 9).empty()) {
    return fail("buy failure player state");
  }

  static_cast<void>(runtime.route_logic_command(
      npc_command(mir2::LogicCommandKind::merchant_select, 9, 1, "@buy")));
  const auto goods = tick_players(runtime);
  const auto goods_packet = find_packet(goods, 9, mir2::kSmSendGoodsList);
  if (!goods_packet.has_value()) {
    return fail("buy failure goods packet");
  }
  const auto body = mir2::legacy_decode_string(goods_packet->body);
  const auto potion = body.find("Potion/");
  const auto elixir = body.find("Elixir/");
  if (potion == std::string::npos || elixir == std::string::npos || potion > elixir) {
    return fail("buy failure merchant order");
  }
  return 0;
}

int storage_failure_does_not_persist_or_reorder() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "StorageMap", {}, 0, 0, 20, 20});
  config.items.push_back(mir2::ItemConfig{1, "Storage Sword", 3, 90, 5, 1, 1, 1000, 1, 0, 0});
  config.npcs.push_back(
      mir2::NpcConfig{"storage", "0", "Keeper", 11, 10, "storage.txt", "storage"});
  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  mir2::CharacterRecord hero;
  hero.account_id = "storage";
  hero.character_name = "StorageHero";
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;
  hero.ability.max_hp = 10;
  hero.ability.max_mp = 10;
  hero.ability.max_exp = 100;
  hero.ability.max_weight = 100;
  hero.bag_items[0] = make_item(1, 4001);
  hero.bag_items[1] = make_item(1, 4002);
  for (std::size_t index = 0; index < hero.storage_items.size(); ++index) {
    hero.storage_items[index] = make_item(1, static_cast<std::int32_t>(5000 + index));
  }
  static_cast<void>(runtime.route_logic_command(enter_command(10, hero)));
  static_cast<void>(tick_players(runtime));

  static_cast<void>(runtime.route_logic_command(npc_command(
      mir2::LogicCommandKind::storage_item, 10, 1, "Storage Sword", 4001)));
  const auto dispatch = tick_players(runtime);
  const auto snapshot = runtime.snapshot_character_actor("StorageHero");
  if (!find_packet(dispatch, 10, mir2::kSmStorageFull).has_value() ||
      find_packet(dispatch, 10, mir2::kSmDelItem).has_value() ||
      has_save_character(dispatch, "StorageHero") || !snapshot.has_value() ||
      snapshot->bag_items[0].make_index != 4001 ||
      snapshot->bag_items[1].make_index != 4002 ||
      snapshot->storage_items[0].make_index != 5000) {
    return fail("storage failure state");
  }
  return 0;
}

mir2::HostConfig make_death_drop_config() {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 7;
  config.budgets.tick_ms = kTestTickMs;
  mir2::MapConfig map{"0", "DeathDropMap", {}, 0, 0, 20, 20};
  map.allow_pk = true;
  config.maps.push_back(map);
  auto make_death_item = [](std::int32_t id, std::string name, std::int32_t std_mode,
                            std::int32_t item_desc = 0) {
    mir2::ItemConfig item;
    item.id = id;
    item.name = std::move(name);
    item.std_mode = std_mode;
    item.item_desc = item_desc;
    item.weight = 1;
    item.dura_max = 1000;
    item.price = 10;
    item.stock = 1;
    return item;
  };
  auto sword = make_death_item(1, "Heavy Sword", 5);
  sword.dc = mir2::make_word(100, 100);
  config.items.push_back(sword);
  config.items.push_back(
      make_death_item(2, "Fragile Ring", 22, mir2::kLegacyItemDieAndBreak));
  config.items.push_back(make_death_item(3, "Drop Gem", 41));
  config.items.push_back(
      make_death_item(4, "Soul Token", 41, mir2::kLegacyItemNeverLose));
  config.items.push_back(make_death_item(5, "Raw Meat", 40));
  return config;
}

mir2::CharacterRecord make_death_character(std::string account, std::string name,
                                           std::int32_t x, std::int32_t y) {
  mir2::CharacterRecord character;
  character.account_id = std::move(account);
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = x;
  character.y = y;
  character.gold = 1234;
  character.ability.level = 30;
  character.ability.hp = 100;
  character.ability.max_hp = 100;
  character.ability.mp = 20;
  character.ability.max_mp = 20;
  character.ability.dc = mir2::make_word(120, 120);
  character.ability.max_exp = 1000;
  character.ability.max_weight = 100;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  character.attack_mode = 0;
  return character;
}

int player_death_drop_conserves_item_domains() {
  mir2::LogicRuntime runtime(make_death_drop_config());
  runtime.initialize();

  auto killer = make_death_character("death_a", "DeathKiller", 10, 10);
  killer.equipped_items[mir2::kEquipWeapon] = make_item(1, 1001);
  static_cast<void>(runtime.route_logic_command(enter_command(20, killer)));
  static_cast<void>(tick_players(runtime));

  auto victim = make_death_character("death_b", "DeathVictim", 10, 9);
  victim.ability.hp = 10;
  victim.ability.max_hp = 10;
  victim.pk_point = 250;
  victim.equipped_items[mir2::kEquipRingLeft] = make_item(2, 2001);
  victim.bag_items[0] = make_item(5, 2002);
  victim.bag_items[0].dura = 3500;
  victim.bag_items[0].dura_max = 3500;
  victim.bag_items[1] = make_item(3, 2003);
  victim.bag_items[2] = make_item(4, 2004);
  static_cast<void>(runtime.route_logic_command(enter_command(23, victim)));
  const auto enter_victim = tick_players(runtime);
  const auto victim_map = find_packet(enter_victim, 23, mir2::kSmNewMap);
  if (!victim_map.has_value()) {
    return fail("death drop victim enter");
  }
  const auto victim_actor_id =
      static_cast<std::uint64_t>(static_cast<std::uint32_t>(victim_map->message.recog));

  static_cast<void>(runtime.route_logic_command(attack_command(20, victim_actor_id)));
  const auto death = runtime.tick(100000);
  const auto victim_after = runtime.snapshot_character_actor("DeathVictim");
  if (!find_packet(death, 20, mir2::kSmDeath).has_value() ||
      !find_packet(death, 23, mir2::kSmNowDeath).has_value()) {
    return fail("death drop packets");
  }
  const auto gem_position = item_show_position(death, "Drop Gem");
  const auto meat_position = item_show_position(death, "Raw Meat");
  if (!gem_position.has_value() || !meat_position.has_value()) {
    return fail("death drop positions");
  }
  if (find_packet(death, 23, mir2::kSmGoldChanged).has_value()) {
    return fail("death drop gold changed");
  }
  if (!has_save_character(death, "DeathVictim")) {
    return fail("death drop save");
  }
  if (!victim_after.has_value() || victim_after->gold != 1234) {
    return fail("death drop victim gold snapshot");
  }
  if (!mir2::is_empty(victim_after->equipped_items[mir2::kEquipRingLeft]) ||
      victim_after->bag_items[0].make_index != 2004 ||
      !mir2::is_empty(victim_after->bag_items[1])) {
    return fail("death drop source domains");
  }

  auto gem_looter = make_death_character("death_gem", "GemLooter",
                                         gem_position->first, gem_position->second);
  static_cast<void>(runtime.route_logic_command(enter_command(21, gem_looter)));
  static_cast<void>(tick_players(runtime));
  auto meat_looter = make_death_character("death_meat", "MeatLooter",
                                          meat_position->first, meat_position->second);
  static_cast<void>(runtime.route_logic_command(enter_command(22, meat_looter)));
  static_cast<void>(tick_players(runtime));

  static_cast<void>(runtime.route_logic_command(
      pickup_command(21, gem_position->first, gem_position->second)));
  const auto gem_pickup = tick_players(runtime);
  const auto gem_add = find_packet(gem_pickup, 21, mir2::kSmAddItem);
  static_cast<void>(runtime.route_logic_command(
      pickup_command(22, meat_position->first, meat_position->second)));
  const auto meat_pickup = tick_players(runtime);
  const auto meat_add = find_packet(meat_pickup, 22, mir2::kSmAddItem);
  const auto gem = gem_add.has_value() ? decode_client_item(gem_add->body) : std::nullopt;
  const auto meat = meat_add.has_value() ? decode_client_item(meat_add->body) : std::nullopt;
  if (!find_packet(gem_pickup, 21, mir2::kSmItemHide).has_value() ||
      !find_packet(meat_pickup, 22, mir2::kSmItemHide).has_value()) {
    return fail("death drop pickup hide");
  }
  if (!gem.has_value() || !meat.has_value()) {
    return fail("death drop pickup add");
  }
  if (gem->make_index != 2003 || meat->make_index != 2002 ||
      meat->item.std_mode != 40 || meat->dura != 1500) {
    return fail("death drop pickup items");
  }
  const auto victim_after_pickup = runtime.snapshot_character_actor("DeathVictim");
  if (count_bag_make_index(runtime, 21, 2003) != 1 ||
      count_bag_make_index(runtime, 22, 2002) != 1 || !victim_after_pickup.has_value() ||
      !mir2::is_empty(victim_after_pickup->equipped_items[mir2::kEquipRingLeft]) ||
      victim_after_pickup->bag_items[0].make_index != 2004 ||
      !mir2::is_empty(victim_after_pickup->bag_items[1])) {
    return fail("death drop pickup domains");
  }
  return 0;
}

mir2::HostConfig make_script_config() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "ScriptMap", {}, 0, 0, 20, 20});
  config.items.push_back(mir2::ItemConfig{1, "Apple", 1, 10, 0, 0, 1, 1000, -1, 0, 0});
  config.items.push_back(mir2::ItemConfig{2, "Anvil", 1, 10, 0, 0, 2, 1000, -1, 0, 0});
  mir2::NpcConfig npc;
  npc.id = "script";
  npc.map_id = "0";
  npc.name = "Script";
  npc.x = 11;
  npc.y = 10;
  npc.service = "none";
  npc.dialog_sections.push_back(
      {"@takefail", "#IF\\CHECKLEVEL 1\\#ACT\\#TAKE Apple 2\\#SAY Done"});
  npc.dialog_sections.push_back(
      {"@takecheckfail", "#IF\\CHECKLEVEL 1\\#ACT\\#TAKECHECKITEM Apple 2\\#SAY Done"});
  npc.dialog_sections.push_back(
      {"@givepartial", "#IF\\CHECKLEVEL 1\\#ACT\\#GIVE Apple 2\\#SAY Done"});
  npc.dialog_sections.push_back(
      {"@goldcap", "#IF\\CHECKLEVEL 1\\#ACT\\#GIVE Gold 2\\#SAY Done"});
  config.npcs.push_back(npc);
  return config;
}

mir2::CharacterRecord make_script_character(std::string name) {
  mir2::CharacterRecord hero;
  hero.account_id = "script";
  hero.character_name = std::move(name);
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;
  hero.ability.level = 10;
  hero.ability.max_hp = 10;
  hero.ability.max_mp = 10;
  hero.ability.max_exp = 100;
  hero.ability.max_weight = 100;
  return hero;
}

int script_take_failure_keeps_order() {
  mir2::LogicRuntime runtime(make_script_config());
  runtime.initialize();
  auto hero = make_script_character("ScriptHero");
  hero.bag_items[0] = make_item(1, 6001);
  hero.bag_items[1] = make_item(2, 6002);
  static_cast<void>(runtime.route_logic_command(enter_command(11, hero)));
  static_cast<void>(tick_players(runtime));

  static_cast<void>(runtime.route_logic_command(
      npc_command(mir2::LogicCommandKind::merchant_select, 11, 1, "@takefail")));
  const auto take_fail = tick_players(runtime);
  auto snapshot = runtime.snapshot_character_actor("ScriptHero");
  if (!snapshot.has_value() || snapshot->bag_items[0].make_index != 6001 ||
      snapshot->bag_items[1].make_index != 6002 ||
      has_save_character(take_fail, "ScriptHero")) {
    return fail("script TAKE failure order");
  }

  static_cast<void>(runtime.route_logic_command(
      npc_command(mir2::LogicCommandKind::merchant_select, 11, 1, "@takecheckfail")));
  const auto takecheck_fail = tick_players(runtime);
  snapshot = runtime.snapshot_character_actor("ScriptHero");
  if (!snapshot.has_value() || snapshot->bag_items[0].make_index != 6001 ||
      snapshot->bag_items[1].make_index != 6002 ||
      has_save_character(takecheck_fail, "ScriptHero")) {
    return fail("script TAKECHECKITEM failure order");
  }
  return 0;
}

int script_give_partial_and_gold_cap() {
  mir2::LogicRuntime partial_runtime(make_script_config());
  partial_runtime.initialize();
  auto partial = make_script_character("PartialHero");
  for (std::size_t index = 0; index + 1 < partial.bag_items.size(); ++index) {
    partial.bag_items[index] = make_item(2, static_cast<std::int32_t>(7000 + index));
  }
  static_cast<void>(partial_runtime.route_logic_command(enter_command(12, partial)));
  static_cast<void>(tick_players(partial_runtime));
  static_cast<void>(partial_runtime.route_logic_command(
      npc_command(mir2::LogicCommandKind::merchant_select, 12, 1, "@givepartial")));
  const auto partial_dispatch = tick_players(partial_runtime);
  const auto partial_snapshot = partial_runtime.snapshot_character_actor("PartialHero");
  if (!partial_snapshot.has_value() || !has_save_character(partial_dispatch, "PartialHero") ||
      std::count_if(partial_snapshot->bag_items.begin(), partial_snapshot->bag_items.end(),
                    [](const mir2::LegacyUserItem& item) {
                      return !mir2::is_empty(item) && item.index == 1;
                    }) != 1) {
    return fail("script GIVE partial");
  }

  mir2::LogicRuntime gold_runtime(make_script_config());
  gold_runtime.initialize();
  auto capped = make_script_character("GoldHero");
  capped.gold = mir2::kLegacyBagGold - 1;
  static_cast<void>(gold_runtime.route_logic_command(enter_command(13, capped)));
  static_cast<void>(tick_players(gold_runtime));
  static_cast<void>(gold_runtime.route_logic_command(
      npc_command(mir2::LogicCommandKind::merchant_select, 13, 1, "@goldcap")));
  const auto gold_dispatch = tick_players(gold_runtime);
  const auto gold_snapshot = gold_runtime.snapshot_character_actor("GoldHero");
  if (!gold_snapshot.has_value() || gold_snapshot->gold != mir2::kLegacyBagGold - 1 ||
      has_save_character(gold_dispatch, "GoldHero")) {
    return fail("script GIVE Gold cap");
  }
  return 0;
}

}  // namespace

int main() {
  if (const auto result = trade_duplicate_make_index_cancels_and_restores(); result != 0) {
    return result;
  }
  if (const auto result = buy_failure_keeps_merchant_order_and_player_state(); result != 0) {
    return result;
  }
  if (const auto result = storage_failure_does_not_persist_or_reorder(); result != 0) {
    return result;
  }
  if (const auto result = player_death_drop_conserves_item_domains(); result != 0) {
    return result;
  }
  if (const auto result = script_take_failure_keeps_order(); result != 0) {
    return result;
  }
  if (const auto result = script_give_partial_and_gold_cap(); result != 0) {
    return result;
  }
  return 0;
}
