#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

#include "shared/legacy/action_ids.hpp"
#include "shared/protocol/client_v1/protocol.hpp"

namespace {

template <typename T>
T round_trip(const T& message, std::uint32_t sequence) {
  using namespace mir2::client_v1;
  auto bytes = encode_frame(make_frame(message, sequence));
  std::vector<std::uint8_t> buffer = bytes;
  auto frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().sequence == sequence);
  auto decoded = decode_message<T>(frames.front());
  assert(decoded.has_value());
  return *decoded;
}

void assert_bytes(const std::vector<std::uint8_t>& actual,
                  const std::vector<std::uint8_t>& expected) {
  assert(actual.size() == expected.size());
  assert(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
}

}  // namespace

int main() {
  using namespace mir2::client_v1;

  const ClientHello hello{kProtocolVersion, 0x01020304U, 0xA0B0C0D0U, 0x0F0E0D0CU};
  const auto hello_bytes = encode_frame(make_frame(hello, 0x11223344U, 0x5566U));
  assert_bytes(hello_bytes, {0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0x66, 0x55,
                             0x44, 0x33, 0x22, 0x11, 0x03, 0x00, 0x00, 0x00,
                             0x04, 0x03, 0x02, 0x01, 0xD0, 0xC0, 0xB0, 0xA0,
                             0x0C, 0x0D, 0x0E, 0x0F});

  const auto login_bytes = encode_frame(make_frame(LoginRequest{"id", "pw"}, 7));
  assert_bytes(login_bytes, {0x10, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00,
                             0x07, 0x00, 0x00, 0x00, 0x02, 0x00, 'i',  'd',
                             0x02, 0x00, 'p',  'w'});

  std::vector<std::uint8_t> partial(login_bytes.begin(), login_bytes.begin() + 5);
  auto frames = drain_frames(partial);
  assert(frames.empty());
  assert(partial.size() == 5);
  partial.insert(partial.end(), login_bytes.begin() + 5, login_bytes.end());
  frames = drain_frames(partial);
  assert(frames.size() == 1);
  assert(partial.empty());
  assert(decode_message<LoginRequest>(frames.front())->account_id == "id");

  std::vector<std::uint8_t> sticky = hello_bytes;
  sticky.insert(sticky.end(), login_bytes.begin(), login_bytes.end());
  frames = drain_frames(sticky);
  assert(frames.size() == 2);
  assert(sticky.empty());
  assert(frames[0].message_id == MessageId::client_hello);
  assert(frames[1].message_id == MessageId::login_request);

  std::vector<std::uint8_t> invalid_length{0x00, 0x00, 0x00, 0x00};
  frames = drain_frames(invalid_length);
  assert(frames.empty());
  assert(invalid_length.empty());

  std::vector<std::uint8_t> overlong_incomplete{0x08, 0x00, 0x10, 0x00};
  frames = drain_frames(overlong_incomplete);
  assert(frames.empty());
  assert(overlong_incomplete.size() == 4);

  const auto unknown_bytes =
      encode_frame(Frame{static_cast<MessageId>(0xFFFFU), 0, 99, {}});
  std::vector<std::uint8_t> unknown_buffer = unknown_bytes;
  frames = drain_frames(unknown_buffer);
  assert(frames.size() == 1);
  assert(!decode_any(frames.front()).has_value());

  const LegacyBundleMeta bundle_meta{0x0102030405060708ULL,
                                     1,
                                     3,
                                     mir2::legacy::kSmWalk,
                                     LegacyBundleMode::actor_queue};
  const ActorAction bundled_action{42,
                                   ActorActionKind::walk,
                                   12,
                                   13,
                                   2,
                                   0,
                                   0,
                                   mir2::legacy::kSmWalk,
                                   0,
                                   false,
                                   0};
  const auto bundled_bytes = encode_frame(make_frame(bundled_action, 0x0A0B0C0DU,
                                                     0x0020U, bundle_meta));
  std::vector<std::uint8_t> bundle_buffer = bundled_bytes;
  frames = drain_frames(bundle_buffer);
  assert(bundle_buffer.empty());
  assert(frames.size() == 1);
  assert((frames.front().flags & kFrameFlagLegacyBundle) != 0U);
  assert(frames.front().legacy_bundle.has_value());
  assert(frames.front().legacy_bundle->bundle_id == bundle_meta.bundle_id);
  assert(frames.front().legacy_bundle->bundle_index == 1);
  assert(frames.front().legacy_bundle->bundle_count == 3);
  assert(frames.front().legacy_bundle->legacy_ident == mir2::legacy::kSmWalk);
  assert(frames.front().legacy_bundle->bundle_mode == LegacyBundleMode::actor_queue);
  assert(decode_message<ActorAction>(frames.front())->kind == ActorActionKind::walk);

  const auto decoded_hello = round_trip(hello, 1);
  assert(decoded_hello.protocol_version == kProtocolVersion);
  assert(decoded_hello.client_build == 0x01020304U);

  LoginRequest request;
  request.account_id = "guest";
  request.password = "pass";

  const auto encoded_frame = encode_frame(make_frame(request, 7));
  std::vector<std::uint8_t> buffer = encoded_frame;
  frames = drain_frames(buffer);
  assert(buffer.empty());
  assert(frames.size() == 1);
  assert(frames.front().sequence == 7);
  assert(frames.front().message_id == MessageId::login_request);

  const auto decoded = decode_message<LoginRequest>(frames.front());
  assert(decoded.has_value());
  assert(decoded->account_id == "guest");
  assert(decoded->password == "pass");

  WorldSnapshot snapshot;
  snapshot.map_id = "0";
  snapshot.width = 960;
  snapshot.height = 720;
  snapshot.self_actor_id = 42;
  snapshot.actors.push_back(WorldActor{42, "Hero", 330, 270, 0, 1, 0, ActorType::player});

  const auto snapshot_frame = encode_frame(make_frame(snapshot, 8));
  buffer = snapshot_frame;
  frames = drain_frames(buffer);
  const auto decoded_snapshot = decode_message<WorldSnapshot>(frames.front());
  assert(decoded_snapshot.has_value());
  assert(decoded_snapshot->map_id == "0");
  assert(decoded_snapshot->actors.size() == 1);
  assert(decoded_snapshot->actors.front().name == "Hero");
  assert(decoded_snapshot->actors.front().feature == 1);

  SelectServerRequest select_server;
  select_server.name = "ModernServer";
  const auto select_frame = encode_frame(make_frame(select_server, 9));
  buffer = select_frame;
  frames = drain_frames(buffer);
  const auto decoded_select = decode_message<SelectServerRequest>(frames.front());
  assert(decoded_select.has_value());
  assert(decoded_select->name == "ModernServer");

  SelectServerResult select_result;
  select_result.success = true;
  select_result.name = "ModernServer";
  select_result.address = "127.0.0.1";
  select_result.port = 5600;
  select_result.lobby_token = "lobby-ticket-1";
  const auto select_result_frame = encode_frame(make_frame(select_result, 10));
  buffer = select_result_frame;
  frames = drain_frames(buffer);
  const auto decoded_select_result = decode_message<SelectServerResult>(frames.front());
  assert(decoded_select_result.has_value());
  assert(decoded_select_result->success);
  assert(decoded_select_result->address == "127.0.0.1");
  assert(decoded_select_result->lobby_token == "lobby-ticket-1");

  CharacterListRequest character_list;
  character_list.lobby_token = "lobby-ticket-1";
  const auto character_list_frame = encode_frame(make_frame(character_list, 11));
  buffer = character_list_frame;
  frames = drain_frames(buffer);
  const auto decoded_character_list = decode_message<CharacterListRequest>(frames.front());
  assert(decoded_character_list.has_value());
  assert(decoded_character_list->lobby_token == "lobby-ticket-1");

  CreateAccountRequest create_account;
  create_account.account_id = "guest";
  create_account.password = "pass";
  create_account.profile.display_name = "guest";
  create_account.profile.user_name = "Guest User";
  create_account.profile.ss_no = "650101-1455111";
  create_account.profile.birthday = "1975/08/21";
  create_account.profile.quiz = "q1";
  create_account.profile.answer = "a1";
  create_account.profile.quiz2 = "q2";
  create_account.profile.answer2 = "a2";
  create_account.profile.phone = "021";
  create_account.profile.mobile_phone = "13900000000";
  create_account.profile.email = "guest@example.test";
  const auto create_frame = encode_frame(make_frame(create_account, 12));
  buffer = create_frame;
  frames = drain_frames(buffer);
  const auto decoded_create = decode_message<CreateAccountRequest>(frames.front());
  assert(decoded_create.has_value());
  assert(decoded_create->profile.user_name == "Guest User");
  assert(decoded_create->profile.answer2 == "a2");

  UpdateAccountResult update_result;
  update_result.success = true;
  update_result.code = 1;
  const auto update_result_frame = encode_frame(make_frame(update_result, 13));
  buffer = update_result_frame;
  frames = drain_frames(buffer);
  const auto decoded_update_result = decode_message<UpdateAccountResult>(frames.front());
  assert(decoded_update_result.has_value());
  assert(decoded_update_result->success);
  assert(decoded_update_result->code == 1);

  LoginNotice notice;
  notice.title = "Welcome";
  notice.text = "Choose OK to enter.";
  const auto notice_frame = encode_frame(make_frame(notice, 14));
  buffer = notice_frame;
  frames = drain_frames(buffer);
  assert(frames.front().message_id == MessageId::login_notice);
  const auto decoded_notice = decode_message<LoginNotice>(frames.front());
  assert(decoded_notice.has_value());
  assert(decoded_notice->title == "Welcome");
  assert(decoded_notice->text == "Choose OK to enter.");

  LoginNoticeOk notice_ok;
  const auto notice_ok_frame = encode_frame(make_frame(notice_ok, 15));
  buffer = notice_ok_frame;
  frames = drain_frames(buffer);
  assert(frames.front().message_id == MessageId::login_notice_ok);
  const auto decoded_notice_ok = decode_message<LoginNoticeOk>(frames.front());
  assert(decoded_notice_ok.has_value());

  ActionIntent action;
  action.kind = WorldActionKind::attack;
  action.x = 334;
  action.y = 271;
  action.dir = 2;
  action.target_actor_id = 99;
  action.legacy_ident = 3014;
  const auto action_frame = encode_frame(make_frame(action, 16));
  buffer = action_frame;
  frames = drain_frames(buffer);
  const auto decoded_action = decode_message<ActionIntent>(frames.front());
  assert(decoded_action.has_value());
  assert(decoded_action->kind == WorldActionKind::attack);
  assert(decoded_action->target_actor_id == 99);
  assert(mir2::legacy::cm_attack_ident_to_sm(mir2::legacy::kCmPowerHit) ==
         mir2::legacy::kSmPowerHit);
  assert(mir2::legacy::cm_attack_ident_to_sm(mir2::legacy::kCmLongHit) ==
         mir2::legacy::kSmLongHit);
  assert(mir2::legacy::cm_attack_ident_to_sm(mir2::legacy::kCmWideHit) ==
         mir2::legacy::kSmWideHit);
  assert(mir2::legacy::cm_attack_ident_to_sm(mir2::legacy::kCmFireHit) ==
         mir2::legacy::kSmFireHit);
  assert(mir2::legacy::cm_attack_ident_to_sm(mir2::legacy::kCmCrossHit) ==
         mir2::legacy::kSmCrossHit);
  assert(mir2::legacy::sm_attack_ident_to_cm(mir2::legacy::kSmFireHit) ==
         mir2::legacy::kCmFireHit);
  assert(mir2::legacy::normalize_attack_ident_to_sm(25) == mir2::legacy::kSmHit);
  assert(!mir2::legacy::is_attack_sm_ident(25));

  SpellIntent spell;
  spell.x = 335;
  spell.y = 272;
  spell.dir = 3;
  spell.target_actor_id = 99;
  spell.magic_id = 1;
  const auto spell_frame = encode_frame(make_frame(spell, 17));
  buffer = spell_frame;
  frames = drain_frames(buffer);
  const auto decoded_spell = decode_message<SpellIntent>(frames.front());
  assert(decoded_spell.has_value());
  assert(decoded_spell->magic_id == 1);

  const auto ack_frame = encode_frame(make_frame(ActionAck{true, 1234}, 18));
  buffer = ack_frame;
  frames = drain_frames(buffer);
  const auto decoded_ack = decode_message<ActionAck>(frames.front());
  assert(decoded_ack.has_value());
  assert(decoded_ack->ok);

  PickupIntent pickup;
  pickup.x = 330;
  pickup.y = 270;
  const auto pickup_frame = encode_frame(make_frame(pickup, 181));
  buffer = pickup_frame;
  frames = drain_frames(buffer);
  assert(frames.front().message_id == MessageId::pickup_intent);
  const auto decoded_pickup = decode_message<PickupIntent>(frames.front());
  assert(decoded_pickup.has_value());
  assert(decoded_pickup->x == 330);

  UseItemIntent use_item;
  use_item.item_make_index = 1001;
  use_item.item_slot = 2;
  use_item.name = "Potion";
  const auto use_item_frame = encode_frame(make_frame(use_item, 182));
  buffer = use_item_frame;
  frames = drain_frames(buffer);
  assert(frames.front().message_id == MessageId::use_item_intent);
  const auto decoded_use_item = decode_message<UseItemIntent>(frames.front());
  assert(decoded_use_item.has_value());
  assert(decoded_use_item->item_slot == 2);
  assert(decoded_use_item->name == "Potion");

  GroundItemAdd ground_add;
  ground_add.item.object_id = 77;
  ground_add.item.x = 10;
  ground_add.item.y = 11;
  ground_add.item.looks = 123;
  ground_add.item.name = "Gold";
  const auto ground_add_frame = encode_frame(make_frame(ground_add, 183));
  buffer = ground_add_frame;
  frames = drain_frames(buffer);
  assert(frames.front().message_id == MessageId::ground_item_add);
  const auto decoded_ground_add = decode_message<GroundItemAdd>(frames.front());
  assert(decoded_ground_add.has_value());
  assert(decoded_ground_add->item.object_id == 77);
  assert(decoded_ground_add->item.name == "Gold");

  const auto ground_remove_frame =
      encode_frame(make_frame(GroundItemRemove{77, 10, 11}, 184));
  buffer = ground_remove_frame;
  frames = drain_frames(buffer);
  assert(frames.front().message_id == MessageId::ground_item_remove);
  const auto decoded_ground_remove = decode_message<GroundItemRemove>(frames.front());
  assert(decoded_ground_remove.has_value());
  assert(decoded_ground_remove->object_id == 77);

  const auto actor_remove_frame = encode_frame(make_frame(ActorRemove{42}, 1841));
  buffer = actor_remove_frame;
  frames = drain_frames(buffer);
  assert(frames.front().message_id == MessageId::actor_remove);
  const auto decoded_actor_remove = decode_message<ActorRemove>(frames.front());
  assert(decoded_actor_remove.has_value());
  assert(decoded_actor_remove->actor_id == 42);

  const auto use_result_frame = encode_frame(make_frame(UseItemResult{true}, 185));
  buffer = use_result_frame;
  frames = drain_frames(buffer);
  assert(frames.front().message_id == MessageId::use_item_result);
  const auto decoded_use_result = decode_message<UseItemResult>(frames.front());
  assert(decoded_use_result.has_value());
  assert(decoded_use_result->ok);

  ActorAction actor_action;
  actor_action.actor_id = 42;
  actor_action.kind = ActorActionKind::hit;
  actor_action.x = 330;
  actor_action.y = 270;
  actor_action.dir = 2;
  actor_action.target_actor_id = 99;
  actor_action.value = 7;
  actor_action.legacy_ident = 14;
  actor_action.magic_effect = 31;
  const auto actor_action_frame = encode_frame(make_frame(actor_action, 19));
  buffer = actor_action_frame;
  frames = drain_frames(buffer);
  const auto decoded_actor_action = decode_message<ActorAction>(frames.front());
  assert(decoded_actor_action.has_value());
  assert(decoded_actor_action->kind == ActorActionKind::hit);
  assert(decoded_actor_action->target_actor_id == 99);
  assert(decoded_actor_action->magic_effect == 31);

  const auto magic_fire_frame =
      encode_frame(make_frame(ActorMagicFire{42, 99, 330, 270, 7, 32}, 191));
  buffer = magic_fire_frame;
  frames = drain_frames(buffer);
  assert(frames.front().message_id == MessageId::actor_magic_fire);
  const auto decoded_magic_fire = decode_message<ActorMagicFire>(frames.front());
  assert(decoded_magic_fire.has_value());
  assert(decoded_magic_fire->actor_id == 42);
  assert(decoded_magic_fire->target_actor_id == 99);
  assert(decoded_magic_fire->effect_type == 7);
  assert(decoded_magic_fire->effect == 32);

  const auto vitals_frame =
      encode_frame(make_frame(ActorVitals{42, 18, 30, 9, 12, 5, 99, true}, 20));
  buffer = vitals_frame;
  frames = drain_frames(buffer);
  const auto decoded_vitals = decode_message<ActorVitals>(frames.front());
  assert(decoded_vitals.has_value());
  assert(decoded_vitals->damage == 5);
  assert(decoded_vitals->magic);

  const auto death_frame = encode_frame(make_frame(ActorDeath{42, 330, 270, 4}, 21));
  buffer = death_frame;
  frames = drain_frames(buffer);
  const auto decoded_death = decode_message<ActorDeath>(frames.front());
  assert(decoded_death.has_value());
  assert(decoded_death->dir == 4);

  MagicList magic_list;
  magic_list.magics.push_back(MagicEntry{1, 1, 0, 0, 1000, "Fire", 15, 900, 1, 20, 3, 3});
  const auto magic_frame = encode_frame(make_frame(magic_list, 22));
  buffer = magic_frame;
  frames = drain_frames(buffer);
  const auto decoded_magic = decode_message<MagicList>(frames.front());
  assert(decoded_magic.has_value());
  assert(decoded_magic->magics.size() == 1);
  assert(decoded_magic->magics.front().name == "Fire");
  assert(decoded_magic->magics.front().effect == 15);
  assert(decoded_magic->magics.front().max_train == 900);
  assert(decoded_magic->magics.front().effect_type == 1);
  assert(decoded_magic->magics.front().spell == 20);
  assert(decoded_magic->magics.front().def_spell == 3);
  assert(decoded_magic->magics.front().max_train_level == 3);

  const auto self_ability =
      round_trip(SelfAbility{35, 1, 123456, 200000, 37, 60, 8888, 3}, 221);
  assert(self_ability.level == 35);
  assert(self_ability.job == 1);
  assert(self_ability.exp == 123456);
  assert(self_ability.max_exp == 200000);
  assert(self_ability.weight == 37);
  assert(self_ability.max_weight == 60);
  assert(self_ability.gold == 8888);
  assert(self_ability.hunger_state == 3);

  SelfAbilityDetail detail;
  detail.level = 35;
  detail.job = 1;
  detail.sex = 0;
  detail.hair = 2;
  detail.hp = 70;
  detail.max_hp = 90;
  detail.mp = 44;
  detail.max_mp = 55;
  detail.ac = 3;
  detail.mac = 4;
  detail.dc = 5;
  detail.mc = 6;
  detail.sc = 7;
  detail.exp = 123456;
  detail.max_exp = 200000;
  detail.weight = 37;
  detail.max_weight = 60;
  detail.wear_weight = 8;
  detail.max_wear_weight = 20;
  detail.hand_weight = 4;
  detail.max_hand_weight = 10;
  detail.hit = 9;
  detail.speed = 1;
  detail.anti_magic = 2;
  detail.anti_poison = 3;
  detail.poison_recover = 4;
  detail.health_recover = 5;
  detail.spell_recover = 6;
  detail.guild_name = "Guild";
  detail.guild_rank_name = "Rank";
  detail.name_color = 0xFFEEDDCCU;
  const auto decoded_detail = round_trip(detail, 222);
  assert(decoded_detail.level == 35);
  assert(decoded_detail.guild_rank_name == "Rank");
  assert(decoded_detail.name_color == 0xFFEEDDCCU);

  BagSnapshot bag;
  bag.items.push_back(ItemSlotState{0, ItemState{"Potion", 1002, 2, 0, 1, 1}});
  const auto bag_frame = encode_frame(make_frame(bag, 23));
  buffer = bag_frame;
  frames = drain_frames(buffer);
  const auto decoded_bag = decode_message<BagSnapshot>(frames.front());
  assert(decoded_bag.has_value());
  assert(decoded_bag->items.size() == 1);
  assert(decoded_bag->items.front().item.name == "Potion");

  EquipmentSnapshot equipment;
  equipment.items.push_back(ItemSlotState{1, ItemState{"Wooden Sword", 1001, 1, 5, 600, 1000}});
  const auto equipment_frame = encode_frame(make_frame(equipment, 24));
  buffer = equipment_frame;
  frames = drain_frames(buffer);
  const auto decoded_equipment = decode_message<EquipmentSnapshot>(frames.front());
  assert(decoded_equipment.has_value());
  assert(decoded_equipment->items.front().slot == 1);

  const auto equip_request = round_trip(EquipItemRequest{1, 1001, "Wooden Sword"}, 25);
  assert(equip_request.equipment_slot == 1);
  assert(equip_request.item_make_index == 1001);

  const auto unequip_request = round_trip(UnequipItemRequest{1, 1001, "Wooden Sword"}, 26);
  assert(unequip_request.equipment_slot == 1);

  const auto drop_request = round_trip(DropItemRequest{1001, "Wooden Sword"}, 27);
  assert(drop_request.item_make_index == 1001);

  const auto drop_gold = round_trip(DropGoldRequest{250}, 271);
  assert(drop_gold.amount == 250);

  const auto durability = round_trip(DurabilityChange{1001, 550, 1000}, 272);
  assert(durability.item_make_index == 1001);
  assert(durability.dura == 550);
  assert(durability.dura_max == 1000);

  const auto npc_click = round_trip(NpcClickRequest{77}, 28);
  assert(npc_click.actor_id == 77);

  const auto npc_dialog = round_trip(NpcDialog{77, 10, "<Buy/@buy>"}, 29);
  assert(npc_dialog.merchant_id == 77);
  assert(npc_dialog.text == "<Buy/@buy>");

  const auto npc_select = round_trip(NpcDialogSelectRequest{77, "@exit"}, 30);
  assert(npc_select.selection == "@exit");

  const auto npc_close = round_trip(NpcDialogClose{77}, 31);
  assert(npc_close.merchant_id == 77);

  const auto magic_key = round_trip(MagicKeyChangeRequest{1, 8}, 32);
  assert(magic_key.magic_id == 1);
  assert(magic_key.key == 8);

  const auto minimap_request = round_trip(MiniMapRequest{"0"}, 33);
  assert(minimap_request.map_id == "0");

  MiniMapData minimap;
  minimap.success = true;
  minimap.map_id = "0";
  minimap.width = 2;
  minimap.height = 2;
  minimap.pixels = {0, 1, 1, 0};
  const auto decoded_minimap = round_trip(minimap, 34);
  assert(decoded_minimap.success);
  assert(decoded_minimap.pixels.size() == 4);

  MerchantGoodsList goods;
  goods.merchant_id = 77;
  goods.items.push_back(MerchantGoodsItem{0, "Potion", 7, 0, 50});
  const auto decoded_goods = round_trip(goods, 35);
  assert(decoded_goods.items.size() == 1);
  assert(decoded_goods.items.front().price == 50);

  const auto buy = round_trip(MerchantBuyRequest{77, 0, "Potion"}, 36);
  assert(buy.merchant_id == 77);
  assert(buy.name == "Potion");

  const auto sell_price = round_trip(MerchantSellPriceRequest{77, 1001, "Potion"}, 37);
  assert(sell_price.item_make_index == 1001);

  const auto sell = round_trip(MerchantSellRequest{77, 1001, "Potion"}, 38);
  assert(sell.item_make_index == 1001);

  const auto price_result = round_trip(MerchantPriceResult{77, 1001, 25, true, true}, 39);
  assert(price_result.sell);
  assert(price_result.price == 25);

  const auto repair_price =
      round_trip(MerchantRepairPriceRequest{77, 1001, "Sword"}, 41);
  assert(repair_price.item_make_index == 1001);
  const auto repair = round_trip(MerchantRepairRequest{77, 1001, "Sword"}, 42);
  assert(repair.name == "Sword");
  const auto repair_result = round_trip(MerchantRepairPriceResult{77, 1001, 120, true}, 43);
  assert(repair_result.price == 120);

  const auto storage = round_trip(StorageList{77, {ItemState{"Potion", 1002, 2, 0, 1, 1}}}, 44);
  assert(storage.items.size() == 1);
  assert(storage.items.front().name == "Potion");
  const auto deposit = round_trip(StorageDepositRequest{77, 1002, "Potion"}, 45);
  assert(deposit.name == "Potion");
  const auto withdraw = round_trip(StorageWithdrawRequest{77, 1002, "Potion"}, 46);
  assert(withdraw.item_make_index == 1002);

  const auto group_mode = round_trip(GroupModeRequest{true}, 47);
  assert(group_mode.allow);
  const auto group_create = round_trip(GroupCreateRequest{"Ally"}, 48);
  assert(group_create.target_name == "Ally");
  const auto group_add = round_trip(GroupAddMemberRequest{"Ally"}, 49);
  assert(group_add.target_name == "Ally");
  const auto group_remove = round_trip(GroupRemoveMemberRequest{"Ally"}, 50);
  assert(group_remove.target_name == "Ally");
  const auto group_state = round_trip(GroupState{true, true, {"Hero", "Ally"}}, 51);
  assert(group_state.members.size() == 2);

  const auto trade_try = round_trip(TradeTryRequest{"Ally"}, 52);
  assert(trade_try.target_name == "Ally");
  static_cast<void>(round_trip(TradeCancelRequest{}, 53));
  const auto trade_add = round_trip(TradeAddItemRequest{1002, "Potion"}, 54);
  assert(trade_add.item_make_index == 1002);
  const auto trade_remove = round_trip(TradeRemoveItemRequest{1002, "Potion"}, 55);
  assert(trade_remove.name == "Potion");
  const auto trade_gold = round_trip(TradeSetGoldRequest{500}, 56);
  assert(trade_gold.gold == 500);
  static_cast<void>(round_trip(TradeAcceptRequest{}, 57));
  const auto trade_state = round_trip(
      TradeState{true, "Ally", {ItemSlotState{0, ItemState{"Potion", 1002, 2, 0, 1, 1}}},
                 {}, 500, 0, true, false},
      58);
  assert(trade_state.local_items.size() == 1);
  assert(trade_state.local_gold == 500);

  static_cast<void>(round_trip(GuildOpenRequest{}, 59));
  static_cast<void>(round_trip(GuildHomeRequest{}, 60));
  static_cast<void>(round_trip(GuildMemberListRequest{}, 61));
  const auto guild_add = round_trip(GuildAddMemberRequest{"Ally"}, 62);
  assert(guild_add.name == "Ally");
  const auto guild_remove = round_trip(GuildRemoveMemberRequest{"Ally"}, 63);
  assert(guild_remove.name == "Ally");
  const auto guild_notice = round_trip(GuildUpdateNoticeRequest{"Notice"}, 64);
  assert(guild_notice.text == "Notice");
  const auto guild_grade = round_trip(GuildUpdateGradeRequest{"Rank"}, 65);
  assert(guild_grade.text == "Rank");
  const auto guild_state = round_trip(
      GuildState{true, "Guild", "Rank", "Notice", {GuildMemberState{"Hero", "Rank", true}},
                 {"Rank"}, true},
      66);
  assert(guild_state.members.size() == 1);
  assert(guild_state.can_admin);

  const auto disconnect =
      round_trip(DisconnectReason{401, "invalid_enter_world_token"}, 40);
  assert(disconnect.code == 401);
  assert(disconnect.text == "invalid_enter_world_token");
  return 0;
}
