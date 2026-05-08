#include <algorithm>
#include <cassert>
#include <string_view>
#include <vector>

#include "protocol/delphi_protocol_map.hpp"
#include "shared/protocol/client_v1/protocol.hpp"

namespace {

template <std::size_t Size>
bool has_entry(const std::array<mir2::client::protocol_migration::MappingEntry, Size>& entries,
               const std::string_view name) {
  return std::any_of(entries.begin(), entries.end(), [&](const auto& entry) {
    return entry.delphi_entry == name;
  });
}

template <std::size_t Size>
const mir2::client::protocol_migration::MappingEntry* find_entry(
    const std::array<mir2::client::protocol_migration::MappingEntry, Size>& entries,
    const std::string_view name) {
  const auto it = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
    return entry.delphi_entry == name;
  });
  return it == entries.end() ? nullptr : &*it;
}

}  // namespace

int main() {
  using namespace mir2::client::protocol_migration;
  using namespace mir2::client_v1;

  static_assert(kDelphiSendMappings.size() == 57);
  static_assert(kDelphiClientGetMappings.size() == 39);

  assert(has_entry(kDelphiSendMappings, "SendSelectServer"));
  assert(has_entry(kDelphiSendMappings, "SendRunLogin"));
  assert(has_entry(kDelphiSendMappings, "SendUpdateAccount"));
  assert(has_entry(kDelphiSendMappings, "CM_CLICKNPC"));
  assert(has_entry(kDelphiClientGetMappings, "ClientGetNeedUpdateAccount"));
  assert(has_entry(kDelphiClientGetMappings, "ClientGetSelectServer"));
  assert(has_entry(kDelphiClientGetMappings, "ClientGetStartPlay"));

  const auto assert_not_planned = [](const auto* entry) {
    assert(entry != nullptr);
    assert(entry->status != MigrationStatus::planned);
  };
  assert_not_planned(find_entry(kDelphiSendMappings, "SendTakeOnItem"));
  assert_not_planned(find_entry(kDelphiSendMappings, "SendTakeOffItem"));
  assert_not_planned(find_entry(kDelphiSendMappings, "SendDropItem"));
  assert_not_planned(find_entry(kDelphiSendMappings, "SendBuyItem"));
  assert_not_planned(find_entry(kDelphiSendMappings, "SendStorageItem"));
  assert_not_planned(find_entry(kDelphiSendMappings, "SendDealTry"));
  assert_not_planned(find_entry(kDelphiClientGetMappings, "ClientGetBagItmes"));
  assert_not_planned(find_entry(kDelphiClientGetMappings, "ClientGetSenduseItems"));
  assert_not_planned(find_entry(kDelphiClientGetMappings, "ClientGetReadMiniMap"));

  SelectServerRequest request;
  request.name = "ModernServer";
  auto frame_bytes = encode_frame(make_frame(request, 11));
  std::vector<std::uint8_t> buffer = frame_bytes;
  auto frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::select_server_request);
  const auto decoded_request = decode_message<SelectServerRequest>(frames.front());
  assert(decoded_request.has_value());
  assert(decoded_request->name == "ModernServer");

  SelectServerResult result;
  result.success = true;
  result.name = "ModernServer";
  result.address = "127.0.0.1";
  result.port = 5601;
  result.lobby_token = "lobby-ticket-1";
  frame_bytes = encode_frame(make_frame(result, 12));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::select_server_result);
  const auto decoded_result = decode_message<SelectServerResult>(frames.front());
  assert(decoded_result.has_value());
  assert(decoded_result->success);
  assert(decoded_result->name == "ModernServer");
  assert(decoded_result->port == 5601);
  assert(decoded_result->lobby_token == "lobby-ticket-1");

  CharacterListRequest character_list_request;
  character_list_request.lobby_token = "lobby-ticket-1";
  frame_bytes = encode_frame(make_frame(character_list_request, 13));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::character_list_request);
  const auto decoded_character_list_request =
      decode_message<CharacterListRequest>(frames.front());
  assert(decoded_character_list_request.has_value());
  assert(decoded_character_list_request->lobby_token == "lobby-ticket-1");

  AccountProfile profile;
  profile.display_name = "guest";
  profile.user_name = "Guest User";
  profile.ss_no = "650101-1455111";
  profile.birthday = "1975/08/21";
  profile.quiz = "q1";
  profile.answer = "a1";
  profile.quiz2 = "q2";
  profile.answer2 = "a2";
  profile.phone = "021";
  profile.mobile_phone = "13900000000";
  profile.email = "guest@example.test";

  UpdateAccountRequest update;
  update.account_id = "guest";
  update.password = "pass";
  update.profile = profile;
  frame_bytes = encode_frame(make_frame(update, 14));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::update_account_request);
  const auto decoded_update = decode_message<UpdateAccountRequest>(frames.front());
  assert(decoded_update.has_value());
  assert(decoded_update->account_id == "guest");
  assert(decoded_update->profile.quiz2 == "q2");
  assert(decoded_update->profile.mobile_phone == "13900000000");

  NeedUpdateAccount need_update;
  need_update.account_id = "guest";
  need_update.profile = profile;
  need_update.message = "account_profile_required";
  frame_bytes = encode_frame(make_frame(need_update, 15));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::need_update_account);
  const auto decoded_need_update = decode_message<NeedUpdateAccount>(frames.front());
  assert(decoded_need_update.has_value());
  assert(decoded_need_update->message == "account_profile_required");
  assert(decoded_need_update->profile.email == "guest@example.test");

  LoginNotice notice;
  notice.title = "Welcome";
  notice.text = "Read this before entering.";
  frame_bytes = encode_frame(make_frame(notice, 16));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::login_notice);
  const auto decoded_notice = decode_message<LoginNotice>(frames.front());
  assert(decoded_notice.has_value());
  assert(decoded_notice->title == "Welcome");
  assert(decoded_notice->text == "Read this before entering.");

  LoginNoticeOk notice_ok;
  frame_bytes = encode_frame(make_frame(notice_ok, 17));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::login_notice_ok);
  const auto decoded_notice_ok = decode_message<LoginNoticeOk>(frames.front());
  assert(decoded_notice_ok.has_value());

  PickupIntent pickup;
  pickup.x = 330;
  pickup.y = 270;
  frame_bytes = encode_frame(make_frame(pickup, 18));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::pickup_intent);
  const auto decoded_pickup = decode_message<PickupIntent>(frames.front());
  assert(decoded_pickup.has_value());
  assert(decoded_pickup->y == 270);

  UseItemIntent use_item;
  use_item.item_make_index = 1001;
  use_item.item_slot = 1;
  use_item.name = "Potion";
  frame_bytes = encode_frame(make_frame(use_item, 19));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::use_item_intent);
  const auto decoded_use_item = decode_message<UseItemIntent>(frames.front());
  assert(decoded_use_item.has_value());
  assert(decoded_use_item->item_make_index == 1001);
  assert(decoded_use_item->name == "Potion");

  GroundItemAdd ground_add;
  ground_add.item.object_id = 77;
  ground_add.item.x = 330;
  ground_add.item.y = 271;
  ground_add.item.looks = 5;
  ground_add.item.name = "Gold";
  frame_bytes = encode_frame(make_frame(ground_add, 20));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::ground_item_add);
  const auto decoded_ground_add = decode_message<GroundItemAdd>(frames.front());
  assert(decoded_ground_add.has_value());
  assert(decoded_ground_add->item.object_id == 77);
  assert(decoded_ground_add->item.name == "Gold");

  frame_bytes = encode_frame(make_frame(GroundItemRemove{77, 330, 271}, 21));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::ground_item_remove);
  const auto decoded_ground_remove = decode_message<GroundItemRemove>(frames.front());
  assert(decoded_ground_remove.has_value());
  assert(decoded_ground_remove->object_id == 77);

  frame_bytes = encode_frame(make_frame(UseItemResult{true}, 22));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::use_item_result);
  const auto decoded_use_result = decode_message<UseItemResult>(frames.front());
  assert(decoded_use_result.has_value());
  assert(decoded_use_result->ok);

  ItemState potion;
  potion.name = "Potion";
  potion.make_index = 1001;
  potion.looks = 7;
  potion.std_mode = 0;
  potion.dura = 10;
  potion.dura_max = 20;

  BagSnapshot bag_snapshot;
  bag_snapshot.items.push_back(ItemSlotState{6, potion});
  frame_bytes = encode_frame(make_frame(bag_snapshot, 23));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::bag_snapshot);
  const auto decoded_bag_snapshot = decode_message<BagSnapshot>(frames.front());
  assert(decoded_bag_snapshot.has_value());
  assert(decoded_bag_snapshot->items.size() == 1);
  assert(decoded_bag_snapshot->items.front().slot == 6);
  assert(decoded_bag_snapshot->items.front().item.looks == 7);

  InventoryAdd inventory_add;
  inventory_add.entry = ItemSlotState{6, potion};
  frame_bytes = encode_frame(make_frame(inventory_add, 24));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::inventory_add);
  const auto decoded_inventory_add = decode_message<InventoryAdd>(frames.front());
  assert(decoded_inventory_add.has_value());
  assert(decoded_inventory_add->entry.slot == 6);

  InventoryUpdate inventory_update;
  inventory_update.entry = ItemSlotState{7, potion};
  frame_bytes = encode_frame(make_frame(inventory_update, 25));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::inventory_update);
  const auto decoded_inventory_update = decode_message<InventoryUpdate>(frames.front());
  assert(decoded_inventory_update.has_value());
  assert(decoded_inventory_update->entry.slot == 7);
  assert(decoded_inventory_update->entry.item.name == "Potion");

  frame_bytes = encode_frame(make_frame(InventoryRemove{7}, 26));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::inventory_remove);
  const auto decoded_inventory_remove = decode_message<InventoryRemove>(frames.front());
  assert(decoded_inventory_remove.has_value());
  assert(decoded_inventory_remove->slot == 7);

  frame_bytes = encode_frame(make_frame(InventoryClearRange{6, 12}, 27));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::inventory_clear_range);
  const auto decoded_inventory_clear = decode_message<InventoryClearRange>(frames.front());
  assert(decoded_inventory_clear.has_value());
  assert(decoded_inventory_clear->first_slot == 6);
  assert(decoded_inventory_clear->last_slot == 12);

  EquipmentSnapshot equipment_snapshot;
  equipment_snapshot.items.push_back(ItemSlotState{1, potion});
  frame_bytes = encode_frame(make_frame(equipment_snapshot, 28));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::equipment_snapshot);
  const auto decoded_equipment_snapshot = decode_message<EquipmentSnapshot>(frames.front());
  assert(decoded_equipment_snapshot.has_value());
  assert(decoded_equipment_snapshot->items.front().slot == 1);
  assert(decoded_equipment_snapshot->items.front().item.make_index == 1001);

  frame_bytes = encode_frame(make_frame(EquipItemRequest{1, 1001, "Potion"}, 29));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::equip_item_request);
  const auto decoded_equip = decode_message<EquipItemRequest>(frames.front());
  assert(decoded_equip.has_value());
  assert(decoded_equip->equipment_slot == 1);
  assert(decoded_equip->name == "Potion");

  frame_bytes = encode_frame(make_frame(UnequipItemRequest{1, 1001, "Potion"}, 30));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::unequip_item_request);
  const auto decoded_unequip = decode_message<UnequipItemRequest>(frames.front());
  assert(decoded_unequip.has_value());
  assert(decoded_unequip->item_make_index == 1001);

  frame_bytes = encode_frame(make_frame(DropItemRequest{1001, "Potion"}, 31));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::drop_item_request);
  const auto decoded_drop = decode_message<DropItemRequest>(frames.front());
  assert(decoded_drop.has_value());
  assert(decoded_drop->name == "Potion");

  ChatLine chat_line{"Hero: hello", 0xFFFFFF00U, 0x00000000U};
  frame_bytes = encode_frame(make_frame(chat_line, 32));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::chat_line);
  const auto decoded_chat_line = decode_message<ChatLine>(frames.front());
  assert(decoded_chat_line.has_value());
  assert(decoded_chat_line->text == "Hero: hello");

  ActorSay actor_say{1000, "Hero: over here", 0xFFFFFFFFU, 0x00000000U};
  frame_bytes = encode_frame(make_frame(actor_say, 33));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::actor_say);
  const auto decoded_actor_say = decode_message<ActorSay>(frames.front());
  assert(decoded_actor_say.has_value());
  assert(decoded_actor_say->actor_id == 1000);
  assert(decoded_actor_say->text == "Hero: over here");

  frame_bytes = encode_frame(make_frame(NpcClickRequest{2000}, 34));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::npc_click_request);
  const auto decoded_npc_click = decode_message<NpcClickRequest>(frames.front());
  assert(decoded_npc_click.has_value());
  assert(decoded_npc_click->actor_id == 2000);

  NpcDialog npc_dialog{2000, 384, "Shopkeeper/Hello <Buy/@buy>"};
  frame_bytes = encode_frame(make_frame(npc_dialog, 35));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::npc_dialog);
  const auto decoded_npc_dialog = decode_message<NpcDialog>(frames.front());
  assert(decoded_npc_dialog.has_value());
  assert(decoded_npc_dialog->merchant_id == 2000);
  assert(decoded_npc_dialog->text == "Shopkeeper/Hello <Buy/@buy>");

  NpcDialogSelectRequest npc_select{2000, "@buy"};
  frame_bytes = encode_frame(make_frame(npc_select, 36));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::npc_dialog_select_request);
  const auto decoded_npc_select = decode_message<NpcDialogSelectRequest>(frames.front());
  assert(decoded_npc_select.has_value());
  assert(decoded_npc_select->merchant_id == 2000);
  assert(decoded_npc_select->selection == "@buy");

  frame_bytes = encode_frame(make_frame(NpcDialogClose{2000}, 37));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::npc_dialog_close);
  const auto decoded_npc_close = decode_message<NpcDialogClose>(frames.front());
  assert(decoded_npc_close.has_value());
  assert(decoded_npc_close->merchant_id == 2000);

  frame_bytes =
      encode_frame(make_frame(MerchantRepairPriceRequest{2000, 1001, "Sword"}, 38));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::merchant_repair_price_request);
  const auto decoded_repair_price = decode_message<MerchantRepairPriceRequest>(frames.front());
  assert(decoded_repair_price.has_value());
  assert(decoded_repair_price->name == "Sword");

  StorageList storage;
  storage.merchant_id = 2000;
  storage.items.push_back(potion);
  frame_bytes = encode_frame(make_frame(storage, 39));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::storage_list);
  const auto decoded_storage = decode_message<StorageList>(frames.front());
  assert(decoded_storage.has_value());
  assert(decoded_storage->items.size() == 1);

  frame_bytes = encode_frame(make_frame(GroupState{true, true, {"Hero"}}, 40));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::group_state);
  const auto decoded_group = decode_message<GroupState>(frames.front());
  assert(decoded_group.has_value());
  assert(decoded_group->allow_group);

  frame_bytes =
      encode_frame(make_frame(TradeState{true, "Ally", {ItemSlotState{0, potion}}, {}, 10, 0,
                                         true, false},
                              41));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::trade_state);
  const auto decoded_trade = decode_message<TradeState>(frames.front());
  assert(decoded_trade.has_value());
  assert(decoded_trade->local_items.size() == 1);

  frame_bytes = encode_frame(make_frame(
      GuildState{true, "Guild", "Rank", "Notice", {GuildMemberState{"Hero", "Rank", true}},
                 {"Rank"}, true},
      42));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::guild_state);
  const auto decoded_guild = decode_message<GuildState>(frames.front());
  assert(decoded_guild.has_value());
  assert(decoded_guild->guild_name == "Guild");

  return 0;
}
