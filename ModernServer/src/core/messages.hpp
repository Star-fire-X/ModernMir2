#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "config/models.hpp"
#include "protocol/legacy_types.hpp"

namespace mir2 {

constexpr std::int32_t kLegacyPacketCode = static_cast<std::int32_t>(0xaa55aa55);
constexpr std::size_t kMaxLegacyQuestMarks = 256;
constexpr std::size_t kMaxLegacyScriptParams = 10;
constexpr std::size_t kMaxLegacySlaves = 5;

struct LegacyPacketHeader {
  std::int32_t code{kLegacyPacketCode};
  std::int32_t socket_number{0};
  std::uint16_t user_gate_index{0};
  std::uint16_t ident{0};
  std::uint16_t user_list_index{0};
  std::uint16_t temp{0};
  std::int32_t length{0};
};

struct LegacyPacket {
  LegacyPacketHeader header{};
  std::vector<std::uint8_t> body{};
};

struct CharacterSlaveRecord {
  std::string name{};
  std::int32_t slave_exp{0};
  std::uint8_t slave_exp_level{0};
  std::uint8_t slave_make_level{0};
  std::int32_t remain_royalty_sec{0};
  std::int32_t hp{0};
  std::int32_t mp{0};
};

struct CharacterRecord {
  std::string account_id{};
  std::string character_name{};
  std::string guild_name{};
  std::string guild_title{};
  std::string map_id{"0"};
  std::int32_t x{330};
  std::int32_t y{270};
  std::uint8_t dir{0};
  std::uint8_t light{0};
  std::uint8_t job{0};
  std::uint8_t sex{0};
  std::uint8_t hair{0};
  std::int32_t gold{0};
  std::int32_t feature{0};
  std::int32_t status{0};
  LegacyAbility ability{};
  std::array<LegacyUserItem, kMaxEquipSlots> equipped_items{};
  std::array<LegacyUserItem, kMaxBagItems> bag_items{};
  std::array<LegacyUserItem, kMaxSaveItems> storage_items{};
  std::array<LegacyUseMagicInfo, kMaxUserMagic> magics{};
  std::uint8_t attack_mode{1};
  std::int32_t pk_point{0};
  std::uint64_t death_time_ms{0};
  std::array<std::uint8_t, kMaxLegacyQuestMarks> quest_marks{};
  std::array<std::uint8_t, kMaxLegacyQuestMarks> quest_open_units{};
  std::array<std::uint8_t, kMaxLegacyQuestMarks> quest_units{};
  std::array<std::int32_t, kMaxLegacyScriptParams> script_params{};
  std::uint32_t daily_quest{0};
  std::array<CharacterSlaveRecord, kMaxLegacySlaves> slaves{};
};

struct MerchantProductRuntimeConfig {
  std::int32_t item_id{0};
  std::string item_name{};
  std::int32_t target_count{0};
  std::uint64_t refresh_ms{0};
  std::uint64_t last_refill_ms{0};
};

struct MerchantStateRecord {
  std::string merchant_key{};
  std::string npc_id{};
  std::string map_id{};
  std::vector<LegacyUserItem> goods{};
  std::unordered_map<std::int32_t, std::int32_t> prices{};
};

struct AccountRecord {
  std::string account_id{};
  std::string password{};
  std::string display_name{};
  std::string user_name{};
  std::string ss_no{};
  std::string phone{};
  std::string quiz{};
  std::string answer{};
  std::string email{};
  std::string quiz2{};
  std::string answer2{};
  std::string birthday{};
  std::string mobile_phone{};
  std::string memo1{};
  std::string memo2{};
  std::int32_t server_index{0};
  std::int32_t passwd_fail{0};
  std::int64_t passwd_fail_time_ms{0};
  bool banned{false};
};

enum class SessionEventKind {
  connected,
  disconnected,
  packet_received,
  send_packet,
  send_packet_and_close,
  force_disconnect
};

struct SessionEvent {
  SessionEventKind kind{SessionEventKind::connected};
  std::string gateway{};
  std::uint64_t session_id{0};
  std::string peer_address{};
  LegacyPacket packet{};
  std::string reason{};
  std::int32_t delay_ms{0};
  std::uint64_t session_seq{0};
};

enum class LogicCommandKind {
  authenticate,
  revoke_authentication,
  enter_world,
  turn,
  walk,
  run,
  attack,
  spell,
  say,
  click_npc,
  merchant_select,
  query_username,
  query_bag_items,
  query_storage_items,
  query_detail_goods,
  query_sell_price,
  query_repair_cost,
  drop_item,
  pickup_item,
  take_on_item,
  take_off_item,
  eat_item,
  drop_gold,
  revive,
  buy_item,
  sell_item,
  repair_item,
  storage_item,
  take_back_storage_item,
  trade_try,
  trade_cancel,
  trade_add_item,
  trade_remove_item,
  trade_set_gold,
  trade_accept,
  logout,
  raw_packet
};

struct LogicCommand {
  LogicCommandKind kind{LogicCommandKind::raw_packet};
  std::string gateway{"game_gateway"};
  std::uint64_t session_id{0};
  std::uint64_t session_seq{0};
  std::string account_id{};
  std::string character_name{};
  std::string map_id{};
  std::int32_t x{0};
  std::int32_t y{0};
  std::uint8_t dir{0};
  std::uint64_t target_actor_id{0};
  std::int32_t item_make_index{0};
  std::int32_t item_slot{-1};
  std::int32_t amount{0};
  CharacterRecord character{};
  LegacyDefaultMessage game_message{};
  std::string text{};
  LegacyPacket packet{};
  std::int32_t certification{0};
  std::int32_t client_version{0};
  std::int32_t client_checksum{0};
  bool start_new{false};
};

enum class ActorMailKind {
  spawn_player,
  spawn_monster,
  spawn_npc,
  system_notice,
  guild_membership_sync,
  turn,
  move,
  run,
  attack,
  spell,
  click_npc,
  merchant_select,
  query_username,
  query_bag_items,
  query_storage_items,
  query_detail_goods,
  query_sell_price,
  query_repair_cost,
  drop_item,
  pickup_item,
  take_on_item,
  take_off_item,
  eat_item,
  drop_gold,
  revive,
  buy_item,
  sell_item,
  repair_item,
  storage_item,
  take_back_storage_item,
  trade_try,
  trade_cancel,
  trade_add_item,
  trade_remove_item,
  trade_set_gold,
  trade_accept,
  despawn,
  transfer,
  persistence_loaded,
  legacy_delayed_effect,
  legacy_magic_lvexp,
  say
};

enum class LegacyDelayedEffectKind {
  none,
  delay_magic,
  mag_healing,
  mag_struck,
  monster_struck,
  open_health,
  make_poison,
  transparent
};

struct ActorMail {
  ActorMailKind kind{ActorMailKind::say};
  std::string map_id{};
  std::uint64_t actor_id{0};
  std::uint64_t session_id{0};
  std::uint64_t session_seq{0};
  std::uint64_t target_actor_id{0};
  std::int32_t item_make_index{0};
  std::int32_t item_slot{-1};
  std::int32_t amount{0};
  std::string name{};
  std::string npc_service{};
  std::string merchant_key{};
  std::vector<LegacyUserItem> merchant_items{};
  std::vector<MerchantProductRuntimeConfig> merchant_products{};
  std::unordered_map<std::int32_t, std::int32_t> merchant_prices{};
  std::vector<std::int32_t> legacy_deal_std_modes{};
  std::vector<NpcDialogSectionConfig> npc_dialog_sections{};
  std::int32_t npc_price_rate_percent{100};
  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t level{1};
  std::int32_t current_hp{0};
  std::int32_t current_mp{0};
  std::int32_t max_hp{0};
  std::int32_t attack_power{0};
  std::int32_t dc_min{0};
  std::int32_t dc_max{0};
  std::int32_t defense{0};
  std::int32_t magic_defense{0};
  std::int32_t mc{0};
  std::int32_t sc{0};
  std::int32_t exp_reward{0};
  std::int32_t life_attrib{0};
  std::int32_t max_mp{0};
  std::int32_t race_server{0};
  std::int32_t race_image{0};
  std::int32_t appearance{0};
  std::int32_t cool_eye{0};
  std::int32_t speed{0};
  std::int32_t accuracy{0};
  std::int32_t walk_speed_ms{20};
  std::int32_t walk_step{1};
  std::int32_t walk_wait_ms{0};
  std::int32_t attack_speed_ms{100};
  std::int32_t home_x{0};
  std::int32_t home_y{0};
  std::int32_t home_area{0};
  MonsterAiProfile monster_ai_profile{MonsterAiProfile::basic};
  std::uint64_t monster_search_rate_ms{0};
  bool legacy_spawn_group{false};
  std::int32_t monster_drop_gold{0};
  std::vector<LegacyUserItem> monster_drop_items{};
  std::uint64_t master_actor_id{0};
  bool monster_is_slave{false};
  std::int32_t slave_exp{0};
  std::int32_t slave_make_level{0};
  std::int32_t slave_exp_level{0};
  std::uint64_t master_royalty_time_ms{0};
  std::uint64_t slave_life_time_ms{0};
  bool monster_no_item{false};
  bool monster_tameable{true};
  std::uint32_t respawn_ms{0};
  std::uint8_t dir{0};
  std::uint8_t retry_count{0};
  LegacyDelayedEffectKind delayed_effect_kind{LegacyDelayedEffectKind::none};
  std::int32_t magic_id{0};
  std::int32_t power{0};
  std::int32_t range{0};
  std::int32_t magic_level{0};
  std::int32_t magic_train{0};
  std::uint32_t magic_lvexp_generation{0};
  std::int32_t poison_kind{0};
  std::int32_t poison_level{0};
  std::uint64_t duration_ticks{0};
  CharacterRecord character{};
  LegacyDefaultMessage game_message{};
  std::string payload{};
};

enum class OfflineGuildCharacterOpKind {
  unknown,
  approve,
  kick,
  transfer,
  title
};

struct OfflineGuildCharacterOp {
  OfflineGuildCharacterOpKind kind{OfflineGuildCharacterOpKind::unknown};
  std::string map_id{};
  std::uint64_t initiator_actor_id{0};
  std::string guild_name{};
  std::string target_name{};
  std::string title_name{};
  std::string target_map_id{};
};

inline std::string offline_guild_character_op_name(OfflineGuildCharacterOpKind kind) {
  switch (kind) {
    case OfflineGuildCharacterOpKind::approve:
      return "approve";
    case OfflineGuildCharacterOpKind::kick:
      return "kick";
    case OfflineGuildCharacterOpKind::transfer:
      return "transfer";
    case OfflineGuildCharacterOpKind::title:
      return "title";
    default:
      return "unknown";
  }
}

inline OfflineGuildCharacterOpKind parse_offline_guild_character_op_kind(std::string_view text) {
  if (text == "approve") {
    return OfflineGuildCharacterOpKind::approve;
  }
  if (text == "kick") {
    return OfflineGuildCharacterOpKind::kick;
  }
  if (text == "transfer") {
    return OfflineGuildCharacterOpKind::transfer;
  }
  if (text == "title") {
    return OfflineGuildCharacterOpKind::title;
  }
  return OfflineGuildCharacterOpKind::unknown;
}

inline std::string encode_offline_guild_character_op(const OfflineGuildCharacterOp& operation) {
  std::string encoded = "guild_offline\n";
  encoded += operation.map_id;
  encoded += '\n';
  encoded += std::to_string(operation.initiator_actor_id);
  encoded += '\n';
  encoded += offline_guild_character_op_name(operation.kind);
  encoded += '\n';
  encoded += operation.guild_name;
  encoded += '\n';
  encoded += operation.target_name;
  encoded += '\n';
  encoded += operation.title_name;
  encoded += '\n';
  encoded += operation.target_map_id;
  return encoded;
}

inline std::optional<OfflineGuildCharacterOp> decode_offline_guild_character_op(
    std::string_view encoded) {
  std::vector<std::string_view> fields;
  std::size_t start = 0;
  while (start <= encoded.size()) {
    const auto end = encoded.find('\n', start);
    if (end == std::string_view::npos) {
      fields.push_back(encoded.substr(start));
      break;
    }
    fields.push_back(encoded.substr(start, end - start));
    start = end + 1;
  }

  if (fields.size() < 6 || fields[0] != "guild_offline") {
    return std::nullopt;
  }

  std::uint64_t initiator_actor_id = 0;
  try {
    initiator_actor_id = static_cast<std::uint64_t>(std::stoull(std::string(fields[2])));
  } catch (...) {
    return std::nullopt;
  }

  OfflineGuildCharacterOp operation;
  operation.map_id = std::string(fields[1]);
  operation.initiator_actor_id = initiator_actor_id;
  operation.kind = parse_offline_guild_character_op_kind(fields[3]);
  operation.guild_name = std::string(fields[4]);
  operation.target_name = std::string(fields[5]);
  if (fields.size() >= 7) {
    operation.title_name = std::string(fields[6]);
  }
  if (fields.size() >= 8) {
    operation.target_map_id = std::string(fields[7]);
  }
  if (operation.kind == OfflineGuildCharacterOpKind::unknown || operation.map_id.empty() ||
      operation.initiator_actor_id == 0 || operation.guild_name.empty() ||
      operation.target_name.empty()) {
    return std::nullopt;
  }
  return operation;
}

enum class PersistRequestKind {
  ensure_schema,
  load_account,
  authenticate_account,
  load_castle_dialog_context,
  load_guild_castle_snapshot,
  save_guild_payload,
  save_guild_state,
  delete_guild,
  save_castle_state,
  create_account,
  update_account,
  change_password,
  load_character,
  load_character_by_name,
  list_characters,
  create_character,
  delete_character,
  save_character,
  load_merchant_states,
  save_merchant_state,
  record_audit,
  seed_runtime
};

struct PersistRequest {
  PersistRequestKind kind{PersistRequestKind::ensure_schema};
  std::string reply_to{};
  std::string account_id{};
  std::string guild_name{};
  std::string castle_name{};
  std::string password{};
  std::string new_password{};
  std::string character_name{};
  std::string payload_json{};
  AccountRecord account{};
  CharacterRecord character{};
  MerchantStateRecord merchant_state{};
  GuildState guild_state{};
  GuildCastleSnapshot guild_castle_snapshot{};
  std::string text{};
  std::string request_id{};
  std::int64_t timestamp_ms{0};
};

enum class PersistResultKind {
  schema_ready,
  account_loaded,
  account_authenticated,
  castle_dialog_context_loaded,
  guild_castle_snapshot_loaded,
  account_created,
  account_updated,
  password_changed,
  character_loaded,
  characters_listed,
  character_created,
  character_deleted,
  character_saved,
  merchant_states_loaded,
  merchant_state_saved,
  audit_recorded,
  seeded,
  error
};

struct PersistResult {
  PersistResultKind kind{PersistResultKind::schema_ready};
  std::string reply_to{};
  std::string account_id{};
  AccountRecord account{};
  CastleDialogContext castle_dialog_context{};
  GuildCastleSnapshot guild_castle_snapshot{};
  std::string character_name{};
  CharacterRecord character{};
  std::vector<MerchantStateRecord> merchant_states{};
  std::string error{};
  std::vector<CharacterRecord> characters{};
  std::string request_id{};
  std::int32_t result_code{0};
};

struct AuditEvent {
  std::string category{};
  std::string message{};
  std::string session_key{};
};

using BusMessage =
    std::variant<SessionEvent, LogicCommand, ActorMail, PersistRequest, PersistResult, AuditEvent>;

}  // namespace mir2
