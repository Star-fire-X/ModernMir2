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
    {"SendQueryUserName", "QueryActorNameRequest", "actor", MigrationStatus::planned, ""},
    {"SendDropItem", "DropItemRequest", "inventory", MigrationStatus::planned, ""},
    {"SendPickup", "PickupIntent", "inventory", MigrationStatus::partial,
     "Client can request pickup at the player's tile; ground item UI remains planned."},
    {"SendTakeOnItem", "EquipItemRequest", "inventory", MigrationStatus::planned, ""},
    {"SendTakeOffItem", "UnequipItemRequest", "inventory", MigrationStatus::planned, ""},
    {"SendEat", "UseItemIntent", "inventory", MigrationStatus::partial,
     "Numeric slots can request consumable use; inventory mirror and rollback UI remain planned."},
    {"SendButchAnimal", "ButchActorRequest", "world", MigrationStatus::planned, ""},
    {"SendMagicKeyChange", "MagicKeyChangeRequest", "magic", MigrationStatus::planned, ""},
    {"SendMerchantDlgSelect", "NpcDialogSelectRequest", "npc", MigrationStatus::partial,
     "DMerchantDlg text-link selection is sent; merchant goods/storage/trade commands remain later phases."},
    {"SendQueryPrice", "QueryPriceRequest", "merchant", MigrationStatus::planned, ""},
    {"SendQueryRepairCost", "QueryRepairCostRequest", "merchant", MigrationStatus::planned, ""},
    {"SendSellItem", "SellItemRequest", "merchant", MigrationStatus::planned, ""},
    {"SendRepairItem", "RepairItemRequest", "merchant", MigrationStatus::planned, ""},
    {"SendStorageItem", "StoreItemRequest", "storage", MigrationStatus::planned, ""},
    {"SendGetDetailItem", "DetailItemRequest", "merchant", MigrationStatus::planned, ""},
    {"SendBuyItem", "BuyItemRequest", "merchant", MigrationStatus::planned, ""},
    {"SendTakeBackStorageItem", "TakeBackStorageItemRequest", "storage", MigrationStatus::planned, ""},
    {"SendMakeDrugItem", "MakeDrugItemRequest", "merchant", MigrationStatus::planned, ""},
    {"SendDropGold", "DropGoldRequest", "inventory", MigrationStatus::planned, ""},
    {"SendGroupMode", "GroupModeRequest", "group", MigrationStatus::planned, ""},
    {"SendCreateGroup", "CreateGroupRequest", "group", MigrationStatus::planned, ""},
    {"SendWantMiniMap", "MiniMapRequest", "map", MigrationStatus::planned, ""},
    {"SendDealTry", "TradeTryRequest", "trade", MigrationStatus::planned, ""},
    {"SendGuildDlg", "GuildDialogRequest", "guild", MigrationStatus::planned, ""},
    {"SendCancelDeal", "TradeCancelRequest", "trade", MigrationStatus::planned, ""},
    {"SendAddDealItem", "TradeAddItemRequest", "trade", MigrationStatus::planned, ""},
    {"SendDelDealItem", "TradeRemoveItemRequest", "trade", MigrationStatus::planned, ""},
    {"SendChangeDealGold", "TradeGoldRequest", "trade", MigrationStatus::planned, ""},
    {"SendDealEnd", "TradeConfirmRequest", "trade", MigrationStatus::planned, ""},
    {"SendAddGroupMember", "AddGroupMemberRequest", "group", MigrationStatus::planned, ""},
    {"SendDelGroupMember", "RemoveGroupMemberRequest", "group", MigrationStatus::planned, ""},
    {"SendGuildHome", "GuildHomeRequest", "guild", MigrationStatus::planned, ""},
    {"SendGuildMemberList", "GuildMemberListRequest", "guild", MigrationStatus::planned, ""},
    {"SendGuildAddMem", "GuildAddMemberRequest", "guild", MigrationStatus::planned, ""},
    {"SendGuildDelMem", "GuildRemoveMemberRequest", "guild", MigrationStatus::planned, ""},
    {"SendGuildUpdateNotice", "GuildUpdateNoticeRequest", "guild", MigrationStatus::planned, ""},
    {"SendGuildUpdateGrade", "GuildUpdateGradeRequest", "guild", MigrationStatus::planned, ""},
    {"SendSpeedHackUser", "SpeedHackReport", "security", MigrationStatus::planned, ""},
    {"SendAdjustBonus", "AdjustBonusRequest", "character", MigrationStatus::planned, ""},
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
    {"ClientGetReconnect", "ReconnectResult", "connection", MigrationStatus::planned, ""},
    {"ClientGetMapDescription", "WorldSnapshot.map_id", "map", MigrationStatus::partial,
     "Map id and size exist; legacy description text and reconnect branches are still planned."},
    {"ClientGetAdjustBonus", "AdjustBonusState", "character", MigrationStatus::planned, ""},
    {"ClientGetAddItem", "InventoryAdd", "inventory", MigrationStatus::planned, ""},
    {"ClientGetUpdateItem", "InventoryUpdate", "inventory", MigrationStatus::planned, ""},
    {"ClientGetDelItem", "InventoryRemove", "inventory", MigrationStatus::planned, ""},
    {"ClientGetDelItems", "InventoryClearRange", "inventory", MigrationStatus::planned, ""},
    {"ClientGetBagItmes", "BagSnapshot", "inventory", MigrationStatus::planned, ""},
    {"ClientGetDropItemFail", "DropItemResult", "inventory", MigrationStatus::planned, ""},
    {"ClientGetShowItem", "GroundItemAdd", "world", MigrationStatus::partial,
     "Ground item lifecycle reaches WorldViewState; sprite-accurate item art is still planned."},
    {"ClientGetHideItem", "GroundItemRemove", "world", MigrationStatus::partial,
     "Ground item removal reaches WorldViewState."},
    {"ClientGetSenduseItems", "EquipmentSnapshot", "inventory", MigrationStatus::planned, ""},
    {"ClientGetAddMagic", "MagicAdd", "magic", MigrationStatus::planned, ""},
    {"ClientGetDelMagic", "MagicRemove", "magic", MigrationStatus::planned, ""},
    {"ClientGetMyMagics", "MagicList", "magic", MigrationStatus::planned, ""},
    {"ClientGetMagicLvExp", "MagicProgress", "magic", MigrationStatus::planned, ""},
    {"ClientGetDuraChange", "DurabilityChange", "inventory", MigrationStatus::planned, ""},
    {"ClientGetMerchantSay", "NpcDialog", "npc", MigrationStatus::partial,
     "DMerchantDlg entry, text parser and link hit areas exist; shop lists and business panels are still planned."},
    {"ClientGetSendGoodsList", "GoodsList", "merchant", MigrationStatus::planned, ""},
    {"ClientGetSendMakeDrugList", "MakeDrugList", "merchant", MigrationStatus::planned, ""},
    {"ClientGetSendUserSell", "UserSellOpen", "merchant", MigrationStatus::planned, ""},
    {"ClientGetSendUserRepair", "UserRepairOpen", "merchant", MigrationStatus::planned, ""},
    {"ClientGetSendUserStorage", "UserStorageOpen", "storage", MigrationStatus::planned, ""},
    {"ClientGetSaveItemList", "StorageItemList", "storage", MigrationStatus::planned, ""},
    {"ClientGetSendDetailGoodsList", "DetailGoodsList", "merchant", MigrationStatus::planned, ""},
    {"ClientGetSendNotice", "Notice", "notice", MigrationStatus::implemented, ""},
    {"ClientGetGroupMembers", "GroupMembers", "group", MigrationStatus::planned, ""},
    {"ClientGetOpenGuildDlg", "GuildDialogOpen", "guild", MigrationStatus::planned, ""},
    {"ClientGetSendGuildMemberList", "GuildMemberList", "guild", MigrationStatus::planned, ""},
    {"ClientGetDealRemoteAddItem", "TradeRemoteAddItem", "trade", MigrationStatus::planned, ""},
    {"ClientGetDealRemoteDelItem", "TradeRemoteRemoveItem", "trade", MigrationStatus::planned, ""},
    {"ClientGetReadMiniMap", "MiniMapData", "map", MigrationStatus::planned, ""},
    {"ClientGetChangeGuildName", "GuildNameChange", "guild", MigrationStatus::planned, ""},
    {"ClientGetSendUserState", "UserStateSnapshot", "character", MigrationStatus::planned, ""},
}};

}  // namespace mir2::client::protocol_migration
