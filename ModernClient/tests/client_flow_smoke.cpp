#include <cassert>
#include <string>
#include <string_view>

#include "game/game_state.hpp"
#include "shared/legacy/action_ids.hpp"

int main() {
  using namespace mir2::client;
  using namespace mir2::client_v1;

  GameStateStore state;
  assert(state.auth_phase == AuthFlowPhase::EditingLogin);
  assert(state.login.pending_focus == LoginPendingFocus::none);
  struct PhaseNameExpectation {
    AuthFlowPhase phase;
    std::string_view name;
  };
  constexpr PhaseNameExpectation kPhaseNames[] = {
      {AuthFlowPhase::EditingLogin, "EditingLogin"},
      {AuthFlowPhase::ConnectingLoginGate, "ConnectingLoginGate"},
      {AuthFlowPhase::WaitingLoginResult, "WaitingLoginResult"},
      {AuthFlowPhase::WaitingServerList, "WaitingServerList"},
      {AuthFlowPhase::BrowsingServers, "BrowsingServers"},
      {AuthFlowPhase::WaitingServerSelection, "WaitingServerSelection"},
      {AuthFlowPhase::ConnectingCharacterGate, "ConnectingCharacterGate"},
      {AuthFlowPhase::QueryingCharacters, "QueryingCharacters"},
      {AuthFlowPhase::BrowsingCharacters, "BrowsingCharacters"},
      {AuthFlowPhase::WaitingStartPlay, "WaitingStartPlay"},
      {AuthFlowPhase::ConnectingRunGate, "ConnectingRunGate"},
      {AuthFlowPhase::EnteringWorld, "EnteringWorld"},
      {AuthFlowPhase::ViewingLoginNotice, "ViewingLoginNotice"},
      {AuthFlowPhase::WaitingWorldSnapshot, "WaitingWorldSnapshot"},
      {AuthFlowPhase::InGame, "InGame"},
      {AuthFlowPhase::Disconnected, "Disconnected"},
  };
  for (const auto& expected : kPhaseNames) {
    assert(auth_flow_phase_name(expected.phase) == expected.name);
  }

  state.login.account_id = "guest";
  state.login.password = "pass";
  state.connection_phase = GameStateStore::ConnectionPhase::login;
  state.auth_phase = AuthFlowPhase::ConnectingLoginGate;
  assert(state.auth_phase == AuthFlowPhase::ConnectingLoginGate);
  assert(state.connection_phase == GameStateStore::ConnectionPhase::login);
  state.auth_phase = AuthFlowPhase::WaitingLoginResult;

  LoginResult login_result{true, 0, "guest", "Guest", ""};
  assert(login_result.success);
  assert(login_result.account_id == "guest");
  state.auth_phase = AuthFlowPhase::WaitingServerList;
  assert(state.auth_phase == AuthFlowPhase::WaitingServerList);

  LoginResult failed_login{false, -1, "guest", "", "login_failed"};
  assert(!failed_login.success);
  state.login.password = "pass";
  state.auth_phase = AuthFlowPhase::EditingLogin;
  state.login.pending_focus = LoginPendingFocus::password;
  assert(state.auth_phase == AuthFlowPhase::EditingLogin);
  assert(state.login.pending_focus == LoginPendingFocus::password);
  assert(state.login.password == "pass");
  state.login.pending_focus = LoginPendingFocus::none;

  state.apply(ServerList{{ServerEntry{"ModernServer", "127.0.0.1", 5600}}});
  state.auth_phase = AuthFlowPhase::BrowsingServers;
  assert(state.lobby.selected_server_name == "ModernServer");
  assert(state.auth_phase == AuthFlowPhase::BrowsingServers);

  state.auth_phase = AuthFlowPhase::WaitingServerSelection;
  state.apply(SelectServerResult{true, "ModernServer", "127.0.0.1", 5600, "lobby-token", ""});
  state.auth_phase = AuthFlowPhase::ConnectingCharacterGate;
  assert(state.connection_phase == GameStateStore::ConnectionPhase::select_character);
  assert(state.pending_lobby_token == "lobby-token");
  assert(state.auth_phase == AuthFlowPhase::ConnectingCharacterGate);

  CharacterList characters;
  characters.characters.push_back(CharacterSummary{"Hero", 1, 0, 0, 1, "0"});
  characters.selected_name = "Hero";
  state.auth_phase = AuthFlowPhase::QueryingCharacters;
  state.apply(characters);
  state.auth_phase = AuthFlowPhase::BrowsingCharacters;
  assert(state.lobby.selected_index == 0);
  assert(state.auth_phase == AuthFlowPhase::BrowsingCharacters);

  state.selected_character = "Hero";
  state.auth_phase = AuthFlowPhase::EnteringWorld;
  state.login_notice = LoginNoticeViewState{"Welcome", "Notice text"};
  state.auth_phase = AuthFlowPhase::ViewingLoginNotice;
  assert(state.login_notice.title == "Welcome");
  assert(state.auth_phase == AuthFlowPhase::ViewingLoginNotice);
  auto notice_ok_count = 0;
  auto acknowledge_notice = [&] {
    if (state.auth_phase != AuthFlowPhase::ViewingLoginNotice) {
      return;
    }
    ++notice_ok_count;
    state.login_notice = LoginNoticeViewState{};
    state.auth_phase = AuthFlowPhase::EnteringWorld;
  };
  acknowledge_notice();
  acknowledge_notice();
  assert(notice_ok_count == 1);
  assert(state.auth_phase == AuthFlowPhase::EnteringWorld);

  state.auth_phase = AuthFlowPhase::WaitingStartPlay;
  SelectCharacterResult select_character{true, "Hero", "world-token", "127.0.0.1", 5602, ""};
  state.enter_world_token = select_character.enter_world_token;
  state.pending_game_host = select_character.address;
  state.pending_game_port = select_character.port;
  state.connection_phase = GameStateStore::ConnectionPhase::play;
  state.auth_phase = AuthFlowPhase::ConnectingRunGate;
  assert(state.enter_world_token == "world-token");
  assert(state.pending_game_host == "127.0.0.1");
  assert(state.pending_game_port == 5602);
  assert(state.auth_phase == AuthFlowPhase::ConnectingRunGate);

  state.auth_phase = AuthFlowPhase::EnteringWorld;
  EnterWorldResult enter;
  enter.success = true;
  enter.self_actor_id = 1000;
  enter.character_name = "Hero";
  enter.map_id = "0";
  enter.x = 330;
  enter.y = 270;
  state.pending_self_actor_id = enter.self_actor_id;
  state.selected_character = enter.character_name;
  state.pending_spawn_x = enter.x;
  state.pending_spawn_y = enter.y;
  state.auth_phase = AuthFlowPhase::WaitingWorldSnapshot;
  assert(state.pending_self_actor_id == 1000);
  assert(state.pending_spawn_x == 330);
  assert(state.pending_spawn_y == 270);
  assert(state.auth_phase == AuthFlowPhase::WaitingWorldSnapshot);

  WorldSnapshot snapshot;
  snapshot.map_id = enter.map_id;
  snapshot.width = 700;
  snapshot.height = 700;
  snapshot.self_actor_id = enter.self_actor_id;
  snapshot.actors.push_back(
      WorldActor{enter.self_actor_id, enter.character_name, enter.x, enter.y, 0, 0, 0,
                 ActorType::player});
  state.world.action_locked = true;
  state.world.legacy_target_x = 1;
  state.world.legacy_chr_action = LegacyChrAction::walk;
  state.apply(snapshot);
  state.connection_phase = GameStateStore::ConnectionPhase::play;
  state.auth_phase = AuthFlowPhase::InGame;

  assert(state.world.self_actor_id == 1000);
  assert(state.world.actors.size() == 1);
  assert(!state.world.action_locked);
  assert(state.world.legacy_target_x == -1);
  assert(state.world.legacy_chr_action == LegacyChrAction::none);
  assert(state.connection_phase == GameStateStore::ConnectionPhase::play);
  assert(state.auth_phase == AuthFlowPhase::InGame);

  state.apply(ActorUpsert{
      WorldActor{2000, "Hen", 332, 271, 4, 0, 0, ActorType::monster}});
  assert(state.world.actors.size() == 2);
  assert(state.world.actors[2000].name == "Hen");
  state.world.focus_actor_id = 2000;
  state.world.target_actor_id = 2000;
  state.world.actors[1000].action_target_actor_id = 2000;
  state.apply(ActorRemove{2000});
  assert(state.world.actors.size() == 1);
  assert(state.world.actors.find(2000) == state.world.actors.end());
  assert(state.world.focus_actor_id == 0);
  assert(state.world.target_actor_id == 0);
  assert(state.world.actors[1000].action_target_actor_id == 0);

  state.apply(ActorStateDelta{1000, 331, 270, 2});
  assert(state.world.actors[1000].x == 331);
  assert(state.world.actors[1000].y == 270);
  assert(state.world.actors[1000].dir == 2);

  auto& flow_self = state.world.actors[1000];
  flow_self.from_x = 330;
  flow_self.from_y = 270;
  flow_self.x = 331;
  flow_self.y = 270;
  flow_self.current_action = ActorActionKind::walk;
  state.world.action_locked = true;
  state.apply(ActionAck{false, 1235});
  assert(!state.world.action_locked);
  assert(flow_self.x == 330 && flow_self.y == 270);
  assert(flow_self.current_action == ActorActionKind::turn);

  state.apply(ActorVitals{1000, 42, 55, 18, 24, 0, 0, false});
  assert(state.world.actors[1000].hp == 42);
  assert(state.world.actors[1000].mp == 18);

  SelfAbility ability;
  ability.level = 12;
  ability.job = 0;
  ability.exp = 3456;
  ability.max_exp = 10000;
  ability.weight = 21;
  ability.max_weight = 40;
  ability.gold = 12345;
  ability.hunger_state = 2;
  state.apply(ability);
  assert(state.world.self_ability.level == 12);
  assert(state.world.self_ability.exp == 3456);
  assert(state.world.self_ability.weight == 21);
  assert(state.world.self_ability_detail.level == 12);

  SelfAbilityDetail detail;
  detail.level = 13;
  detail.job = 1;
  detail.sex = 0;
  detail.hp = 50;
  detail.max_hp = 60;
  detail.mp = 40;
  detail.max_mp = 45;
  detail.ac = 3;
  detail.mac = 4;
  detail.dc = 5;
  detail.mc = 6;
  detail.sc = 7;
  detail.exp = 4567;
  detail.max_exp = 12000;
  detail.weight = 22;
  detail.max_weight = 44;
  detail.guild_name = "Guild";
  detail.guild_rank_name = "Rank";
  state.apply(detail);
  assert(state.world.self_ability.level == 13);
  assert(state.world.self_ability_detail.mc == 6);

  ItemState potion;
  potion.name = "Potion";
  potion.make_index = 1001;
  potion.looks = 7;
  potion.std_mode = 0;
  potion.dura = 10;
  potion.dura_max = 20;

  BagSnapshot bag_snapshot;
  bag_snapshot.items.push_back(ItemSlotState{0, potion});
  bag_snapshot.items.push_back(ItemSlotState{6, potion});
  state.apply(bag_snapshot);
  assert(state.world.bag_items[0].name == "Potion");
  assert(state.world.bag_items[6].name == "Potion");
  assert(state.world.bag_items[7].empty());

  ItemState torch = potion;
  torch.name = "Torch";
  torch.make_index = 1002;
  state.apply(InventoryUpdate{ItemSlotState{6, torch}});
  assert(state.world.bag_items[6].name == "Torch");
  state.apply(InventoryAdd{ItemSlotState{7, potion}});
  assert(state.world.bag_items[7].make_index == 1001);
  state.apply(InventoryRemove{6});
  assert(state.world.bag_items[6].empty());
  state.apply(InventoryClearRange{7, 7});
  assert(state.world.bag_items[7].empty());

  ItemState weapon = potion;
  weapon.name = "Sword";
  weapon.make_index = 3001;
  weapon.std_mode = 5;
  ItemState dress = potion;
  dress.name = "Armor";
  dress.make_index = 3002;
  dress.std_mode = 10;
  EquipmentSnapshot equipment_snapshot;
  equipment_snapshot.items.push_back(ItemSlotState{1, weapon});
  equipment_snapshot.items.push_back(ItemSlotState{0, dress});
  state.apply(equipment_snapshot);
  assert(state.world.equipment[1].name == "Sword");
  assert(state.world.equipment[0].name == "Armor");
  state.apply(DurabilityChange{3001, 12, 34});
  assert(state.world.equipment[1].dura == 12);
  assert(state.world.equipment[1].dura_max == 34);

  state.world.eating_item_slot = 1;
  state.world.eat_time_ms = 100;
  state.apply(UseItemResult{false});
  assert(state.world.eating_item_slot == -1);
  assert(state.world.eat_time_ms == 0);

  state.apply(ChatLine{"Hero: hello", 0xFFFFFFFFU, 0x00000000U});
  assert(!state.world.chat_lines.empty());
  assert(state.world.chat_lines.back().text == "Hero: hello");
  for (int index = 0; index < 205; ++index) {
    state.push_chat_line("m" + std::to_string(index), 0xFFFFFFFFU, 0x00000000U);
  }
  assert(state.world.chat_lines.size() == 200);
  assert(state.world.chat_lines.front().text == "m5");
  assert(state.world.chat_board_top ==
         static_cast<int>(state.world.chat_lines.size()) - 9);

  state.apply(ActorUpsert{
      WorldActor{2000, "Hen", 332, 271, 4, 0, 0, ActorType::monster}});
  assert(state.world.actors.size() == 2);
  assert(state.world.actors[2000].name == "Hen");

  state.apply(ActorSay{2000, "Hen: cluck", 0xFFFFFF00U, 0x00000000U});
  assert(state.world.actors[2000].saying == "Hen: cluck");
  assert(state.world.actors[2000].saying_started_ms != 0);
  assert(state.world.chat_lines.back().text == "Hen: cluck");

  state.apply(NpcDialog{2000, 384, "Shopkeeper/Line1\\Line2 <Buy/@buy>"});
  assert(state.world.npc_dialog.visible);
  assert(state.world.npc_dialog.merchant_id == 2000);
  assert(state.world.npc_dialog.face == 384);
  assert(state.world.npc_dialog.npc_name == "Shopkeeper");
  assert(state.world.npc_dialog.text == "Line1\nLine2 <Buy/@buy>");
  assert(state.world.npc_dialog.opened_x == 330);
  assert(state.world.npc_dialog.opened_y == 270);
  state.apply(NpcDialogClose{2000});
  assert(!state.world.npc_dialog.visible);

  state.apply(ActorAction{2000, ActorActionKind::hit, 332, 271, 4, 1000, 0, 14, 0, false});
  assert(state.world.actors[2000].current_action == ActorActionKind::turn);
  state.process_legacy_actor_queues(mir2::client::detail::monotonic_ms());
  assert(state.world.actors[2000].current_action == ActorActionKind::hit);
  assert(state.world.actors[2000].legacy_action_ident == 14);
  state.world.actors[2000].action_started_ms = 0;
  state.apply(ActorAction{2000, ActorActionKind::hit, 332, 271, 4, 1000, 0,
                          mir2::legacy::kCmPowerHit, 0, false});
  state.process_legacy_actor_queues(mir2::client::detail::monotonic_ms());
  assert(state.world.actors[2000].legacy_action_ident == mir2::legacy::kSmPowerHit);
  state.world.actors[2000].action_started_ms = 0;
  state.apply(ActorAction{2000, ActorActionKind::hit, 332, 271, 4, 1000, 0,
                          mir2::legacy::kCmLongHit, 0, false});
  state.process_legacy_actor_queues(mir2::client::detail::monotonic_ms());
  assert(state.world.actors[2000].legacy_action_ident == mir2::legacy::kSmLongHit);
  state.world.actors[2000].action_started_ms = 0;
  state.apply(ActorAction{2000, ActorActionKind::hit, 332, 271, 4, 1000, 0,
                          mir2::legacy::kCmWideHit, 0, false});
  state.process_legacy_actor_queues(mir2::client::detail::monotonic_ms());
  assert(state.world.actors[2000].legacy_action_ident == mir2::legacy::kSmWideHit);
  state.world.actors[2000].action_started_ms = 0;
  state.apply(ActorAction{2000, ActorActionKind::hit, 332, 271, 4, 1000, 0,
                          mir2::legacy::kCmFireHit, 0, false});
  state.process_legacy_actor_queues(mir2::client::detail::monotonic_ms());
  assert(state.world.actors[2000].legacy_action_ident == mir2::legacy::kSmFireHit);
  state.world.actors[2000].action_started_ms = 0;
  state.apply(ActorAction{2000, ActorActionKind::hit, 332, 271, 4, 1000, 0,
                          mir2::legacy::kCmCrossHit, 0, false});
  state.process_legacy_actor_queues(mir2::client::detail::monotonic_ms());
  assert(state.world.actors[2000].legacy_action_ident == mir2::legacy::kSmCrossHit);

  state.apply(ActorVitals{2000, 7, 12, -1, -1, 5, 1000, false});
  assert(state.world.actors[2000].hp == 7);
  assert(state.world.actors[2000].max_hp == 12);
  assert(state.world.actors[2000].last_damage == 5);

  state.apply(ActorAction{1000, ActorActionKind::spell, 335, 272, 3, 2000, 0, 0, 1, true, 3});
  state.process_legacy_actor_queues(mir2::client::detail::monotonic_ms());
  assert(state.world.actors[1000].current_action == ActorActionKind::spell);
  assert(state.world.actors[1000].magic_id == 1);
  assert(state.world.actors[1000].action_magic_effect == 3);
  assert(state.world.actors[1000].x == 330 && state.world.actors[1000].y == 270);
  assert(state.world.actors[1000].action_target_x == 335);
  assert(state.world.actors[1000].action_target_y == 272);
  assert(state.world.actors[1000].action_target_actor_id == 2000);
  state.apply(ActorMagicFire{1000, 2000, 336, 273, 7, 32});
  state.process_legacy_actor_queues(mir2::client::detail::monotonic_ms());
  assert(state.world.actors[1000].action_magic_effect_type == -1);
  state.process_legacy_actor_hurry_queues(mir2::client::detail::monotonic_ms());
  assert(state.world.actors[1000].action_magic_effect_type == 7);
  assert(state.world.actors[1000].action_magic_effect == 32);
  assert(state.world.actors[1000].action_target_x == 336);

  state.apply(ActorDeath{2000, 332, 271, 4});
  assert(state.world.actors[2000].hp == 0);
  state.world.actors[2000].action_started_ms = 0;
  state.process_legacy_actor_queues(mir2::client::detail::monotonic_ms());
  assert(state.world.actors[2000].dead);

  MagicList magics;
  magics.magics.push_back(
      MagicEntry{1, 1, 2, 300, 1000, "Fire", 15, 900, 1});
  state.apply(magics);
  assert(state.world.magics.size() == 1);
  assert(state.world.magics.front().magic_id == 1);
  assert(state.world.magics.front().key == 1);
  assert(state.world.magics.front().effect == 15);
  assert(state.world.magics.front().max_train == 900);
  assert(state.world.magics.front().effect_type == 1);

  MerchantGoodsList goods;
  goods.merchant_id = 2000;
  goods.items.push_back(MerchantGoodsItem{0, "Potion", 7, 0, 50});
  state.apply(goods);
  assert(state.world.merchant_shop.visible);
  assert(state.world.merchant_shop.goods.front().price == 50);

  state.apply(MerchantPriceResult{2000, 0, 0, true, true});
  assert(state.world.merchant_shop.sell_selecting);
  state.world.merchant_shop.pending_sell_name = "Potion";
  state.apply(MerchantPriceResult{2000, 1001, 25, true, true});
  assert(!state.world.merchant_shop.sell_selecting);
  assert(state.world.merchant_shop.pending_sell_make_index == 1001);
  assert(state.world.merchant_shop.pending_sell_price == 25);

  state.apply(MerchantRepairPriceResult{2000, 0, 0, true});
  assert(state.world.repair.selecting);
  state.world.repair.pending_name = "Sword";
  state.apply(MerchantRepairPriceResult{2000, 1002, 120, true});
  assert(!state.world.repair.selecting);
  assert(state.world.repair.dialog_visible);
  assert(state.world.repair.pending_make_index == 1002);
  assert(state.world.repair.pending_price == 120);

  state.apply(StorageList{2000, {potion, weapon}});
  assert(state.world.storage.visible);
  assert(state.world.storage.items.size() == 2);
  assert(state.world.storage.items.front().name == "Potion");

  state.apply(GroupState{true, true, {"Hero", "Ally"}});
  assert(state.world.group.visible);
  assert(state.world.group.allow_group);
  assert(state.world.group.members.size() == 2);

  state.apply(TradeState{true, "Ally", {ItemSlotState{0, potion}}, {}, 100, 0, true, false});
  assert(state.world.trade.visible);
  assert(state.world.trade.remote_name == "Ally");
  assert(state.world.trade.local_items.size() == 1);
  assert(state.world.trade.local_gold == 100);

  state.apply(GuildState{true, "Guild", "Rank", "Notice",
                         {GuildMemberState{"Hero", "Rank", true}}, {"Rank"}, true});
  assert(state.world.guild.visible);
  assert(state.world.guild.guild_name == "Guild");
  assert(state.world.guild.members.size() == 1);

  MiniMapData minimap;
  minimap.success = true;
  minimap.map_id = "0";
  minimap.width = 2;
  minimap.height = 2;
  minimap.pixels = {0, 1, 1, 0};
  state.apply(minimap);
  assert(state.world.minimap.visible);
  assert(state.world.minimap.loaded);
  assert(state.world.minimap.pixels.size() == 4);

  state.apply(MapDoorState{12, 13, true});
  assert(state.map_door_open(12, 13));
  state.apply(MapDoorState{12, 13, false});
  assert(!state.map_door_open(12, 13));
  assert(state.world.map_doors.find(GameStateStore::map_door_key(12, 13)) !=
         state.world.map_doors.end());
  state.apply(MapDoorState{12, 13, true});
  assert(state.map_door_open(12, 13));
  state.world.map_doors[GameStateStore::map_door_key(12, 13)].updated_ms = 1;
  state.expire_map_door_states(1 + kLegacyMapDoorOpenExpireMs);
  assert(!state.map_door_open(12, 13));
  assert(state.world.map_doors.find(GameStateStore::map_door_key(12, 13)) ==
         state.world.map_doors.end());
  state.apply(MapDoorState{12, 13, true});
  assert(state.map_door_open(12, 13));

  state.apply(WorldClearObjects{});
  assert(state.world.map_transition_pending);
  assert(state.world.map_clear_waiting_for_change);
  assert(state.should_defer_runtime_for_map_transition(mir2::client::detail::monotonic_ms()));
  assert(state.world.self_actor_id == 1000);
  assert(!state.world.actors.empty());
  assert(state.map_door_open(12, 13));
  assert(state.world.bag_items[0].name == "Potion");
  assert(state.world.equipment[1].name == "Sword");
  assert(state.world.magics.size() == 1);

  state.world.actors[1000].action_started_ms = mir2::client::detail::monotonic_ms();
  state.world.actors[1000].action_duration_ms = 1000;
  state.apply(MapChange{"1"});
  assert(state.world.map_transition_pending);
  assert(!state.world.map_clear_waiting_for_change);
  assert(state.world.map_change_waiting);
  assert(state.world.pending_map_id == "1");
  assert(state.world.map_id != "1");
  assert(!state.world.actors.empty());
  state.world.actors[1000].action_started_ms = 0;
  assert(state.finish_pending_map_transition_if_ready(mir2::client::detail::monotonic_ms()));
  assert(state.world.map_id == "1");
  assert(state.world.map_transition_pending);
  assert(state.world.actors.empty());
  WorldSnapshot changed_snapshot;
  changed_snapshot.map_id = "1";
  changed_snapshot.width = 500;
  changed_snapshot.height = 400;
  changed_snapshot.self_actor_id = 1000;
  changed_snapshot.actors.push_back(
      WorldActor{1000, "Hero", 5, 5, 2, 0, 0, ActorType::player});
  state.apply(changed_snapshot);
  assert(!state.world.map_transition_pending);
  assert(state.world.map_id == "1");
  assert(state.world.width == 500);
  assert(state.world.actors.size() == 1);
  assert(state.world.bag_items[0].name == "Potion");
  assert(state.world.equipment[1].name == "Sword");
  assert(state.world.magics.size() == 1);
  assert(!state.world.minimap.visible);
  assert(state.world.group.visible);
  assert(state.world.guild.visible);

  state.world.npc_dialog.visible = true;
  state.world.merchant_shop.visible = true;
  state.world.repair.dialog_visible = true;
  state.world.storage.visible = true;
  state.world.group.visible = true;
  state.world.trade.visible = true;
  state.world.guild.visible = true;
  state.world.moving_item = MovingItemState{true, MovingItemSource::bag, 6, potion};
  state.world.waiting_item = MovingItemState{true, MovingItemSource::equipment, 1, weapon};
  state.world.waiting_item_started_ms = 123;
  state.world.hovered_bag_slot = 6;
  state.world.hovered_equipment_slot = 1;
  state.world.selected_bag_slot = 6;
  state.world.focus_actor_id = 2000;
  state.world.focus_ground_item_id = 3000;
  state.world.target_actor_id = 2000;
  state.world.pending_pickup_item_id = 3000;
  state.world.action_locked = true;
  state.world.action_lock_started_ms = 456;
  state.world.last_action_ack_ms = 789;
  state.world.last_action_ack_ok = false;
  state.world.action_fail_lock = true;
  state.world.fail_action_ident = mir2::legacy::kSmWalk;
  state.world.fail_dir = 3;
  state.world.fail_action_time_ms = 999;
  state.world.last_sent_action_ident = mir2::legacy::kSmRun;
  state.world.last_sent_action_dir = 4;
  state.world.dizzy_delay_start_ms = 1000;
  state.world.dizzy_delay_time_ms = 2000;
  state.world.skip_tick = 1;
  state.world.move_slow_level = 2;
  state.world.move_slow = true;
  state.world.attack_slow = true;
  state.world.legacy_target_x = 10;
  state.world.legacy_target_y = 11;
  state.world.legacy_chr_action = LegacyChrAction::run;
  state.world.action_key = 3;
  state.world.run_ready_count = 1;
  state.world.mouse_down_ms = 321;
  state.world.last_pickup_ms = 654;
  state.world.eating_item_make_index = 777;
  state.world.eating_item_slot = 6;
  state.world.eat_time_ms = 888;
  state.world.last_use_item_ok = false;
  state.clear_world_ui_state();
  assert(!state.world.npc_dialog.visible);
  assert(!state.world.merchant_shop.visible);
  assert(!state.world.repair.dialog_visible);
  assert(!state.world.storage.visible);
  assert(!state.world.group.visible);
  assert(!state.world.trade.visible);
  assert(!state.world.guild.visible);
  assert(!state.world.minimap.visible);
  assert(!state.world.moving_item.active);
  assert(!state.world.waiting_item.active);
  assert(state.world.waiting_item_started_ms == 0);
  assert(state.world.hovered_bag_slot == -1);
  assert(state.world.hovered_equipment_slot == -1);
  assert(state.world.selected_bag_slot == -1);
  assert(state.world.focus_actor_id == 0);
  assert(state.world.focus_ground_item_id == 0);
  assert(state.world.target_actor_id == 0);
  assert(state.world.pending_pickup_item_id == 0);
  assert(!state.world.action_locked);
  assert(state.world.action_lock_started_ms == 0);
  assert(state.world.last_action_ack_ms == 0);
  assert(state.world.last_action_ack_ok);
  assert(!state.world.action_fail_lock);
  assert(state.world.fail_action_ident == 0);
  assert(state.world.fail_dir == 0);
  assert(state.world.fail_action_time_ms == 0);
  assert(state.world.last_sent_action_ident == 0);
  assert(state.world.last_sent_action_dir == 0);
  assert(state.world.dizzy_delay_start_ms == 0);
  assert(state.world.dizzy_delay_time_ms == 0);
  assert(state.world.skip_tick == 0);
  assert(state.world.move_slow_level == 0);
  assert(!state.world.move_slow);
  assert(!state.world.attack_slow);
  assert(state.world.legacy_target_x == -1);
  assert(state.world.legacy_target_y == -1);
  assert(state.world.legacy_chr_action == LegacyChrAction::none);
  assert(state.world.action_key == -1);
  assert(state.world.run_ready_count == 0);
  assert(state.world.mouse_down_ms == 0);
  assert(state.world.last_pickup_ms == 0);
  assert(state.world.eating_item_make_index == 0);
  assert(state.world.eating_item_slot == -1);
  assert(state.world.eat_time_ms == 0);
  assert(state.world.last_use_item_ok);

  state.world.map_id = "3";
  state.world.width = 400;
  state.world.height = 300;
  state.world.self_actor_id = 1000;
  state.world.actors.emplace(1000, ActorState{});
  state.world.ground_items.emplace(1, GroundItemState{});
  state.world.actor_draw_order.push_back(1000);
  state.world.ground_item_draw_order.push_back(1);
  state.clear_play_scene_state();
  assert(state.world.map_id == "0");
  assert(state.world.width == 800);
  assert(state.world.height == 600);
  assert(state.world.self_actor_id == 0);
  assert(state.world.actors.empty());
  assert(state.world.ground_items.empty());
  assert(state.world.actor_draw_order.empty());
  assert(state.world.ground_item_draw_order.empty());

  state.world.action_locked = true;
  state.apply(ActionAck{true, 1234});
  assert(!state.world.action_locked);

  state.auth_phase = AuthFlowPhase::Disconnected;
  assert(state.auth_phase == AuthFlowPhase::Disconnected);
  state.auth_phase = AuthFlowPhase::EditingLogin;
  assert(state.auth_phase == AuthFlowPhase::EditingLogin);
  return 0;
}
