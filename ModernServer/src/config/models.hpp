#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <array>
#include <string>
#include <vector>

namespace mir2 {

struct RuntimeConfig {
  std::filesystem::path log_dir{"logs"};
  std::filesystem::path data_dir{"data"};
  std::filesystem::path asset_root{};
  std::filesystem::path legacy_admin_list{"Envir/AdminList.txt"};
  std::filesystem::path status_file{"runtime/status.json"};
  std::size_t default_queue_capacity{4096};
  std::size_t io_threads{2};
  bool enable_legacy_gateways{false};
  bool enable_client_v1_gateways{true};
  bool legacy_approval_mode{false};
  std::size_t backpressure_threshold{3072};
  std::size_t disconnect_threshold{3};
  std::uint32_t castle_context_refresh_ms{5000};
  std::string login_notice_title{"Notice"};
  std::string login_notice_text{};
  std::string castle_name{"Sabuk"};
  std::string default_castle_war_date{"Unknown"};
  std::string no_active_wars_text{"No active wars."};
  std::string unclaimed_castle_owner{"Unclaimed"};
  std::string unclaimed_castle_lord{"Unclaimed"};
  std::string castle_owner_role_label{"Castle Owner"};
  std::string castle_owner_guild_role_label{"Owner"};
  std::string castle_challenger_role_label{"Challenger"};
  std::string castle_rival_role_label{"Rival"};
  std::string castle_unknown_role_label{"Unknown"};
  std::string castle_war_entry_listed_label{"Listed"};
  std::string castle_war_entry_unlisted_label{"Not Listed"};
  std::string castle_war_status_active_label{"Active"};
  std::string castle_war_status_available_label{"Available"};
  std::string castle_role_change_owner_label{"Castle Owner"};
  std::string castle_role_change_challenger_label{"Castle Challenger"};
  std::string castle_claim_summary_template{"Castle claimed for guild <$GUILD>."};
  std::string castle_war_summary_template{
      "Castle war declared against <$TARGETGUILD> for <$GOLD> gold."};
  std::string castle_claim_require_guild_template{"Join a guild before claiming the castle."};
  std::string castle_claim_missing_guild_template{
      "Guild data is unavailable. Try again in a moment."};
  std::string castle_claim_only_lord_template{"Only the guild lord can claim the castle."};
  std::string castle_war_require_guild_template{"Join a guild before declaring war."};
  std::string castle_war_missing_guild_template{
      "Guild data is unavailable. Try again in a moment."};
  std::string castle_war_only_lord_template{"Only the guild lord can declare war."};
  std::string castle_war_usage_template{"Usage: @castle war <guild_name>"};
  std::string castle_war_self_target_template{"Your guild cannot declare war on itself."};
  std::string castle_war_target_missing_template{"Target guild not found."};
  std::string castle_war_already_registered_template{
      "Castle war against <$TARGETGUILD> is already registered."};
  std::string castle_war_need_gold_template{"You need <$GOLD> gold to declare war."};
  std::string guild_create_summary_template{"Guild <$GUILD> created."};
  std::string guild_apply_summary_template{"Application sent to guild <$GUILD>."};
  std::string guild_withdraw_summary_template{"Withdrew application from guild <$GUILD>."};
  std::string guild_approve_summary_template{"Approved guild application for <$TARGET>."};
  std::string guild_reject_summary_template{"Rejected guild application for <$TARGET>."};
  std::string guild_kick_summary_template{"Kicked guild member <$TARGET>."};
  std::string guild_title_summary_template{"Set guild title for <$TARGET> to <$TITLE>."};
  std::string guild_transfer_summary_template{"Transferred guild leadership to <$TARGET>."};
  std::string guild_leave_summary_template{"You left <$GUILD>."};
  std::string guild_leave_transfer_summary_template{
      "You left <$GUILD>. New lord: <$NEWLORD>."};
  std::string guild_disband_summary_template{"Guild <$GUILD> has been disbanded."};
  std::string guild_membership_cleared_summary_template{"Guild membership cleared."};
  std::string guild_apply_alert_template{"<$TARGET> applied to join <$GUILD>."};
  std::string guild_withdraw_alert_template{"<$TARGET> withdrew the application to <$GUILD>."};
  std::string guild_approved_notice_template{"Your application to <$GUILD> was approved."};
  std::string guild_rejected_notice_template{"Your application to <$GUILD> was rejected."};
  std::string guild_removed_notice_template{"You were removed from guild <$GUILD>."};
  std::string guild_new_lord_notice_template{"You are now the guild lord of <$GUILD>."};
  std::string guild_title_changed_notice_template{"Your guild title is now <$TITLE>."};
  std::string guild_create_leave_current_template{
      "Leave your current guild before creating a new one."};
  std::string guild_create_choose_name_template{"Choose a guild name first."};
  std::string guild_create_name_unavailable_template{"That guild already exists."};
  std::string guild_create_need_gold_template{"You need <$GOLD> gold to found a guild."};
  std::string guild_apply_leave_current_template{
      "Leave your current guild before joining another."};
  std::string guild_apply_choose_guild_template{"Choose a guild first."};
  std::string guild_not_found_template{"Guild not found."};
  std::string guild_apply_already_pending_template{
      "Your application to <$GUILD> is already pending."};
  std::int32_t guild_war_fee{30000};
  std::int32_t upgrade_weapon_fee{10000};
  std::int32_t guild_create_fee{10000};
  std::string black_stone_name{"BlackStone"};
  std::int32_t legacy_user_full_count{500};
  std::int32_t legacy_zen_fast_step{300};
  std::optional<std::uint32_t> legacy_random_seed{};
};

struct PortBinding {
  std::string address{"127.0.0.1"};
  std::uint16_t port{0};
};

struct PortConfig {
  PortBinding login_gateway{};
  PortBinding game_gateway{};
  PortBinding client_v1_login_gateway{"127.0.0.1", 5600};
  PortBinding client_v1_game_gateway{"127.0.0.1", 7100};
};

struct LogicBudgetConfig {
  std::uint32_t tick_ms{10};
  std::uint32_t player_budget_ms{30};
  std::uint32_t player_input_budget_per_tick{0};
  std::uint32_t monster_budget_ms{30};
  std::uint32_t spawn_budget_ms{30};
  std::uint32_t npc_budget_ms{5};
  std::uint32_t net_flush_budget_ms{30};
};

struct MapZoneConfig {
  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t width{0};
  std::int32_t height{0};
};

struct MapGateConfig {
  std::int32_t x{0};
  std::int32_t y{0};
  std::string target_map_id{};
  std::int32_t target_x{0};
  std::int32_t target_y{0};
  bool require_doors_open{true};
};

struct NpcDialogSectionConfig {
  std::string action{};
  std::string text{};
};

struct MapEntryQuestConfig {
  std::string qfile{};
  std::vector<NpcDialogSectionConfig> dialog_sections{};
};

struct MapConfig {
  std::string id{};
  std::string title{};
  std::filesystem::path source_map{};
  std::int32_t width{0};
  std::int32_t height{0};
  std::int32_t home_x{0};
  std::int32_t home_y{0};
  bool allow_pk{true};
  std::vector<MapZoneConfig> safe_zones{};
  bool law_full{false};
  bool fight_zone{false};
  bool fight3_zone{false};
  bool daylight{false};
  bool darkness{false};
  bool no_reconnect{false};
  bool need_hole{false};
  bool no_recall{false};
  bool no_random_move{false};
  bool no_drug{false};
  bool no_position_move{false};
  std::int32_t need_level{0};
  std::int32_t mine_map{0};
  std::string back_map{};
  std::vector<MapGateConfig> gates{};
  bool quiz_zone{false};
  std::int32_t need_set_number{-1};
  std::int32_t need_set_value{-1};
  std::optional<MapEntryQuestConfig> check_quest{};
};

struct MapEntryRuleConfig {
  std::string map_id{};
  std::filesystem::path source_map{};
  std::int32_t width{0};
  std::int32_t height{0};
  std::int32_t need_level{0};
  bool need_hole{false};
  std::int32_t need_set_number{-1};
  std::int32_t need_set_value{-1};
  std::optional<MapEntryQuestConfig> check_quest{};
};

struct SpawnConfig {
  std::string map_id{};
  std::string actor_type{};
  std::string name{};
  std::int32_t x{0};
  std::int32_t y{0};
  std::uint32_t respawn_ms{0};
  std::int32_t level{1};
  std::int32_t max_hp{12};
  std::int32_t attack_power{3};
  std::int32_t defense{0};
  std::int32_t magic_defense{0};
  std::int32_t exp_reward{12};
  std::int32_t life_attrib{0};
  bool tameable{true};
  std::int32_t area{0};
  std::int32_t count{1};
  std::uint32_t zen_time_ms{0};
  std::int32_t small_zen_rate{0};
  bool legacy_group{false};
};

enum class MonsterAiProfile {
  passive_animal,
  basic,
  aggressive,
  slow,
  ranged,
  stationary
};

struct MonsterDefConfig {
  std::string name{};
  std::int32_t race_server{0};
  std::int32_t race_image{0};
  std::int32_t appearance{0};
  std::int32_t level{1};
  bool undead{false};
  bool tameable{true};
  std::int32_t cool_eye{0};
  std::int32_t exp{12};
  std::int32_t hp{12};
  std::int32_t mp{0};
  std::int32_t ac{0};
  std::int32_t mac{0};
  std::int32_t dc{3};
  std::int32_t dc_max{0};
  std::int32_t mc{0};
  std::int32_t sc{0};
  std::int32_t agility{0};
  std::int32_t accurate{0};
  std::int32_t walk_speed_ms{200};
  std::int32_t walk_step{1};
  std::int32_t walk_wait_ms{0};
  std::int32_t attack_speed_ms{200};
  MonsterAiProfile ai_profile{MonsterAiProfile::basic};
};

struct MonsterDropConfig {
  std::string monster_name{};
  std::int32_t sel_point{0};
  std::int32_t max_point{0};
  std::string item_name{};
  std::int32_t count{1};
};

struct ItemConfig {
  std::int32_t id{0};
  std::string name{};
  std::int32_t weight{0};
  std::int32_t price{0};
  std::int32_t std_mode{0};
  std::int32_t shape{0};
  std::int32_t looks{0};
  std::int32_t dura_max{0};
  std::int32_t equip_slot{-1};
  std::int32_t hp_add{0};
  std::int32_t mp_add{0};
  std::int32_t need{0};
  std::int32_t need_level{0};
  std::int32_t job{-1};
  std::int32_t sex{-1};
  std::int32_t stock{0};
  std::int32_t item_desc{0};
  std::int32_t special_pwr{0};
  std::uint16_t ac{0};
  std::uint16_t mac{0};
  std::uint16_t dc{0};
  std::uint16_t mc{0};
  std::uint16_t sc{0};
  std::int32_t accurate{0};
  std::int32_t agility{0};
  std::int32_t atk_spd{0};
  std::int32_t mg_avoid{0};
  std::int32_t strong{0};
  std::int32_t undead{0};
  std::int32_t exp_add{0};
  std::int32_t eff_type1{0};
  std::int32_t eff_rate1{0};
  std::int32_t eff_value1{0};
  std::int32_t eff_type2{0};
  std::int32_t eff_rate2{0};
  std::int32_t eff_value2{0};
  std::string scroll_kind{};
  std::string unbind_item{};
  std::int32_t unbind_count{0};
  std::int32_t ani_count{0};
};

struct LegacyMagicDefinition {
  bool legacy_present{false};
  std::int32_t effect_type{0};
  std::int32_t effect{0};
  std::int32_t spell{0};
  std::int32_t min_power{0};
  std::int32_t max_power{0};
  std::int32_t job{0};
  std::array<std::int32_t, 4> need_level{0, 0, 0, 0};
  std::array<std::int32_t, 4> max_train{0, 0, 0, 0};
  std::int32_t max_train_level{0};
  std::int32_t delay_time{0};
  std::int32_t def_spell{0};
  std::int32_t def_min_power{0};
  std::int32_t def_max_power{0};
  std::string desc{};
  bool is_sword_skill{false};
};

struct MagicConfig {
  std::int32_t id{0};
  std::string name{};
  std::int32_t mp_cost{0};
  std::int32_t power{0};
  std::int32_t radius{0};
  bool affect_players{false};
  bool affect_monsters{true};
  std::int32_t instant_heal{0};
  std::int32_t heal_per_tick{0};
  bool dispel_negative{false};
  std::int32_t dot_damage{0};
  std::uint32_t effect_duration_ms{0};
  std::uint32_t effect_tick_ms{0};
  std::int32_t slow_percent{0};
  std::int32_t shield_amount{0};
  LegacyMagicDefinition legacy{};
};

struct MerchantProductConfig {
  std::string item_name{};
  std::int32_t count{0};
  std::int32_t refresh_hours{0};
};

struct CastleDialogContext {
  std::string castle_name{};
  std::string owner_guild{};
  std::string lord{};
  std::string castle_war_date{};
  std::string list_of_war{};
  std::string no_active_wars_text{};
  std::string unclaimed_owner_label{};
  std::string unclaimed_lord_label{};
  std::string owner_role_label{};
  std::string owner_guild_role_label{};
  std::string challenger_role_label{};
  std::string rival_role_label{};
  std::string unknown_role_label{};
  std::string war_entry_listed_label{};
  std::string war_entry_unlisted_label{};
  std::string war_status_active_label{};
  std::string war_status_available_label{};
  std::string role_change_owner_label{};
  std::string role_change_challenger_label{};
  std::string claim_summary_template{};
  std::string war_summary_template{};
  std::string claim_require_guild_template{};
  std::string claim_missing_guild_template{};
  std::string claim_only_lord_template{};
  std::string war_require_guild_template{};
  std::string war_missing_guild_template{};
  std::string war_only_lord_template{};
  std::string war_usage_template{};
  std::string war_self_target_template{};
  std::string war_target_missing_template{};
  std::string war_already_registered_template{};
  std::string war_need_gold_template{};
  std::string guild_create_summary_template{};
  std::string guild_apply_summary_template{};
  std::string guild_withdraw_summary_template{};
  std::string guild_approve_summary_template{};
  std::string guild_reject_summary_template{};
  std::string guild_kick_summary_template{};
  std::string guild_title_summary_template{};
  std::string guild_transfer_summary_template{};
  std::string guild_leave_summary_template{};
  std::string guild_leave_transfer_summary_template{};
  std::string guild_disband_summary_template{};
  std::string guild_membership_cleared_summary_template{};
  std::string guild_apply_alert_template{};
  std::string guild_withdraw_alert_template{};
  std::string guild_approved_notice_template{};
  std::string guild_rejected_notice_template{};
  std::string guild_removed_notice_template{};
  std::string guild_new_lord_notice_template{};
  std::string guild_title_changed_notice_template{};
  std::string guild_create_leave_current_template{};
  std::string guild_create_choose_name_template{};
  std::string guild_create_name_unavailable_template{};
  std::string guild_create_need_gold_template{};
  std::string guild_apply_leave_current_template{};
  std::string guild_apply_choose_guild_template{};
  std::string guild_not_found_template{};
  std::string guild_apply_already_pending_template{};
  std::int32_t guild_war_fee{0};
  std::int32_t upgrade_weapon_fee{0};
  std::int32_t guild_create_fee{0};
};

struct GuildState {
  std::string guild_name{};
  std::string lord{};
  std::vector<std::string> members{};
  std::vector<std::string> applicants{};
};

struct GuildCastleSnapshot {
  CastleDialogContext castle_dialog{};
  std::vector<GuildState> guilds{};
};

struct NpcConfig {
  std::string id{};
  std::string map_id{};
  std::string name{};
  std::int32_t x{0};
  std::int32_t y{0};
  std::string script{};
  std::string service{};
  std::vector<std::int32_t> merchant_goods{};
  std::vector<NpcDialogSectionConfig> dialog_sections{};
  std::int32_t price_rate_percent{100};
  std::vector<std::int32_t> legacy_deal_std_modes{};
  std::vector<MerchantProductConfig> merchant_products{};
};

struct MapQuestConfig {
  std::string map_id{};
  std::int32_t set_number{0};
  std::int32_t value{0};
  std::string monster_name{};
  std::string item_name{};
  std::string qfile{};
  bool enable_group{false};
  std::vector<NpcDialogSectionConfig> dialog_sections{};
};

struct HostConfig {
  RuntimeConfig runtime{};
  PortConfig ports{};
  LogicBudgetConfig budgets{};
  std::vector<MapConfig> maps{};
  std::vector<SpawnConfig> spawns{};
  std::vector<MonsterDefConfig> monsters{};
  std::vector<MonsterDropConfig> monster_drops{};
  std::vector<ItemConfig> items{};
  std::vector<MagicConfig> magics{};
  std::vector<NpcConfig> npcs{};
  std::vector<MapQuestConfig> map_quests{};
};

}  // namespace mir2
