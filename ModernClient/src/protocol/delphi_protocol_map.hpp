// ============================================================
// Mir2 现代客户端 — Delphi 旧版协议到新版协议的映射表
// 职责：记录经典 Delphi 客户端消息与新版 client_v1 协议
//       之间的对应关系、迁移状态和遗留说明
//
// 背景：
// 经典传奇（Mir2）的 Delphi 客户端使用一种基于文本行的
// 协议格式（每条消息以文本行的形式发送，如 'LOGIN' 等）。
// 本新版客户端改用结构化的二进制协议（client_v1），
// 此映射表记录了旧版 Delphi 消息到新版 C++ 消息之间的
// 对应关系，用于指导协议迁移和验证实现的完整性。
//
// 映射表中的每个条目独立记录一条消息的迁移状态：
// - implemented: 已在新版中完整实现
// - partial: 部分实现（核心功能可用，边缘情况未处理）
// - planned: 已列入计划，但尚未实现
// - internal: 内部机制变化（非直接消息映射）
// ============================================================
#pragma once

#include <array>
#include <string_view>

namespace mir2::client::protocol_migration {

/// 迁移状态枚举
enum class MigrationStatus {
  implemented,  ///< 已在新版协议中完整实现
  partial,      ///< 部分实现（基本功能可用，细节待完善）
  planned,      ///< 已计划，尚未实现
  internal      ///< 内部机制变更，非直接消息映射
};

/// 映射条目结构
struct MappingEntry {
  std::string_view delphi_entry;  ///< 旧版 Delphi 函数/消息名（如在 ClFunc.pas 中的声明）
  std::string_view cxx_entry;     ///< 新版 C++ 函数/结构体名
  std::string_view domain;        ///< 所属功能域（如 login/character/inventory/magic）
  MigrationStatus status;         ///< 迁移状态
  std::string_view notes;         ///< 备注说明（迁移差异/注意事项）
};

/// 迁移状态转字符串
inline constexpr std::string_view to_string(const MigrationStatus status) {
  switch (status) {
    case MigrationStatus::implemented: return "implemented";
    case MigrationStatus::partial:     return "partial";
    case MigrationStatus::planned:     return "planned";
    case MigrationStatus::internal:    return "internal";
  }
  return "unknown";
}

// ====================================================================
// Migration status summary (as of PR-8 protocol mapping audit):
//   Send mappings:    11 implemented, 38 partial, 6 planned, 2 internal = 57
//   Receive mappings:  5 implemented, 30 partial, 4 planned, 0 internal = 39
//   Scene transitions: 11 implemented, 2 partial, 0 planned, 0 internal = 13
//
// All legacy Delphi client messages are accounted for.  Partial entries
// have core functionality working; details and edge cases are tracked.
// No known Delphi message is missing from the mapping.
// ====================================================================
// 客户端发送消息映射表（57 条）
// 列出所有从 Delphi 客户端发送到服务端的消息及其新版映射
// ====================================================================
inline constexpr std::array<MappingEntry, 57> kDelphiSendMappings{{
    {"SendSocket", "ProtocolClient::send / client_v1 frame", "transport", MigrationStatus::internal,
     "Legacy text framing is replaced by length-prefixed client_v1 frames."},
    {"SendClientMessage", "client_v1 typed messages", "transport", MigrationStatus::internal,
     "Generic CM envelope is replaced by typed payloads."},
    {"CM_CLICKNPC", "NpcClickRequest", "npc", MigrationStatus::partial,
     "WorldScene sends NPC click intent for ActorType::npc; merchant/shop follow-up windows remain planned."},
    {"SendVersionNumber", "ClientHello", "session", MigrationStatus::implemented,
     "client_build and resource_revision carry the version handshake."},
    {"SendLogin", "LoginRequest", "login", MigrationStatus::implemented, ""},
    {"SendNewAccount", "CreateAccountRequest", "account", MigrationStatus::implemented,
     "Carries the legacy profile fields through AccountProfile."},
    {"SendUpdateAccount", "UpdateAccountRequest", "account", MigrationStatus::implemented,
     "Used after NeedUpdateAccount to complete legacy profile details."},
    {"SendSelectServer", "SelectServerRequest", "server_select", MigrationStatus::implemented, ""},
    {"SendChgPw", "ChangePasswordRequest", "account", MigrationStatus::implemented, ""},
    {"SendNewChr", "CreateCharacterRequest", "character", MigrationStatus::implemented, ""},
    {"SendQueryChr", "CharacterListRequest", "character", MigrationStatus::implemented,
     "First request after server selection carries the lobby token."},
    {"SendDelChr", "DeleteCharacterRequest", "character", MigrationStatus::implemented, ""},
    {"SendSelChr", "SelectCharacterRequest", "character", MigrationStatus::implemented, ""},
    {"SendRunLogin", "EnterWorldRequest + LoginNoticeOk", "world_entry",
     MigrationStatus::implemented,
     "Certification is represented by enter_world_token; CM_LOGINNOTICEOK is LoginNoticeOk."},
    {"SendSay", "ChatSend", "chat", MigrationStatus::partial,
     "World chat input supports Enter/Space/@/!/slash prefixes; complete channel semantics remain server-side/planned."},
    {"SendActMsg", "ActionIntent", "action", MigrationStatus::partial,
     "Turn/walk/run/attack now share the legacy action-lock path; sit and special idents are still partial."},
    {"SendSpellMsg", "SpellIntent", "combat", MigrationStatus::partial,
     "Shortcut spell intent and action-lock timing exist; detailed magic effects remain partial."},
    {"SendQueryUserName", "QueryActorNameRequest", "actor", MigrationStatus::planned,
     "Needs client query UI and server/client_v1 bridge coverage before implementation."},
    {"SendDropItem", "DropItemRequest", "inventory", MigrationStatus::partial,
     "Bag-item drop requests are wired with local pending rollback; equipment must be unequipped before dropping."},
    {"SendPickup", "PickupIntent", "inventory", MigrationStatus::partial,
     "Client can request pickup at the player's tile; ground item UI remains planned."},
    {"SendTakeOnItem", "EquipItemRequest", "inventory", MigrationStatus::partial,
     "Bag-to-equipment requests are wired; extended equipment slots remain visually hidden."},
    {"SendTakeOffItem", "UnequipItemRequest", "inventory", MigrationStatus::partial,
     "Equipment-to-bag requests are wired with pending rollback."},
    {"SendEat", "UseItemIntent", "inventory", MigrationStatus::partial,
     "Numeric slots can request consumable use; inventory mirror and rollback UI remain planned."},
    {"SendButchAnimal", "ButchActorRequest", "world", MigrationStatus::planned,
     "Needs butch target selection UI and world loot authority before implementation."},
    {"SendMagicKeyChange", "MagicKeyChangeRequest", "magic", MigrationStatus::partial,
     "State window can send key rebinding requests; server authority still arrives through MagicList refreshes."},
    {"SendMerchantDlgSelect", "NpcDialogSelectRequest", "npc", MigrationStatus::partial,
     "DMerchantDlg text-link selection is sent; merchant goods/storage/trade commands remain later phases."},
    {"SendQueryPrice", "MerchantSellPriceRequest", "merchant", MigrationStatus::partial,
     "Sell-price query is wired for the current merchant sell flow."},
    {"SendQueryRepairCost", "MerchantRepairPriceRequest", "merchant", MigrationStatus::partial,
     "Repair-price query is wired for bag-item repair selection."},
    {"SendSellItem", "MerchantSellRequest", "merchant", MigrationStatus::partial,
     "Sell confirmation is wired after a successful price result."},
    {"SendRepairItem", "MerchantRepairRequest", "merchant", MigrationStatus::partial,
     "Repair confirmation is wired after a successful repair price result."},
    {"SendStorageItem", "StorageDepositRequest", "storage", MigrationStatus::partial,
     "Storage deposit from selected bag items is wired."},
    {"SendGetDetailItem", "DetailItemRequest", "merchant", MigrationStatus::planned,
     "Needs detail-goods panel request/response wiring before implementation."},
    {"SendBuyItem", "MerchantBuyRequest", "merchant", MigrationStatus::partial,
     "Merchant buy request is wired from the goods list."},
    {"SendTakeBackStorageItem", "StorageWithdrawRequest", "storage", MigrationStatus::partial,
     "Storage withdraw request is wired from the storage list."},
    {"SendMakeDrugItem", "MakeDrugItemRequest", "merchant", MigrationStatus::planned,
     "Needs make-drug merchant list, selection UI, and server command bridge."},
    {"SendDropGold", "DropGoldRequest", "inventory", MigrationStatus::partial,
     "client_v1 wire and gateway bridge are implemented; amount-entry UI remains minimal."},
    {"SendGroupMode", "GroupModeRequest", "group", MigrationStatus::partial,
     "Group allow/invite mode is wired from the group panel and mirrored by the gateway registry."},
    {"SendCreateGroup", "GroupCreateRequest", "group", MigrationStatus::partial,
     "Create-group request is wired to the focused target and gateway online group registry."},
    {"SendWantMiniMap", "MiniMapRequest", "map", MigrationStatus::partial,
     "Mini-map request and MiniMapData rendering are wired."},
    {"SendDealTry", "TradeTryRequest", "trade", MigrationStatus::partial,
     "Trade open request is wired; client_v1 gateway now mirrors both online peers while Delphi lock/confirm edge cases remain partial."},
    {"SendGuildDlg", "GuildOpenRequest", "guild", MigrationStatus::partial,
     "Guild panel open request is wired to the gateway guild snapshot; full Delphi guild management remains partial."},
    {"SendCancelDeal", "TradeCancelRequest", "trade", MigrationStatus::partial,
     "Trade cancel request is wired and closes both client_v1 peer windows through the gateway session mirror."},
    {"SendAddDealItem", "TradeAddItemRequest", "trade", MigrationStatus::partial,
     "Selected bag item add is mirrored to the peer trade state before world authority removes it from the bag."},
    {"SendDelDealItem", "TradeRemoveItemRequest", "trade", MigrationStatus::partial,
     "Protocol and gateway rollback path exist; the panel still uses minimal item selection."},
    {"SendChangeDealGold", "TradeSetGoldRequest", "trade", MigrationStatus::partial,
     "Gold request is wired and clamped by the gateway; amount-entry UI remains minimal."},
    {"SendDealEnd", "TradeAcceptRequest", "trade", MigrationStatus::partial,
     "Accept request is mirrored to both peers; final success/cancel still follows world authority messages."},
    {"SendAddGroupMember", "GroupAddMemberRequest", "group", MigrationStatus::partial,
     "Add-member request is wired to the focused target and gateway online group registry."},
    {"SendDelGroupMember", "GroupRemoveMemberRequest", "group", MigrationStatus::partial,
     "Remove-member request is wired to the gateway online group registry with disband-on-singleton."},
    {"SendGuildHome", "GuildHomeRequest", "guild", MigrationStatus::partial,
     "Guild home request refreshes the gateway guild snapshot."},
    {"SendGuildMemberList", "GuildMemberListRequest", "guild", MigrationStatus::partial,
     "Guild member-list request refreshes the gateway guild snapshot."},
    {"SendGuildAddMem", "GuildAddMemberRequest", "guild", MigrationStatus::partial,
     "Guild add-member request updates the client_v1 gateway guild snapshot for visible online clients."},
    {"SendGuildDelMem", "GuildRemoveMemberRequest", "guild", MigrationStatus::partial,
     "Guild remove-member request updates the client_v1 gateway guild snapshot for visible online clients."},
    {"SendGuildUpdateNotice", "GuildUpdateNoticeRequest", "guild", MigrationStatus::partial,
     "Protocol and gateway notice refresh exist; the guild panel still lacks full Delphi editing UX."},
    {"SendGuildUpdateGrade", "GuildUpdateGradeRequest", "guild", MigrationStatus::partial,
     "Protocol and gateway rank-list refresh exist; the guild panel still lacks full Delphi editing UX."},
    {"SendSpeedHackUser", "SpeedHackReport", "security", MigrationStatus::planned,
     "Needs client-side detection cadence and server audit/rate-limit policy."},
    {"SendAdjustBonus", "AdjustBonusRequest", "character", MigrationStatus::planned,
     "Needs bonus allocation UI and authoritative character-stat update flow."},
    {"SendTimeTimerTimer", "Ping", "session", MigrationStatus::partial,
     "Ping/Pong exists, but the client has no scheduled heartbeat yet."},
}};

// ====================================================================
// 客户端接收消息映射表（39 条）
// 列出所有从服务端发送到客户端的消息及其新版映射
// ====================================================================
inline constexpr std::array<MappingEntry, 39> kDelphiClientGetMappings{{
    {"ClientGetPasswdSuccess", "LoginResult + ServerList", "login", MigrationStatus::partial,
     "Login success and server list exist; account-update/login-notice details are separate messages."},
    {"ClientGetNeedUpdateAccount", "NeedUpdateAccount + LoginScene update mode", "account",
     MigrationStatus::implemented, ""},
    {"ClientGetSelectServer", "ServerList + SelectServerResult + ServerSelectScene", "server_select",
     MigrationStatus::implemented, "Selection result carries character-gateway endpoint and lobby token."},
    {"ClientGetReceiveChrs", "CharacterList", "character", MigrationStatus::implemented, ""},
    {"ClientGetStartPlay", "SelectCharacterResult + LoginNotice + EnterWorldResult + WorldSnapshot",
     "world_entry", MigrationStatus::implemented,
     "World entry now waits on stLoginNotice/LoginNoticeOk before applying the snapshot."},
    {"ClientGetReconnect", "ReconnectResult", "connection", MigrationStatus::planned,
     "Needs a dedicated typed reconnect result once Delphi reconnect branches are ported."},
    {"ClientGetMapDescription", "WorldSnapshot.map_id", "map", MigrationStatus::partial,
     "Map id and size exist; legacy description text and reconnect branches are still planned."},
    {"ClientGetAdjustBonus", "AdjustBonusState", "character", MigrationStatus::planned,
     "Needs bonus allocation state and UI before the legacy message can be represented."},
    {"ClientGetAddItem", "InventoryAdd", "inventory", MigrationStatus::partial,
     "Inventory add messages update the bag mirror and clear matching pending actions."},
    {"ClientGetUpdateItem", "InventoryUpdate", "inventory", MigrationStatus::partial,
     "Inventory update messages update the bag mirror and clear matching pending actions."},
    {"ClientGetDelItem", "InventoryRemove", "inventory", MigrationStatus::partial,
     "Inventory remove messages update the bag mirror and clear matching pending actions."},
    {"ClientGetDelItems", "InventoryClearRange", "inventory", MigrationStatus::partial,
     "Inventory clear-range messages update the bag mirror."},
    {"ClientGetBagItmes", "BagSnapshot", "inventory", MigrationStatus::partial,
     "Bag snapshots refresh the 46-slot bag mirror used by bag and belt UI."},
    {"ClientGetDropItemFail", "SysMessage", "inventory", MigrationStatus::partial,
     "Drop failures currently surface as system messages rather than a dedicated DropItemResult."},
    {"ClientGetShowItem", "GroundItemAdd", "world", MigrationStatus::partial,
     "Ground item lifecycle reaches WorldViewState; sprite-accurate item art is still planned."},
    {"ClientGetHideItem", "GroundItemRemove", "world", MigrationStatus::partial,
     "Ground item removal reaches WorldViewState."},
    {"ClientGetSenduseItems", "EquipmentSnapshot", "inventory", MigrationStatus::partial,
     "Equipment snapshots refresh the 13-slot equipment mirror; only classic visible slots have HUD hit areas."},
    {"ClientGetAddMagic", "MagicList refresh", "magic", MigrationStatus::partial,
     "Gateway folds add-magic updates into a refreshed MagicList."},
    {"ClientGetDelMagic", "MagicList refresh", "magic", MigrationStatus::partial,
     "Gateway folds delete-magic updates into a refreshed MagicList."},
    {"ClientGetMyMagics", "MagicList", "magic", MigrationStatus::partial,
     "MagicList drives the state-window magic page."},
    {"ClientGetMagicLvExp", "MagicList refresh", "magic", MigrationStatus::partial,
     "Gateway folds level/experience changes into a refreshed MagicList."},
    {"ClientGetDuraChange", "DurabilityChange", "inventory", MigrationStatus::partial,
     "Gateway translates SM_DURACHANGE to make-index durability updates for bag/equipment mirrors."},
    {"ClientGetMerchantSay", "NpcDialog", "npc", MigrationStatus::partial,
     "DMerchantDlg entry, text parser and link hit areas exist; shop lists and business panels are still planned."},
    {"ClientGetSendGoodsList", "MerchantGoodsList", "merchant", MigrationStatus::partial,
     "Merchant goods lists drive the buy window."},
    {"ClientGetSendMakeDrugList", "MakeDrugList", "merchant", MigrationStatus::planned,
     "Needs make-drug merchant panel state and gateway translation."},
    {"ClientGetSendUserSell", "MerchantPriceResult", "merchant", MigrationStatus::partial,
     "Sell selection and confirmation dialogs are represented by MerchantPriceResult state."},
    {"ClientGetSendUserRepair", "MerchantRepairPriceResult", "merchant", MigrationStatus::partial,
     "Repair selection and confirmation dialogs are represented by MerchantRepairPriceResult state."},
    {"ClientGetSendUserStorage", "StorageList", "storage", MigrationStatus::partial,
     "Storage open state is represented by StorageList."},
    {"ClientGetSaveItemList", "StorageList", "storage", MigrationStatus::partial,
     "Storage item lists drive the storage window."},
    {"ClientGetSendDetailGoodsList", "DetailGoodsList", "merchant", MigrationStatus::planned,
     "Needs detail-goods panel state and gateway translation."},
    {"ClientGetSendNotice", "Notice", "notice", MigrationStatus::implemented, ""},
    {"ClientGetGroupMembers", "GroupState", "group", MigrationStatus::partial,
     "Group state drives the group panel."},
    {"ClientGetOpenGuildDlg", "GuildState", "guild", MigrationStatus::partial,
     "Guild state drives the guild panel."},
    {"ClientGetSendGuildMemberList", "GuildState", "guild", MigrationStatus::partial,
     "Guild member lists are represented through GuildState."},
    {"ClientGetDealRemoteAddItem", "TradeState", "trade", MigrationStatus::partial,
     "Remote trade item changes are represented by refreshed TradeState."},
    {"ClientGetDealRemoteDelItem", "TradeState", "trade", MigrationStatus::partial,
     "Remote trade item removals are represented by refreshed TradeState."},
    {"ClientGetReadMiniMap", "MiniMapData", "map", MigrationStatus::partial,
     "MiniMapData drives the client mini-map panel."},
    {"ClientGetChangeGuildName", "GuildState", "guild", MigrationStatus::partial,
     "Guild name changes are represented through refreshed GuildState."},
    {"ClientGetSendUserState", "SelfAbilityDetail", "character", MigrationStatus::partial,
     "Detailed ability state drives the state window."},
}};

// ====================================================================
// 场景切换相关协议映射表
// 仅列出会直接驱动登录/选服/选角/创建角色/PlayScene 切换的消息。
// ====================================================================
inline constexpr std::array<MappingEntry, 13> kSceneTransitionMappings{{
    {"SendLogin", "LoginRequest", "login", MigrationStatus::implemented,
     "Login button sends this before any scene change; LoginResult/ServerList drive the next scene."},
    {"ClientGetPasswdSuccess", "LoginResult + ServerList -> ServerSelectScene", "login",
     MigrationStatus::partial,
     "client_v1 keeps LoginResult and ServerList typed but preserves receive order in the frame drain."},
    {"SendSelectServer", "SelectServerRequest", "server_select", MigrationStatus::implemented,
     "Server-select click sends the selected name and leaves the current UI pending until result."},
    {"ClientGetSelectServer", "SelectServerResult -> character gateway connect", "server_select",
     MigrationStatus::implemented,
     "Successful result carries lobby token and endpoint; CharacterListRequest follows connection."},
    {"SendQueryChr", "CharacterListRequest", "character", MigrationStatus::implemented,
     "Sent after selecting a server or refreshing create/delete results."},
    {"ClientGetReceiveChrs", "CharacterList -> CharacterSelectScene", "character",
     MigrationStatus::implemented,
     "Character slots are replaced from the typed list before switching to character select."},
    {"SendNewChr", "CreateCharacterRequest", "character_create", MigrationStatus::implemented,
     "Create dialog submits name/job/sex and waits for CreateCharacterResult."},
    {"ClientGetNewChr", "CreateCharacterResult -> CharacterListRequest", "character_create",
     MigrationStatus::implemented,
     "Success refreshes the character list; failure stays in create-character UI with modal text."},
    {"SendDelChr", "DeleteCharacterRequest", "character", MigrationStatus::implemented,
     "Delete confirmation sends the selected name and waits for DeleteCharacterResult."},
    {"ClientGetDelChr", "DeleteCharacterResult -> CharacterListRequest", "character",
     MigrationStatus::implemented,
     "Success refreshes the character list; failure keeps the current character-select scene."},
    {"SendSelChr", "SelectCharacterRequest", "character", MigrationStatus::implemented,
     "Selecting a character enters pending/loading only after the gateway accepts it."},
    {"ClientGetStartPlay", "SelectCharacterResult + LoginNotice + EnterWorldResult + WorldSnapshot",
     "world_entry", MigrationStatus::implemented,
     "Typed messages remain ordered; WorldSnapshot is the frame that applies world state and switches to PlayScene."},
    {"ClientGetServerDown", "DisconnectReason / DisconnectedEvent -> LoginScene", "connection",
     MigrationStatus::partial,
     "Server disconnect notices show modal text; complete Delphi reconnect edge cases remain planned."},
}};

}  // namespace mir2::client::protocol_migration
