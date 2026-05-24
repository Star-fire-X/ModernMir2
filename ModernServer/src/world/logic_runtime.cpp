#include "world/logic_runtime.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cctype>
#include <iterator>
#include <limits>
#include <unordered_map>

#include "spdlog/spdlog.h"
#include "util/string_utils.hpp"
#include "world/legacy_chat_parser.hpp"
#include "world/legacy_gm_commands.hpp"
#include "world/legacy_item_rules.hpp"

namespace mir2 {

namespace {

LegacyUserItem make_merchant_item(const ItemConfig& item_config, std::int32_t make_index) {
  LegacyUserItem item;
  item.index = static_cast<std::uint16_t>(std::clamp(item_config.id, 0, 65535));
  item.make_index = make_index;
  item.dura =
      static_cast<std::uint16_t>(std::clamp(item_config.dura_max > 0 ? item_config.dura_max : 1000, 0, 65535));
  item.dura_max = item.dura;
  return item;
}

std::string normalized_key(std::string_view value) {
  return util::lower_copy(util::trim(std::string(value)));
}

std::string legacy_character_key(std::string_view value) {
  return util::lower_copy(std::string(value));
}

bool legacy_ascii_equals_ci(std::string_view lhs, std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    const auto left = static_cast<unsigned char>(lhs[index]);
    const auto right = static_cast<unsigned char>(rhs[index]);
    if (std::tolower(left) != std::tolower(right)) {
      return false;
    }
  }
  return true;
}

bool elapsed_gt(std::uint64_t now_ms, std::uint64_t start_ms, std::uint64_t interval_ms) {
  return now_ms - start_ms > interval_ms;
}

bool legacy_command_equals(std::string_view command,
                           std::string_view utf8,
                           std::string_view gbk) {
  return command == utf8 || command == gbk;
}

std::optional<std::int32_t> parse_legacy_int32(std::string_view text) {
  if (text.empty()) {
    return std::nullopt;
  }
  std::int32_t value = 0;
  const auto* begin = text.data();
  const auto* end = text.data() + text.size();
  const auto [ptr, error] = std::from_chars(begin, end, value);
  if (error != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  return value;
}

std::string legacy_minutes_text(std::uint64_t expire_ms, std::uint64_t now_ms) {
  if (expire_ms <= now_ms) {
    return "0";
  }
  return std::to_string((expire_ms - now_ms) / (60ULL * 1000ULL));
}

std::string join_legacy_args(const std::vector<std::string>& args, std::size_t begin,
                             std::size_t end) {
  std::string result;
  for (auto index = begin; index < end && index < args.size(); ++index) {
    if (!result.empty()) {
      result += ' ';
    }
    result += args[index];
  }
  return result;
}

std::pair<std::int32_t, std::int32_t> legacy_direction_delta(std::uint8_t dir) {
  switch (dir % 8) {
    case 0:
      return {0, -1};
    case 1:
      return {1, -1};
    case 2:
      return {1, 0};
    case 3:
      return {1, 1};
    case 4:
      return {0, 1};
    case 5:
      return {-1, 1};
    case 6:
      return {-1, 0};
    case 7:
      return {-1, -1};
  }
  return {0, 1};
}

const ItemConfig* find_item_config_by_name(
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs, std::string_view name) {
  const auto wanted = normalized_key(name);
  for (const auto& [_, item_config] : item_configs) {
    if (normalized_key(item_config.name) == wanted) {
      return &item_config;
    }
  }
  return nullptr;
}

const GuildState* find_runtime_guild_state(const GuildCastleSnapshot& snapshot,
                                           std::string_view guild_name) {
  const auto wanted = util::lower_copy(std::string(guild_name));
  for (const auto& guild : snapshot.guilds) {
    if (util::lower_copy(guild.guild_name) == wanted) {
      return &guild;
    }
  }
  return nullptr;
}

bool dialog_has_weapon_upgrade_link(const std::vector<NpcDialogSectionConfig>& sections) {
  for (const auto& section : sections) {
    const auto action = util::lower_copy(section.action);
    if (action == "@upgradenow" || action == "@getbackupgnow") {
      return true;
    }
    const auto text = util::lower_copy(section.text);
    if (text.find("@upgradenow") != std::string::npos ||
        text.find("@getbackupgnow") != std::string::npos) {
      return true;
    }
  }
  return false;
}

MonsterAiProfile infer_monster_ai_profile(const MonsterDefConfig& def) {
  switch (def.race_server) {
    case 50:
    case 52:
      return MonsterAiProfile::passive_animal;
    case 85:
    case 103:
    case 110:
    case 111:
    case 112:
    case 116:
    case 121:
      return MonsterAiProfile::stationary;
    case 82:
    case 87:
    case 90:
    case 91:
    case 93:
    case 94:
    case 95:
    case 96:
    case 100:
    case 101:
    case 102:
    case 104:
    case 105:
    case 106:
    case 107:
    case 113:
    case 114:
    case 115:
    case 117:
    case 118:
    case 119:
    case 122:
    case 123:
    case 124:
    case 125:
    case 126:
    case 127:
    case 128:
    case 129:
    case 130:
      return MonsterAiProfile::ranged;
    case 53:
    case 80:
    case 81:
    case 86:
    case 88:
    case 89:
    case 97:
    case 108:
      return MonsterAiProfile::aggressive;
    default:
      break;
  }
  const auto name = normalized_key(def.name);
  if (name.find("deer") != std::string::npos || name.find("chicken") != std::string::npos ||
      name.find("hen") != std::string::npos || name.find("pig") != std::string::npos) {
    return MonsterAiProfile::passive_animal;
  }
  if (name.find("archer") != std::string::npos || name.find("bow") != std::string::npos ||
      name.find("spider") != std::string::npos) {
    return MonsterAiProfile::ranged;
  }
  if (name.find("flower") != std::string::npos || name.find("plant") != std::string::npos) {
    return MonsterAiProfile::stationary;
  }
  if (name.find("oma") != std::string::npos || name.find("woma") != std::string::npos ||
      name.find("skeleton") != std::string::npos || name.find("wolf") != std::string::npos ||
      (def.race_server >= 81 && def.race_server <= 89)) {
    return MonsterAiProfile::aggressive;
  }
  if (def.walk_speed_ms >= 1200 || def.walk_wait_ms >= 800) {
    return MonsterAiProfile::slow;
  }
  return def.ai_profile;
}

bool is_legacy_gold_name(std::string_view normalized_name) {
  return normalized_name == "gold";
}

void apply_runtime_castle_defaults(const RuntimeConfig& runtime_config,
                                   CastleDialogContext& castle_dialog_context) {
  if (castle_dialog_context.castle_name.empty()) {
    castle_dialog_context.castle_name = runtime_config.castle_name;
  }
  if (castle_dialog_context.castle_war_date.empty()) {
    castle_dialog_context.castle_war_date = runtime_config.default_castle_war_date;
  }
  if (castle_dialog_context.no_active_wars_text.empty()) {
    castle_dialog_context.no_active_wars_text = runtime_config.no_active_wars_text;
  }
  if (castle_dialog_context.unclaimed_owner_label.empty()) {
    castle_dialog_context.unclaimed_owner_label = runtime_config.unclaimed_castle_owner;
  }
  if (castle_dialog_context.unclaimed_lord_label.empty()) {
    castle_dialog_context.unclaimed_lord_label = runtime_config.unclaimed_castle_lord;
  }
  if (castle_dialog_context.owner_role_label.empty()) {
    castle_dialog_context.owner_role_label = runtime_config.castle_owner_role_label;
  }
  if (castle_dialog_context.owner_guild_role_label.empty()) {
    castle_dialog_context.owner_guild_role_label = runtime_config.castle_owner_guild_role_label;
  }
  if (castle_dialog_context.challenger_role_label.empty()) {
    castle_dialog_context.challenger_role_label = runtime_config.castle_challenger_role_label;
  }
  if (castle_dialog_context.rival_role_label.empty()) {
    castle_dialog_context.rival_role_label = runtime_config.castle_rival_role_label;
  }
  if (castle_dialog_context.unknown_role_label.empty()) {
    castle_dialog_context.unknown_role_label = runtime_config.castle_unknown_role_label;
  }
  if (castle_dialog_context.war_entry_listed_label.empty()) {
    castle_dialog_context.war_entry_listed_label = runtime_config.castle_war_entry_listed_label;
  }
  if (castle_dialog_context.war_entry_unlisted_label.empty()) {
    castle_dialog_context.war_entry_unlisted_label = runtime_config.castle_war_entry_unlisted_label;
  }
  if (castle_dialog_context.war_status_active_label.empty()) {
    castle_dialog_context.war_status_active_label = runtime_config.castle_war_status_active_label;
  }
  if (castle_dialog_context.war_status_available_label.empty()) {
    castle_dialog_context.war_status_available_label =
        runtime_config.castle_war_status_available_label;
  }
  if (castle_dialog_context.role_change_owner_label.empty()) {
    castle_dialog_context.role_change_owner_label = runtime_config.castle_role_change_owner_label;
  }
  if (castle_dialog_context.role_change_challenger_label.empty()) {
    castle_dialog_context.role_change_challenger_label =
        runtime_config.castle_role_change_challenger_label;
  }
  if (castle_dialog_context.claim_summary_template.empty()) {
    castle_dialog_context.claim_summary_template = runtime_config.castle_claim_summary_template;
  }
  if (castle_dialog_context.war_summary_template.empty()) {
    castle_dialog_context.war_summary_template = runtime_config.castle_war_summary_template;
  }
  if (castle_dialog_context.claim_require_guild_template.empty()) {
    castle_dialog_context.claim_require_guild_template =
        runtime_config.castle_claim_require_guild_template;
  }
  if (castle_dialog_context.claim_missing_guild_template.empty()) {
    castle_dialog_context.claim_missing_guild_template =
        runtime_config.castle_claim_missing_guild_template;
  }
  if (castle_dialog_context.claim_only_lord_template.empty()) {
    castle_dialog_context.claim_only_lord_template =
        runtime_config.castle_claim_only_lord_template;
  }
  if (castle_dialog_context.war_require_guild_template.empty()) {
    castle_dialog_context.war_require_guild_template =
        runtime_config.castle_war_require_guild_template;
  }
  if (castle_dialog_context.war_missing_guild_template.empty()) {
    castle_dialog_context.war_missing_guild_template =
        runtime_config.castle_war_missing_guild_template;
  }
  if (castle_dialog_context.war_only_lord_template.empty()) {
    castle_dialog_context.war_only_lord_template =
        runtime_config.castle_war_only_lord_template;
  }
  if (castle_dialog_context.war_usage_template.empty()) {
    castle_dialog_context.war_usage_template = runtime_config.castle_war_usage_template;
  }
  if (castle_dialog_context.war_self_target_template.empty()) {
    castle_dialog_context.war_self_target_template =
        runtime_config.castle_war_self_target_template;
  }
  if (castle_dialog_context.war_target_missing_template.empty()) {
    castle_dialog_context.war_target_missing_template =
        runtime_config.castle_war_target_missing_template;
  }
  if (castle_dialog_context.war_already_registered_template.empty()) {
    castle_dialog_context.war_already_registered_template =
        runtime_config.castle_war_already_registered_template;
  }
  if (castle_dialog_context.war_need_gold_template.empty()) {
    castle_dialog_context.war_need_gold_template = runtime_config.castle_war_need_gold_template;
  }
  if (castle_dialog_context.guild_create_summary_template.empty()) {
    castle_dialog_context.guild_create_summary_template =
        runtime_config.guild_create_summary_template;
  }
  if (castle_dialog_context.guild_apply_summary_template.empty()) {
    castle_dialog_context.guild_apply_summary_template =
        runtime_config.guild_apply_summary_template;
  }
  if (castle_dialog_context.guild_withdraw_summary_template.empty()) {
    castle_dialog_context.guild_withdraw_summary_template =
        runtime_config.guild_withdraw_summary_template;
  }
  if (castle_dialog_context.guild_approve_summary_template.empty()) {
    castle_dialog_context.guild_approve_summary_template =
        runtime_config.guild_approve_summary_template;
  }
  if (castle_dialog_context.guild_reject_summary_template.empty()) {
    castle_dialog_context.guild_reject_summary_template =
        runtime_config.guild_reject_summary_template;
  }
  if (castle_dialog_context.guild_kick_summary_template.empty()) {
    castle_dialog_context.guild_kick_summary_template =
        runtime_config.guild_kick_summary_template;
  }
  if (castle_dialog_context.guild_title_summary_template.empty()) {
    castle_dialog_context.guild_title_summary_template =
        runtime_config.guild_title_summary_template;
  }
  if (castle_dialog_context.guild_transfer_summary_template.empty()) {
    castle_dialog_context.guild_transfer_summary_template =
        runtime_config.guild_transfer_summary_template;
  }
  if (castle_dialog_context.guild_leave_summary_template.empty()) {
    castle_dialog_context.guild_leave_summary_template =
        runtime_config.guild_leave_summary_template;
  }
  if (castle_dialog_context.guild_leave_transfer_summary_template.empty()) {
    castle_dialog_context.guild_leave_transfer_summary_template =
        runtime_config.guild_leave_transfer_summary_template;
  }
  if (castle_dialog_context.guild_disband_summary_template.empty()) {
    castle_dialog_context.guild_disband_summary_template =
        runtime_config.guild_disband_summary_template;
  }
  if (castle_dialog_context.guild_membership_cleared_summary_template.empty()) {
    castle_dialog_context.guild_membership_cleared_summary_template =
        runtime_config.guild_membership_cleared_summary_template;
  }
  if (castle_dialog_context.guild_apply_alert_template.empty()) {
    castle_dialog_context.guild_apply_alert_template = runtime_config.guild_apply_alert_template;
  }
  if (castle_dialog_context.guild_withdraw_alert_template.empty()) {
    castle_dialog_context.guild_withdraw_alert_template =
        runtime_config.guild_withdraw_alert_template;
  }
  if (castle_dialog_context.guild_approved_notice_template.empty()) {
    castle_dialog_context.guild_approved_notice_template =
        runtime_config.guild_approved_notice_template;
  }
  if (castle_dialog_context.guild_rejected_notice_template.empty()) {
    castle_dialog_context.guild_rejected_notice_template =
        runtime_config.guild_rejected_notice_template;
  }
  if (castle_dialog_context.guild_removed_notice_template.empty()) {
    castle_dialog_context.guild_removed_notice_template =
        runtime_config.guild_removed_notice_template;
  }
  if (castle_dialog_context.guild_new_lord_notice_template.empty()) {
    castle_dialog_context.guild_new_lord_notice_template =
        runtime_config.guild_new_lord_notice_template;
  }
  if (castle_dialog_context.guild_title_changed_notice_template.empty()) {
    castle_dialog_context.guild_title_changed_notice_template =
        runtime_config.guild_title_changed_notice_template;
  }
  if (castle_dialog_context.guild_create_leave_current_template.empty()) {
    castle_dialog_context.guild_create_leave_current_template =
        runtime_config.guild_create_leave_current_template;
  }
  if (castle_dialog_context.guild_create_choose_name_template.empty()) {
    castle_dialog_context.guild_create_choose_name_template =
        runtime_config.guild_create_choose_name_template;
  }
  if (castle_dialog_context.guild_create_name_unavailable_template.empty()) {
    castle_dialog_context.guild_create_name_unavailable_template =
        runtime_config.guild_create_name_unavailable_template;
  }
  if (castle_dialog_context.guild_create_need_gold_template.empty()) {
    castle_dialog_context.guild_create_need_gold_template =
        runtime_config.guild_create_need_gold_template;
  }
  if (castle_dialog_context.guild_apply_leave_current_template.empty()) {
    castle_dialog_context.guild_apply_leave_current_template =
        runtime_config.guild_apply_leave_current_template;
  }
  if (castle_dialog_context.guild_apply_choose_guild_template.empty()) {
    castle_dialog_context.guild_apply_choose_guild_template =
        runtime_config.guild_apply_choose_guild_template;
  }
  if (castle_dialog_context.guild_not_found_template.empty()) {
    castle_dialog_context.guild_not_found_template = runtime_config.guild_not_found_template;
  }
  if (castle_dialog_context.guild_apply_already_pending_template.empty()) {
    castle_dialog_context.guild_apply_already_pending_template =
        runtime_config.guild_apply_already_pending_template;
  }
  const auto owner_text = util::lower_copy(util::trim(castle_dialog_context.owner_guild));
  if (owner_text.empty() || owner_text == "none" || owner_text == "unclaimed" ||
      owner_text == "-" ||
      owner_text == util::lower_copy(castle_dialog_context.unclaimed_owner_label)) {
    castle_dialog_context.owner_guild.clear();
  }
  const auto lord_text = util::lower_copy(util::trim(castle_dialog_context.lord));
  if (castle_dialog_context.owner_guild.empty() || lord_text.empty() || lord_text == "none" ||
      lord_text == "unclaimed" || lord_text == "-" ||
      lord_text == util::lower_copy(castle_dialog_context.unclaimed_lord_label)) {
    castle_dialog_context.lord.clear();
  }
  const auto wars_text = util::trim(castle_dialog_context.list_of_war);
  if (wars_text.empty() ||
      util::lower_copy(wars_text) == "no active wars." ||
      util::lower_copy(wars_text) == util::lower_copy(castle_dialog_context.no_active_wars_text)) {
    castle_dialog_context.list_of_war.clear();
  }
  if (castle_dialog_context.guild_war_fee <= 0) {
    castle_dialog_context.guild_war_fee = runtime_config.guild_war_fee;
  }
  if (castle_dialog_context.upgrade_weapon_fee <= 0) {
    castle_dialog_context.upgrade_weapon_fee = runtime_config.upgrade_weapon_fee;
  }
  if (castle_dialog_context.guild_create_fee <= 0) {
    castle_dialog_context.guild_create_fee = runtime_config.guild_create_fee;
  }
}

void apply_runtime_castle_defaults(const RuntimeConfig& runtime_config,
                                   GuildCastleSnapshot& guild_castle_snapshot) {
  apply_runtime_castle_defaults(runtime_config, guild_castle_snapshot.castle_dialog);
}

}  // namespace

LogicRuntime::LogicRuntime(HostConfig config) : config_(std::move(config)) {}

void LogicRuntime::set_legacy_random_seed(std::uint32_t seed) { legacy_random_.seed(seed); }

std::uint32_t LogicRuntime::legacy_random_state() const { return legacy_random_.state(); }

void LogicRuntime::initialize() {
  if (config_.runtime.legacy_random_seed.has_value()) {
    legacy_random_.seed(*config_.runtime.legacy_random_seed);
  } else {
    const auto seed = static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    legacy_random_.seed(seed);
  }

  maps_.clear();
  map_order_.clear();
  item_configs_.clear();
  magic_configs_.clear();
  monster_defs_.clear();
  monster_drops_.clear();
  item_configs_by_name_.clear();
  legacy_shut_up_list_.clear();
  legacy_admin_degrees_ = load_legacy_admin_list(config_.runtime.legacy_admin_list);
  monster_groups_.clear();
  merchant_order_.clear();
  npc_order_.clear();
  mon_cur_ = 0;
  mon_sub_cur_ = 0;
  gen_cur_ = 0;
  mer_cur_ = 0;
  npc_cur_ = 0;
  make_index_allocator_.reset();
  for (const auto& [_, merchant_state] : merchant_states_) {
    make_index_allocator_.observe(merchant_state);
  }
  one_zen_time_ms_ = 0;
  one_zen_time_initialized_ = false;
  default_map_id_.clear();
  apply_runtime_castle_defaults(config_.runtime, castle_dialog_context_);
  apply_runtime_castle_defaults(config_.runtime, guild_castle_snapshot_);
  castle_dialog_context_ = guild_castle_snapshot_.castle_dialog;

  for (const auto& item : config_.items) {
    item_configs_[item.id] = item;
    if (!item.name.empty()) {
      item_configs_by_name_[util::lower_copy(util::trim(item.name))] = item;
    }
  }
  for (const auto& magic : config_.magics) {
    magic_configs_[magic.id] = magic;
  }
  for (const auto& monster : config_.monsters) {
    if (!monster.name.empty()) {
      monster_defs_[util::lower_copy(util::trim(monster.name))] = monster;
    }
  }
  for (const auto& drop : config_.monster_drops) {
    if (!drop.monster_name.empty()) {
      monster_drops_[util::lower_copy(util::trim(drop.monster_name))].push_back(drop);
    }
  }

  if (!config_.maps.empty()) {
    default_map_id_ = config_.maps.front().id;
  }

  std::unordered_map<std::string, MapEntryRuleConfig> map_entry_rules;
  for (const auto& map : config_.maps) {
    MapEntryRuleConfig rule;
    rule.map_id = map.id;
    rule.source_map = map.source_map;
    rule.width = map.width;
    rule.height = map.height;
    rule.need_level = map.need_level;
    rule.need_hole = map.need_hole;
    rule.need_set_number = map.need_set_number;
    rule.need_set_value = map.need_set_value;
    rule.check_quest = map.check_quest;
    map_entry_rules[map.id] = std::move(rule);
  }

  for (const auto& map : config_.maps) {
    auto [map_it, inserted] = maps_.emplace(
        map.id, std::make_unique<MapActor>(map, config_.budgets, item_configs_, magic_configs_,
                                           config_.map_quests, castle_dialog_context_,
                                           monster_defs_, map_entry_rules,
                                           &make_index_allocator_,
                                           config_.runtime.black_stone_name,
                                           config_.runtime.legacy_approval_mode));
    map_it->second->set_legacy_random(&legacy_random_);
    if (inserted) {
      map_order_.push_back(map.id);
    }
    map_it->second->set_guild_castle_snapshot(guild_castle_snapshot_);
  }

  for (const auto& spawn : config_.spawns) {
    MonsterGroup group;
    group.name = spawn.name;
    group.map_id = resolve_map_id(spawn.map_id);
    group.spawn = spawn;
    group.x = spawn.x;
    group.y = spawn.y;
    group.area = std::max(spawn.area, 0);
    group.count = std::max(spawn.count, 1);
    group.small_zen_rate = std::clamp(spawn.small_zen_rate, 0, 100);
    group.legacy_group = spawn.legacy_group;
    group.respawn_ms = spawn.respawn_ms;
    group.zen_time_ms = spawn.zen_time_ms != 0 ? spawn.zen_time_ms : spawn.respawn_ms;
    if (!group.legacy_group) {
      const auto actor_id = next_actor_id_++;
      auto mail = make_monster_spawn_mail(group, actor_id, spawn.x, spawn.y, 0, nullptr);
      maps_.at(mail.map_id)->enqueue_mail(std::move(mail));
      group.monsters.push_back(ActorRef{group.map_id, actor_id, spawn.name});
    }
    monster_groups_.push_back(std::move(group));
  }

  for (const auto& npc : config_.npcs) {
    ActorMail mail;
    mail.kind = ActorMailKind::spawn_npc;
    mail.map_id = resolve_map_id(npc.map_id);
    mail.actor_id = next_actor_id_++;
    mail.name = npc.name;
    mail.npc_service = npc.service;
    mail.merchant_key = npc.id + "-" + mail.map_id;
    mail.npc_dialog_sections = npc.dialog_sections;
    mail.npc_price_rate_percent = npc.price_rate_percent;
    mail.legacy_deal_std_modes = npc.legacy_deal_std_modes;
    for (const auto item_id : npc.merchant_goods) {
      const auto item_it = item_configs_.find(item_id);
      if (item_it == item_configs_.end()) {
        continue;
      }
      mail.merchant_items.push_back(
          make_merchant_item(item_it->second, make_index_allocator_.allocate()));
    }
    for (const auto& product : npc.merchant_products) {
      const auto* item_config = find_item_config_by_name(item_configs_, product.item_name);
      if (item_config == nullptr) {
        spdlog::warn("Skipping merchant product '{}' for NPC '{}' because the item was not found",
                     product.item_name, npc.id);
        continue;
      }
      mail.merchant_products.push_back(MerchantProductRuntimeConfig{
          item_config->id, item_config->name, std::max(product.count, 0),
          static_cast<std::uint64_t>(std::max(product.refresh_hours, 0)) * 60ULL * 1000ULL, 0});
      for (std::int32_t count = 0; count < product.count; ++count) {
        mail.merchant_items.push_back(
            make_merchant_item(*item_config, make_index_allocator_.allocate()));
      }
    }
    if (const auto state = merchant_states_.find(mail.merchant_key); state != merchant_states_.end()) {
      mail.merchant_items = state->second.goods;
      mail.merchant_prices = state->second.prices;
      mail.weapon_upgrades = state->second.weapon_upgrades;
    }
    mail.x = npc.x;
    mail.y = npc.y;
    const auto actor_id = mail.actor_id;
    const auto map_id = mail.map_id;
    const auto name = mail.name;
    if (is_merchant_npc_config(npc, mail)) {
      merchant_order_.push_back(ActorRef{map_id, actor_id, name});
    } else {
      npc_order_.push_back(ActorRef{map_id, actor_id, name});
    }
    maps_.at(mail.map_id)->enqueue_mail(std::move(mail));
  }
}

void LogicRuntime::set_merchant_states(std::vector<MerchantStateRecord> merchant_states) {
  merchant_states_.clear();
  for (auto& state : merchant_states) {
    if (!state.merchant_key.empty()) {
      make_index_allocator_.observe(state);
      merchant_states_[state.merchant_key] = std::move(state);
    }
  }
}

void LogicRuntime::apply_merchant_states(std::vector<MerchantStateRecord> merchant_states) {
  set_merchant_states(std::move(merchant_states));
  for (const auto& [_, state] : merchant_states_) {
    for (auto& [__, map] : maps_) {
      if (map->apply_merchant_state(state)) {
        break;
      }
    }
  }
}

void LogicRuntime::set_castle_dialog_context(CastleDialogContext castle_dialog_context) {
  apply_runtime_castle_defaults(config_.runtime, castle_dialog_context);
  castle_dialog_context_ = std::move(castle_dialog_context);
  guild_castle_snapshot_.castle_dialog = castle_dialog_context_;
  for (const auto& map_id : map_order_) {
    if (auto map_it = maps_.find(map_id); map_it != maps_.end()) {
      map_it->second->set_castle_dialog_context(castle_dialog_context_);
    }
  }
}

void LogicRuntime::set_guild_castle_snapshot(GuildCastleSnapshot guild_castle_snapshot) {
  apply_runtime_castle_defaults(config_.runtime, guild_castle_snapshot);
  guild_castle_snapshot_ = std::move(guild_castle_snapshot);
  castle_dialog_context_ = guild_castle_snapshot_.castle_dialog;
  for (const auto& map_id : map_order_) {
    if (auto map_it = maps_.find(map_id); map_it != maps_.end()) {
      map_it->second->set_guild_castle_snapshot(guild_castle_snapshot_);
    }
  }
}

void LogicRuntime::add_legacy_shut_up(std::string_view character_name,
                                      std::uint64_t duration_ms,
                                      std::uint64_t now_ms) {
  const auto key = legacy_character_key(character_name);
  if (key.empty()) {
    return;
  }
  auto& entry = legacy_shut_up_list_[key];
  if (entry.character_name.empty() || now_ms > entry.expire_ms) {
    entry.character_name = std::string(character_name);
    entry.expire_ms = now_ms + duration_ms;
  } else {
    entry.expire_ms += duration_ms;
  }
}

bool LogicRuntime::release_legacy_shut_up(std::string_view character_name) {
  return legacy_shut_up_list_.erase(legacy_character_key(character_name)) != 0;
}

std::vector<LegacyShutUpEntry> LogicRuntime::legacy_shut_up_entries() const {
  std::vector<LegacyShutUpEntry> entries;
  entries.reserve(legacy_shut_up_list_.size());
  for (const auto& [_, entry] : legacy_shut_up_list_) {
    entries.push_back(entry);
  }
  return entries;
}

bool LogicRuntime::has_live_or_closing_character(std::string_view character_name) const {
  const auto key = util::lower_copy(std::string(character_name));
  if (key.empty()) {
    return false;
  }
  if (const auto close_it = close_records_.find(key);
      close_it != close_records_.end() && close_it->second.reason != "closed") {
    return true;
  }
  for (const auto& [_, locator] : session_index_) {
    if (util::lower_copy(locator.character_name) == key) {
      return true;
    }
  }
  return false;
}

LegacyUserDegree LogicRuntime::resolve_legacy_user_degree(
    std::string_view account_id, std::string_view character_name) const {
  const auto admin_it = legacy_admin_degrees_.find(legacy_character_key(character_name));
  if (admin_it != legacy_admin_degrees_.end()) {
    return admin_it->second;
  }
  return legacy_heuristic_user_degree(account_id);
}

std::optional<std::uint64_t> LogicRuntime::find_actor_session_by_name(
    std::string_view character_name) const {
  const auto key = legacy_character_key(character_name);
  if (key.empty()) {
    return std::nullopt;
  }
  for (const auto& [session_id, locator] : session_index_) {
    if (legacy_character_key(locator.character_name) == key) {
      return session_id;
    }
  }
  return std::nullopt;
}

void LogicRuntime::remove_legacy_group_member(std::uint64_t session_id) {
  auto locator_it = session_index_.find(session_id);
  if (locator_it == session_index_.end()) {
    return;
  }
  const auto group_id = locator_it->second.legacy_group_id;
  locator_it->second.legacy_group_id = 0;
  if (group_id == 0) {
    return;
  }
  auto group_it = legacy_groups_.find(group_id);
  if (group_it == legacy_groups_.end()) {
    return;
  }
  auto& members = group_it->second.members;
  members.erase(std::remove(members.begin(), members.end(), session_id), members.end());
  if (members.size() >= 2) {
    return;
  }
  const auto remaining_members = members;
  for (const auto member_session_id : remaining_members) {
    if (auto member_it = session_index_.find(member_session_id);
        member_it != session_index_.end()) {
      member_it->second.legacy_group_id = 0;
    }
  }
  legacy_groups_.erase(group_it);
}

void LogicRuntime::create_legacy_group(std::uint64_t owner_session_id,
                                       std::string_view target_name) {
  auto owner_it = session_index_.find(owner_session_id);
  const auto target_session_id = find_actor_session_by_name(target_name);
  if (owner_it == session_index_.end() || !target_session_id.has_value() ||
      *target_session_id == owner_session_id) {
    return;
  }
  auto target_it = session_index_.find(*target_session_id);
  if (target_it == session_index_.end() || owner_it->second.legacy_group_id != 0 ||
      target_it->second.legacy_group_id != 0) {
    return;
  }

  const auto group_id = next_legacy_group_id_++;
  legacy_groups_[group_id].members = {owner_session_id, *target_session_id};
  owner_it->second.legacy_group_id = group_id;
  target_it->second.legacy_group_id = group_id;
}

void LogicRuntime::add_legacy_group_member(std::uint64_t owner_session_id,
                                           std::string_view target_name) {
  auto owner_it = session_index_.find(owner_session_id);
  const auto target_session_id = find_actor_session_by_name(target_name);
  if (owner_it == session_index_.end() || owner_it->second.legacy_group_id == 0 ||
      !target_session_id.has_value() || *target_session_id == owner_session_id) {
    return;
  }
  auto group_it = legacy_groups_.find(owner_it->second.legacy_group_id);
  auto target_it = session_index_.find(*target_session_id);
  if (group_it == legacy_groups_.end() || target_it == session_index_.end() ||
      target_it->second.legacy_group_id != 0) {
    return;
  }
  group_it->second.members.push_back(*target_session_id);
  target_it->second.legacy_group_id = owner_it->second.legacy_group_id;
}

void LogicRuntime::remove_legacy_group_member_by_name(std::uint64_t owner_session_id,
                                                      std::string_view target_name) {
  auto owner_it = session_index_.find(owner_session_id);
  const auto target_session_id = find_actor_session_by_name(target_name);
  if (owner_it == session_index_.end() || owner_it->second.legacy_group_id == 0 ||
      !target_session_id.has_value()) {
    return;
  }
  auto target_it = session_index_.find(*target_session_id);
  if (target_it == session_index_.end() ||
      target_it->second.legacy_group_id != owner_it->second.legacy_group_id) {
    return;
  }
  remove_legacy_group_member(*target_session_id);
}

ActorMail LogicRuntime::make_player_mail(const LogicCommand& command,
                                         const ActorLocator& locator) const {
  ActorMail mail;
  mail.map_id = locator.map_id;
  mail.actor_id = locator.actor_id;
  mail.session_id = command.session_id;
  mail.session_seq = command.session_seq;
  mail.x = command.x;
  mail.y = command.y;
  mail.dir = command.dir;
  mail.target_actor_id = command.target_actor_id;
  mail.item_make_index = command.item_make_index;
  mail.item_slot = command.item_slot;
  mail.amount = command.amount;
  mail.game_message = command.game_message;
  mail.payload = command.text;
  switch (command.kind) {
    case LogicCommandKind::turn:
      mail.kind = ActorMailKind::turn;
      break;
    case LogicCommandKind::walk:
      mail.kind = ActorMailKind::move;
      break;
    case LogicCommandKind::run:
      mail.kind = ActorMailKind::run;
      break;
    case LogicCommandKind::attack:
      mail.kind = ActorMailKind::attack;
      break;
    case LogicCommandKind::spell:
      mail.kind = ActorMailKind::spell;
      break;
    case LogicCommandKind::say:
      mail.kind = ActorMailKind::say;
      break;
    case LogicCommandKind::click_npc:
      mail.kind = ActorMailKind::click_npc;
      break;
    case LogicCommandKind::merchant_select:
      mail.kind = ActorMailKind::merchant_select;
      break;
    case LogicCommandKind::query_username:
      mail.kind = ActorMailKind::query_username;
      break;
    case LogicCommandKind::query_bag_items:
      mail.kind = ActorMailKind::query_bag_items;
      break;
    case LogicCommandKind::query_storage_items:
      mail.kind = ActorMailKind::query_storage_items;
      break;
    case LogicCommandKind::query_detail_goods:
      mail.kind = ActorMailKind::query_detail_goods;
      break;
    case LogicCommandKind::query_sell_price:
      mail.kind = ActorMailKind::query_sell_price;
      break;
    case LogicCommandKind::query_repair_cost:
      mail.kind = ActorMailKind::query_repair_cost;
      break;
    case LogicCommandKind::drop_item:
      mail.kind = ActorMailKind::drop_item;
      break;
    case LogicCommandKind::pickup_item:
      mail.kind = ActorMailKind::pickup_item;
      break;
    case LogicCommandKind::take_on_item:
      mail.kind = ActorMailKind::take_on_item;
      break;
    case LogicCommandKind::take_off_item:
      mail.kind = ActorMailKind::take_off_item;
      break;
    case LogicCommandKind::eat_item:
      mail.kind = ActorMailKind::eat_item;
      break;
    case LogicCommandKind::drop_gold:
      mail.kind = ActorMailKind::drop_gold;
      break;
    case LogicCommandKind::revive:
      mail.kind = ActorMailKind::revive;
      break;
    case LogicCommandKind::buy_item:
      mail.kind = ActorMailKind::buy_item;
      break;
    case LogicCommandKind::sell_item:
      mail.kind = ActorMailKind::sell_item;
      break;
    case LogicCommandKind::repair_item:
      mail.kind = ActorMailKind::repair_item;
      break;
    case LogicCommandKind::storage_item:
      mail.kind = ActorMailKind::storage_item;
      break;
    case LogicCommandKind::take_back_storage_item:
      mail.kind = ActorMailKind::take_back_storage_item;
      break;
    case LogicCommandKind::trade_try:
      mail.kind = ActorMailKind::trade_try;
      break;
    case LogicCommandKind::trade_cancel:
      mail.kind = ActorMailKind::trade_cancel;
      break;
    case LogicCommandKind::trade_add_item:
      mail.kind = ActorMailKind::trade_add_item;
      break;
    case LogicCommandKind::trade_remove_item:
      mail.kind = ActorMailKind::trade_remove_item;
      break;
    case LogicCommandKind::trade_set_gold:
      mail.kind = ActorMailKind::trade_set_gold;
      break;
    case LogicCommandKind::trade_accept:
      mail.kind = ActorMailKind::trade_accept;
      break;
    default:
      break;
  }
  return mail;
}

bool LogicRuntime::handle_legacy_chat_command(const LogicCommand& command,
                                              ActorLocator& locator,
                                              const LegacyChatParseResult& parsed,
                                              std::uint64_t now_ms,
                                              RuntimeDispatch& dispatch) {
  auto queue_delivery = [&](const ActorLocator& target, LegacyChatDeliveryKind kind,
                            std::string payload, std::uint64_t recog_actor_id) {
    ActorMail mail;
    mail.kind = ActorMailKind::legacy_chat_delivery;
    mail.map_id = target.map_id;
    mail.actor_id = target.actor_id;
    mail.session_id = command.session_id;
    mail.session_seq = command.session_seq;
    mail.target_actor_id = recog_actor_id;
    mail.legacy_chat_kind = kind;
    mail.payload = std::move(payload);
    append_dispatch(dispatch, route_actor_mail(mail));
  };
  auto queue_system_to = [&](const ActorLocator& target, std::string payload) {
    queue_delivery(target, LegacyChatDeliveryKind::system, std::move(payload),
                   target.actor_id);
  };
  auto queue_system = [&](std::string payload) {
    queue_system_to(locator, std::move(payload));
  };
  auto find_locator = [&](std::string_view character_name) -> ActorLocator* {
    const auto key = legacy_character_key(character_name);
    for (auto& [_, candidate] : session_index_) {
      if (legacy_character_key(candidate.character_name) == key) {
        return &candidate;
      }
    }
    return nullptr;
  };
  auto find_session_id = [&](std::string_view character_name) -> std::optional<std::uint64_t> {
    const auto key = legacy_character_key(character_name);
    for (const auto& [session_id, candidate] : session_index_) {
      if (legacy_character_key(candidate.character_name) == key) {
        return session_id;
      }
    }
    return std::nullopt;
  };
  auto args_text = [&]() {
    std::string text;
    for (std::size_t index = 0; index < parsed.command_args.size(); ++index) {
      if (index != 0) {
        text += ',';
      }
      text += parsed.command_args[index];
    }
    return text;
  };
  auto audit_gm_command = [&](std::string category, const LegacyGmCommandDefinition& definition,
                              std::string reason) {
    const auto target =
        parsed.command_args.empty() ? std::string{} : parsed.command_args.front();
    dispatch.audit_events.push_back(AuditEvent{
        std::move(category),
        "actor=" + locator.character_name + ";cmd=" + definition.canonical_name +
            ";args=" + args_text() + ";target=" + target +
            ";degree=" + std::string(legacy_user_degree_name(locator.user_degree)) +
            ";required=" + std::string(legacy_user_degree_name(definition.minimum_degree)) +
            ";reason=" + std::move(reason),
        locator.account_id});
  };
  auto audit_failed = [&](const LegacyGmCommandDefinition& definition, std::string reason) {
    audit_gm_command("gm.command.failed", definition, std::move(reason));
  };
  auto audit_ok = [&](const LegacyGmCommandDefinition& definition, std::string reason) {
    audit_gm_command("gm.command.ok", definition, std::move(reason));
  };
  auto live_monster_count_for_map = [&](std::string_view map_id) {
    const auto resolved_map_id = resolve_map_id(std::string(map_id));
    auto count = 0;
    for (const auto& group : monster_groups_) {
      for (const auto& ref : group.monsters) {
        if (resolve_map_id(ref.map_id) != resolved_map_id) {
          continue;
        }
        const auto map_it = maps_.find(resolved_map_id);
        if (map_it != maps_.end() && map_it->second->legacy_monster_counts_for_spawn(ref.actor_id)) {
          ++count;
        }
      }
    }
    return count;
  };
  auto run_random_space_move = [&](std::string source_map_id, std::string target_map_id,
                                   std::uint64_t actor_id) {
    dispatch.legacy_random_space_moves.push_back(
        LegacyRandomSpaceMoveRequest{std::move(source_map_id), std::move(target_map_id),
                                     actor_id, 0});
    process_legacy_random_space_moves(dispatch, now_ms);
    process_cross_map_mails(dispatch);
  };
  auto run_space_move = [&](const ActorLocator& source, std::uint64_t actor_id,
                            const std::string& target_map_id, std::int32_t x,
                            std::int32_t y) {
    if (auto map_it = maps_.find(resolve_map_id(source.map_id)); map_it != maps_.end()) {
      append_dispatch(dispatch,
                      map_it->second->legacy_space_move_player(
                          actor_id, resolve_map_id(target_map_id), x, y, true,
                          current_tick_, now_ms));
      process_cross_map_mails(dispatch);
    }
  };
  auto toggle_block_whisper = [&](std::string_view character_name) {
    if (character_name.empty()) {
      return;
    }
    const auto key = legacy_character_key(character_name);
    const auto it = std::find(locator.whisper_block_list.begin(),
                              locator.whisper_block_list.end(), key);
    if (it != locator.whisper_block_list.end()) {
      locator.whisper_block_list.erase(it);
      queue_system("[允许与:" + std::string(character_name) + " 私聊]");
      return;
    }
    locator.whisper_block_list.push_back(key);
    queue_system("[禁止与:" + std::string(character_name) + " 私聊]");
  };

  const auto short_broadcast = parsed.command_name == "!" || parsed.command_name == "$" ||
                               parsed.command_name == "#";
  auto* command_definition = find_legacy_gm_command(parsed.command_name);
  if (parsed.gm_broadcast != LegacyGmBroadcastKind::none && !parsed.command_name.empty()) {
    const auto broadcast_command = parsed.command_name.substr(0, 1);
    command_definition = find_legacy_gm_command(broadcast_command);
  }
  if (command_definition != nullptr &&
      !legacy_user_degree_at_least(locator.user_degree,
                                   command_definition->minimum_degree)) {
    audit_gm_command("gm.command.denied", *command_definition, "permission");
    return true;
  }

  if (parsed.gm_broadcast != LegacyGmBroadcastKind::none || short_broadcast) {
    if (parsed.broadcast_text.empty()) {
      return true;
    }
    std::string message;
    switch (parsed.gm_broadcast) {
      case LegacyGmBroadcastKind::sysop_global_interserver:
        message = "(公告)" + parsed.broadcast_text;
        for (const auto& [_, target] : session_index_) {
          queue_system_to(target, message);
        }
        if (command_definition != nullptr) {
          audit_gm_command("gm.command.ok", *command_definition, "broadcast");
        }
        return true;
      case LegacyGmBroadcastKind::sysop_global_local:
        message = "(!)" + parsed.broadcast_text;
        for (const auto& [_, target] : session_index_) {
          queue_system_to(target, message);
        }
        if (command_definition != nullptr) {
          audit_gm_command("gm.command.ok", *command_definition, "broadcast");
        }
        return true;
      case LegacyGmBroadcastKind::sysop_map:
        message = "(#)" + parsed.broadcast_text;
        for (const auto& [_, target] : session_index_) {
          if (target.map_id == locator.map_id) {
            queue_system_to(target, message);
          }
        }
        if (command_definition != nullptr) {
          audit_gm_command("gm.command.ok", *command_definition, "broadcast");
        }
        return true;
      case LegacyGmBroadcastKind::none:
        return true;
    }
  }

  if (command_definition != nullptr) {
    if (legacy_ascii_equals_ci(command_definition->canonical_name, "GameMaster")) {
      locator.legacy_sysop_mode = !locator.legacy_sysop_mode;
      queue_system(locator.legacy_sysop_mode ? "进入管理员模式" : "退出管理员模式");
      audit_ok(*command_definition, "gm_mode");
      return true;
    }
    if (legacy_ascii_equals_ci(command_definition->canonical_name, "Observer")) {
      locator.legacy_supervisor_mode = !locator.legacy_supervisor_mode;
      queue_system(locator.legacy_supervisor_mode ? "进入观察模式" : "退出观察模式");
      audit_ok(*command_definition, "observer_mode");
      return true;
    }
    if (legacy_ascii_equals_ci(command_definition->canonical_name, "Superman")) {
      locator.legacy_superman_mode = !locator.legacy_superman_mode;
      queue_system(locator.legacy_superman_mode ? "进入无敌模式" : "退出无敌模式");
      audit_ok(*command_definition, "superman_mode");
      return true;
    }
    if (legacy_ascii_equals_ci(command_definition->canonical_name, "Map")) {
      queue_system("地图: " + locator.map_id);
      audit_ok(*command_definition, "map");
      return true;
    }
    if (legacy_ascii_equals_ci(command_definition->canonical_name, "Human")) {
      const auto target_map = parsed.command_args.empty() ? locator.map_id : parsed.command_args[0];
      const auto resolved_map_id = resolve_map_id(target_map);
      auto count = 0;
      for (const auto& [_, target] : session_index_) {
        if (resolve_map_id(target.map_id) == resolved_map_id) {
          ++count;
        }
      }
      queue_system("地图: " + target_map + "当前人数=" + std::to_string(count));
      audit_ok(*command_definition, "human_count");
      return true;
    }
    if (legacy_ascii_equals_ci(command_definition->canonical_name, "MobCount")) {
      const auto target_map = parsed.command_args.empty() ? locator.map_id : parsed.command_args[0];
      queue_system("地图: " + target_map + "当前怪物=" +
                   std::to_string(live_monster_count_for_map(target_map)));
      audit_ok(*command_definition, "mob_count");
      return true;
    }
    if (legacy_ascii_equals_ci(command_definition->canonical_name, "MobLevel")) {
      for (const auto& [_, def] : monster_defs_) {
        queue_system(def.name + " " + std::to_string(def.level));
      }
      audit_ok(*command_definition, "mob_level");
      return true;
    }
    if (legacy_ascii_equals_ci(command_definition->canonical_name, "Info")) {
      const auto target = parsed.command_args.empty() ? std::string{} : parsed.command_args[0];
      const auto character = snapshot_character_actor(target);
      if (target.empty() || !character.has_value()) {
        audit_failed(*command_definition, "target_not_found");
        return true;
      }
      queue_system(character->character_name + " Lv." +
                   std::to_string(character->ability.level) + " " +
                   character->map_id + " " + std::to_string(character->x) + " " +
                   std::to_string(character->y));
      audit_ok(*command_definition, "info");
      return true;
    }
    if (legacy_ascii_equals_ci(command_definition->canonical_name, "Kick")) {
      const auto target = parsed.command_args.empty() ? std::string{} : parsed.command_args[0];
      const auto target_session_id = find_session_id(target);
      if (!target_session_id.has_value()) {
        audit_failed(*command_definition, "target_not_found");
        return true;
      }
      audit_ok(*command_definition, "kick");
      append_dispatch(dispatch, mark_session_disconnected(*target_session_id, "legacy_gm_kick"));
      return true;
    }
    if (legacy_ascii_equals_ci(command_definition->canonical_name, "Ting")) {
      const auto target = parsed.command_args.empty() ? std::string{} : parsed.command_args[0];
      auto* target_locator = find_locator(target);
      if (target_locator == nullptr) {
        queue_system(target + " 无法查找");
        audit_failed(*command_definition, "target_not_found");
        return true;
      }
      const auto target_map = default_map_id_.empty() ? target_locator->map_id : default_map_id_;
      audit_ok(*command_definition, "ting");
      run_random_space_move(target_locator->map_id, target_map, target_locator->actor_id);
      return true;
    }
    if (legacy_ascii_equals_ci(command_definition->canonical_name, "SuperTing")) {
      const auto target = parsed.command_args.empty() ? std::string{} : parsed.command_args[0];
      const auto target_character = snapshot_character_actor(target);
      if (target.empty() || !target_character.has_value()) {
        queue_system(target + " 无法查找");
        audit_failed(*command_definition, "target_not_found");
        return true;
      }
      auto range = 2;
      if (parsed.command_args.size() >= 2) {
        if (const auto parsed_range = parse_legacy_int32(parsed.command_args[1]);
            parsed_range.has_value()) {
          range = std::clamp(*parsed_range, 0, 10);
        }
      }
      for (const auto& [_, target_locator] : session_index_) {
        const auto candidate = snapshot_character_actor(target_locator.character_name);
        if (!candidate.has_value() || candidate->map_id != target_character->map_id ||
            std::abs(candidate->x - target_character->x) > range ||
            std::abs(candidate->y - target_character->y) > range) {
          continue;
        }
        const auto target_map = default_map_id_.empty() ? candidate->map_id : default_map_id_;
        dispatch.legacy_random_space_moves.push_back(
            LegacyRandomSpaceMoveRequest{candidate->map_id, target_map,
                                         target_locator.actor_id, 0});
      }
      audit_ok(*command_definition, "super_ting");
      process_legacy_random_space_moves(dispatch, now_ms);
      process_cross_map_mails(dispatch);
      return true;
    }
    if (legacy_ascii_equals_ci(command_definition->canonical_name, "Move")) {
      const auto target_map = parsed.command_args.empty() ? std::string{} : parsed.command_args[0];
      if (target_map.empty() || maps_.find(resolve_map_id(target_map)) == maps_.end()) {
        audit_failed(*command_definition, "map_not_found");
        return true;
      }
      audit_ok(*command_definition, "move");
      run_random_space_move(locator.map_id, target_map, locator.actor_id);
      return true;
    }
    if (legacy_ascii_equals_ci(command_definition->canonical_name, "PositionMove")) {
      if (parsed.command_args.size() < 3) {
        queue_system("Fail");
        audit_failed(*command_definition, "missing_args");
        return true;
      }
      const auto x = parse_legacy_int32(parsed.command_args[1]);
      const auto y = parse_legacy_int32(parsed.command_args[2]);
      if (!x.has_value() || !y.has_value() ||
          maps_.find(resolve_map_id(parsed.command_args[0])) == maps_.end()) {
        queue_system("Fail");
        audit_failed(*command_definition, "invalid_args");
        return true;
      }
      audit_ok(*command_definition, "position_move");
      run_space_move(locator, locator.actor_id, parsed.command_args[0], *x, *y);
      return true;
    }
    if (legacy_ascii_equals_ci(command_definition->canonical_name, "Recall")) {
      const auto target = parsed.command_args.empty() ? std::string{} : parsed.command_args[0];
      auto* target_locator = find_locator(target);
      const auto speaker = snapshot_character_actor(locator.character_name);
      if (target_locator == nullptr || !speaker.has_value()) {
        queue_system(target + " 无法查找");
        audit_failed(*command_definition, "target_not_found");
        return true;
      }
      const auto [dx, dy] = legacy_direction_delta(speaker->dir);
      audit_ok(*command_definition, "recall");
      run_space_move(*target_locator, target_locator->actor_id, locator.map_id,
                     speaker->x + dx, speaker->y + dy);
      return true;
    }
    auto apply_gm_to_actor = [&](ActorLocator& target_locator) {
      const auto map_it = maps_.find(resolve_map_id(target_locator.map_id));
      if (map_it == maps_.end()) {
        audit_failed(*command_definition, "target_map_not_found");
        return true;
      }
      auto result = map_it->second->legacy_apply_gm_command(
          target_locator.actor_id, command_definition->canonical_name, parsed.command_args,
          current_tick_, now_ms);
      append_dispatch(dispatch, std::move(result.dispatch));
      for (auto& message : result.messages) {
        queue_system(std::move(message));
      }
      if (!result.handled) {
        return false;
      }
      if (result.success) {
        audit_ok(*command_definition, result.reason);
      } else {
        audit_failed(*command_definition, result.reason);
      }
      return true;
    };
    auto apply_gm_to_named_target = [&]() {
      const auto target = parsed.command_args.empty() ? std::string{} : parsed.command_args[0];
      auto* target_locator = find_locator(target);
      if (target_locator == nullptr) {
        const auto is_offline_gold =
            legacy_ascii_equals_ci(command_definition->canonical_name, "AddGold") ||
            legacy_ascii_equals_ci(command_definition->canonical_name, "DelGold");
        if (is_offline_gold) {
          audit_gm_command("gm.command.pending", *command_definition,
                           "offline_character_mutation");
        } else {
          audit_failed(*command_definition, "target_not_found");
        }
        return true;
      }
      return apply_gm_to_actor(*target_locator);
    };
    const auto self_player_command =
        legacy_ascii_equals_ci(command_definition->canonical_name, "Level") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "Level0") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "AdjustTestLevel") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "IncPkPoint") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "ChangeLuck") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "hair") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "Training") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "DeleteSkill") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "ChangeJob") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "ChangeGender") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "NameColor") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "Transparency") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "Make") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "DeleteItem") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "Test_GOLD_Change") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "WeaponRefinery") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "ChangeWeaponDura");
    if (self_player_command) {
      return apply_gm_to_actor(locator);
    }
    const auto target_player_command =
        legacy_ascii_equals_ci(command_definition->canonical_name, "AdjustLevel") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "AdjustExp") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "FreePenalty") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "PKpoint") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "LuckyPoint") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "AddGold") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "DelGold") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "OPTraining") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "OPDeleteSkill") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "flag") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "showopen") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "showunit") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "setflag") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "setopen") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "setunit");
    if (target_player_command) {
      return apply_gm_to_named_target();
    }
    if (legacy_ascii_equals_ci(command_definition->canonical_name, "Mob") ||
        legacy_ascii_equals_ci(command_definition->canonical_name, "RecallMob")) {
      if (parsed.command_args.empty()) {
        audit_failed(*command_definition, "missing_args");
        return true;
      }
      const auto speaker = snapshot_character_actor(locator.character_name);
      if (!speaker.has_value()) {
        audit_failed(*command_definition, "speaker_not_found");
        return true;
      }
      std::string monster_name;
      auto count = 1;
      auto slave_exp_level = std::uint8_t{0};
      for (auto split = parsed.command_args.size(); split > 0; --split) {
        const auto candidate = join_legacy_args(parsed.command_args, 0, split);
        if (!monster_defs_.contains(normalized_key(candidate))) {
          continue;
        }
        monster_name = candidate;
        if (split < parsed.command_args.size()) {
          count = parse_legacy_int32(parsed.command_args[split]).value_or(1);
        }
        if (legacy_ascii_equals_ci(command_definition->canonical_name, "RecallMob") &&
            split + 1 < parsed.command_args.size()) {
          slave_exp_level = static_cast<std::uint8_t>(
              std::clamp(parse_legacy_int32(parsed.command_args[split + 1]).value_or(0), 0, 7));
        }
        break;
      }
      if (monster_name.empty()) {
        audit_failed(*command_definition, "monster_spawn_failed");
        return true;
      }
      const auto [dx, dy] = legacy_direction_delta(speaker->dir);
      const auto clamped_count = std::clamp(count, 1, 50);
      auto master_actor_id = std::uint64_t{0};
      if (legacy_ascii_equals_ci(command_definition->canonical_name, "RecallMob")) {
        master_actor_id = locator.actor_id;
      }
      const auto spawned = spawn_legacy_gm_monsters(
          dispatch, locator.map_id, speaker->x + dx, speaker->y + dy,
          monster_name, clamped_count, now_ms, master_actor_id, slave_exp_level);
      if (spawned == 0) {
        audit_failed(*command_definition, "monster_spawn_failed");
      } else {
        audit_ok(*command_definition, "spawned=" + std::to_string(spawned));
      }
      return true;
    }
    if (legacy_ascii_equals_ci(command_definition->canonical_name, "Mission")) {
      if (parsed.command_args.size() >= 3) {
        locator.legacy_sys_mission_map = resolve_map_id(parsed.command_args[0]);
        locator.legacy_sys_mission_x =
            parse_legacy_int32(parsed.command_args[1]).value_or(locator.legacy_sys_mission_x);
        locator.legacy_sys_mission_y =
            parse_legacy_int32(parsed.command_args[2]).value_or(locator.legacy_sys_mission_y);
        locator.legacy_sys_mission = true;
      } else if (parsed.command_args.size() >= 2) {
        locator.legacy_sys_mission_map = locator.map_id;
        locator.legacy_sys_mission_x =
            parse_legacy_int32(parsed.command_args[0]).value_or(locator.legacy_sys_mission_x);
        locator.legacy_sys_mission_y =
            parse_legacy_int32(parsed.command_args[1]).value_or(locator.legacy_sys_mission_y);
        locator.legacy_sys_mission = true;
      } else {
        locator.legacy_sys_mission = false;
      }
      audit_ok(*command_definition, locator.legacy_sys_mission ? "mission_set" : "mission_clear");
      return true;
    }
    if (legacy_ascii_equals_ci(command_definition->canonical_name, "MobPlace")) {
      if (parsed.command_args.size() < 3) {
        audit_failed(*command_definition, "missing_args");
        return true;
      }
      const auto spawn_x = parse_legacy_int32(parsed.command_args[0]).value_or(0);
      const auto spawn_y = parse_legacy_int32(parsed.command_args[1]).value_or(0);
      std::string monster_name;
      auto count = 1;
      for (auto split = parsed.command_args.size(); split > 2; --split) {
        const auto candidate = join_legacy_args(parsed.command_args, 2, split);
        if (!monster_defs_.contains(normalized_key(candidate))) {
          continue;
        }
        monster_name = candidate;
        if (split < parsed.command_args.size()) {
          count = parse_legacy_int32(parsed.command_args[split]).value_or(1);
        }
        break;
      }
      if (monster_name.empty()) {
        audit_failed(*command_definition, "monster_spawn_failed");
        return true;
      }
      const auto clamped_count = std::clamp(count, 1, 500);
      const auto target_xy = locator.legacy_sys_mission
                                 ? std::optional<std::pair<std::int32_t, std::int32_t>>{
                                       {locator.legacy_sys_mission_x, locator.legacy_sys_mission_y}}
                                 : std::nullopt;
      const auto map_id = locator.legacy_sys_mission ? locator.legacy_sys_mission_map
                                                     : locator.map_id;
      const auto spawned = spawn_legacy_gm_monsters(
          dispatch, map_id, spawn_x, spawn_y, monster_name, clamped_count, now_ms,
          0, 0, target_xy);
      if (spawned == 0) {
        audit_failed(*command_definition, "monster_spawn_failed");
      } else {
        audit_ok(*command_definition, "spawned=" + std::to_string(spawned));
      }
      return true;
    }
    if (legacy_ascii_equals_ci(command_definition->canonical_name, "ReloadAdmin")) {
      legacy_admin_degrees_ = load_legacy_admin_list(config_.runtime.legacy_admin_list);
      for (auto& [_, session] : session_index_) {
        session.user_degree =
            resolve_legacy_user_degree(session.account_id, session.character_name);
      }
      queue_system("ReloadAdmin OK");
      audit_ok(*command_definition, "admin_list");
      return true;
    }
    if (legacy_ascii_equals_ci(command_definition->canonical_name, "SabukWallGold")) {
      queue_system("SabukWallGold 0");
      audit_ok(*command_definition, "castle_snapshot");
      return true;
    }
    if (legacy_ascii_equals_ci(command_definition->canonical_name, "AddGuild")) {
      if (parsed.command_args.size() < 2) {
        audit_failed(*command_definition, "missing_args");
        return true;
      }
      const auto guild_name = parsed.command_args[0];
      const auto lord_name = parsed.command_args[1];
      if (find_runtime_guild_state(guild_castle_snapshot_, guild_name) != nullptr) {
        audit_failed(*command_definition, "guild_exists");
        return true;
      }
      auto* lord_locator = find_locator(lord_name);
      if (lord_locator == nullptr) {
        audit_failed(*command_definition, "target_not_found");
        return true;
      }
      auto lord_snapshot = snapshot_character_actor(lord_name);
      if (!lord_snapshot.has_value() || !lord_snapshot->guild_name.empty()) {
        audit_failed(*command_definition, "target_has_guild");
        return true;
      }
      GuildState guild;
      guild.guild_name = guild_name;
      guild.lord = lord_snapshot->character_name;
      guild.members.push_back(lord_snapshot->character_name);
      guild_castle_snapshot_.guilds.push_back(guild);
      for (const auto& map_id : map_order_) {
        if (auto map_it = maps_.find(map_id); map_it != maps_.end()) {
          map_it->second->set_guild_castle_snapshot(guild_castle_snapshot_);
        }
      }
      ActorMail sync;
      sync.kind = ActorMailKind::guild_membership_sync;
      sync.map_id = lord_locator->map_id;
      sync.actor_id = lord_locator->actor_id;
      sync.character.guild_name = guild_name;
      sync.character.guild_title = "Lord";
      append_dispatch(dispatch, route_actor_mail(sync));
      PersistRequest request;
      request.kind = PersistRequestKind::save_guild_state;
      request.reply_to = "world_service";
      request.guild_name = guild.guild_name;
      request.guild_state = guild;
      dispatch.persist_requests.push_back(std::move(request));
      audit_ok(*command_definition, "guild_created");
      return true;
    }
    if (legacy_ascii_equals_ci(command_definition->canonical_name, "DelGuild")) {
      if (parsed.command_args.empty()) {
        audit_failed(*command_definition, "missing_args");
        return true;
      }
      const auto wanted = legacy_character_key(parsed.command_args[0]);
      auto guild_it = std::find_if(guild_castle_snapshot_.guilds.begin(),
                                   guild_castle_snapshot_.guilds.end(),
                                   [&](const GuildState& guild) {
                                     return legacy_character_key(guild.guild_name) == wanted;
                                   });
      if (guild_it == guild_castle_snapshot_.guilds.end()) {
        audit_failed(*command_definition, "guild_not_found");
        return true;
      }
      const auto guild_name = guild_it->guild_name;
      const auto members = guild_it->members;
      guild_castle_snapshot_.guilds.erase(guild_it);
      for (const auto& member : members) {
        if (auto* member_locator = find_locator(member); member_locator != nullptr) {
          ActorMail sync;
          sync.kind = ActorMailKind::guild_membership_sync;
          sync.map_id = member_locator->map_id;
          sync.actor_id = member_locator->actor_id;
          append_dispatch(dispatch, route_actor_mail(sync));
        }
      }
      for (const auto& map_id : map_order_) {
        if (auto map_it = maps_.find(map_id); map_it != maps_.end()) {
          map_it->second->set_guild_castle_snapshot(guild_castle_snapshot_);
        }
      }
      PersistRequest request;
      request.kind = PersistRequestKind::delete_guild;
      request.reply_to = "world_service";
      request.guild_name = guild_name;
      dispatch.persist_requests.push_back(std::move(request));
      audit_ok(*command_definition, "guild_deleted");
      return true;
    }
    if (legacy_ascii_equals_ci(command_definition->canonical_name, "ChangeSabukLord")) {
      if (parsed.command_args.empty()) {
        audit_failed(*command_definition, "missing_args");
        return true;
      }
      const auto* guild = find_runtime_guild_state(guild_castle_snapshot_, parsed.command_args[0]);
      if (guild == nullptr) {
        audit_failed(*command_definition, "guild_not_found");
        return true;
      }
      guild_castle_snapshot_.castle_dialog.owner_guild = guild->guild_name;
      guild_castle_snapshot_.castle_dialog.lord = guild->lord;
      set_guild_castle_snapshot(guild_castle_snapshot_);
      PersistRequest request;
      request.kind = PersistRequestKind::save_castle_state;
      request.reply_to = "world_service";
      request.castle_name = guild_castle_snapshot_.castle_dialog.castle_name.empty()
                                ? config_.runtime.castle_name
                                : guild_castle_snapshot_.castle_dialog.castle_name;
      request.guild_castle_snapshot = guild_castle_snapshot_;
      request.payload_json = "{\"owner_guild\":\"" +
                             guild_castle_snapshot_.castle_dialog.owner_guild +
                             "\",\"lord\":\"" + guild_castle_snapshot_.castle_dialog.lord + "\"}";
      dispatch.persist_requests.push_back(std::move(request));
      audit_ok(*command_definition, "castle_lord_changed");
      return true;
    }
  }

  if (command_definition != nullptr &&
      command_definition->implementation == LegacyGmCommandImplementation::pending) {
    audit_gm_command("gm.command.pending", *command_definition,
                     command_definition->dependency);
    return true;
  }

  if (legacy_command_equals(parsed.command_name, "拒绝私聊",
                            std::string_view("\xBE\xDC\xBE\xF8\xCB\xBD\xC1\xC4", 8))) {
    locator.hear_whisper = !locator.hear_whisper;
    queue_system(locator.hear_whisper ? "[允许接收私聊信息]" : "[拒绝接收私聊信息]");
    return true;
  }
  if (legacy_command_equals(parsed.command_name, "允许私聊",
                            std::string_view("\xD4\xCA\xD0\xED\xCB\xBD\xC1\xC4", 8))) {
    locator.hear_whisper = true;
    queue_system("[允许私聊]");
    return true;
  }
  if (legacy_command_equals(parsed.command_name, "拒绝",
                            std::string_view("\xBE\xDC\xBE\xF8", 4))) {
    for (std::size_t index = 0; index < parsed.command_args.size() && index < 3; ++index) {
      toggle_block_whisper(parsed.command_args[index]);
    }
    return true;
  }
  if (legacy_command_equals(parsed.command_name, "拒绝喊话",
                            std::string_view("\xBE\xDC\xBE\xF8\xBA\xB0\xBB\xB0", 8)) ||
      legacy_command_equals(parsed.command_name, "允许喊话",
                            std::string_view("\xD4\xCA\xD0\xED\xBA\xB0\xBB\xB0", 8))) {
    locator.hear_cry = !locator.hear_cry;
    queue_system(locator.hear_cry ? "[允许接收(黄颜色字体)喊话]"
                                  : "[拒绝接收(黄颜色字体)喊话]");
    return true;
  }
  if (legacy_command_equals(
          parsed.command_name, "允许行会喊话",
          std::string_view("\xD4\xCA\xD0\xED\xD0\xD0\xBB\xE1\xBA\xB0\xBB\xB0", 12)) ||
      legacy_command_equals(
          parsed.command_name, "拒绝行会喊话",
          std::string_view("\xBE\xDC\xBE\xF8\xD0\xD0\xBB\xE1\xBA\xB0\xBB\xB0", 12))) {
    locator.hear_guild_msg = !locator.hear_guild_msg;
    queue_system(locator.hear_guild_msg ? "允许接收行会喊话信息"
                                        : "拒绝接收行会喊话信息");
    return true;
  }

  const auto shutup = legacy_ascii_equals_ci(parsed.command_name, "Shutup");
  const auto release_shutup =
      legacy_ascii_equals_ci(parsed.command_name, "ReleaseShutup");
  const auto shutup_list = legacy_ascii_equals_ci(parsed.command_name, "ShutupList");
  if (shutup || release_shutup || shutup_list) {
    for (auto it = legacy_shut_up_list_.begin(); it != legacy_shut_up_list_.end();) {
      if (now_ms > it->second.expire_ms) {
        it = legacy_shut_up_list_.erase(it);
      } else {
        ++it;
      }
    }
    if (shutup) {
      const auto target = parsed.command_args.empty() ? std::string{} : parsed.command_args[0];
      if (target.empty()) {
        queue_system("无法查找");
        return true;
      }
      auto minutes = std::int32_t{5};
      if (parsed.command_args.size() >= 2) {
        if (const auto parsed_minutes = parse_legacy_int32(parsed.command_args[1]);
            parsed_minutes.has_value() && *parsed_minutes >= 0) {
          minutes = *parsed_minutes;
        }
      }
      add_legacy_shut_up(target, static_cast<std::uint64_t>(minutes) * 60ULL * 1000ULL,
                         now_ms);
      queue_system(target + "禁止聊天 + " + std::to_string(minutes) + "分钟");
      if (command_definition != nullptr) {
        audit_gm_command("gm.command.ok", *command_definition, "shutup");
      }
      return true;
    }
    if (release_shutup) {
      const auto target = parsed.command_args.empty() ? std::string{} : parsed.command_args[0];
      if (target.empty() || !release_legacy_shut_up(target)) {
        queue_system(target + " 无法查找");
        return true;
      }
      if (const auto* target_locator = find_locator(target); target_locator != nullptr) {
        queue_system_to(*target_locator, "从禁止聊天列表删除");
      }
      queue_system(target + " ");
      if (command_definition != nullptr) {
        audit_gm_command("gm.command.ok", *command_definition, "release_shutup");
      }
      return true;
    }
    for (const auto& [_, entry] : legacy_shut_up_list_) {
      queue_system(entry.character_name + " " +
                   legacy_minutes_text(entry.expire_ms, now_ms) + "分钟");
    }
    if (command_definition != nullptr) {
      audit_gm_command("gm.command.ok", *command_definition, "shutup_list");
    }
    return true;
  }

  return false;
}

bool LogicRuntime::route_legacy_chat_command(const LogicCommand& command,
                                             ActorLocator& locator,
                                             std::uint64_t now_ms,
                                             RuntimeDispatch& dispatch) {
  constexpr std::uint64_t kBombSayWindowMs = 3000;
  constexpr std::uint64_t kAutoShutUpMs = 60ULL * 1000ULL;
  constexpr std::uint64_t kCryCooldownMs = 10ULL * 1000ULL;
  const auto parsed = parse_legacy_chat_input(command.text);

  auto queue_delivery = [&](std::string map_id, std::uint64_t actor_id,
                            LegacyChatDeliveryKind kind, std::string payload,
                            std::uint64_t target_actor_id = 0) {
    ActorMail mail;
    mail.kind = ActorMailKind::legacy_chat_delivery;
    mail.map_id = std::move(map_id);
    mail.actor_id = actor_id;
    mail.session_id = command.session_id;
    mail.session_seq = command.session_seq;
    mail.target_actor_id = target_actor_id;
    mail.legacy_chat_kind = kind;
    mail.payload = std::move(payload);
    append_dispatch(dispatch, route_actor_mail(mail));
  };
  auto queue_system = [&](std::string payload) {
    queue_delivery(locator.map_id, locator.actor_id, LegacyChatDeliveryKind::system,
                   std::move(payload), locator.actor_id);
  };
  auto find_locator = [&](std::string_view character_name) -> ActorLocator* {
    const auto key = legacy_character_key(character_name);
    for (auto& [_, candidate] : session_index_) {
      if (legacy_character_key(candidate.character_name) == key) {
        return &candidate;
      }
    }
    return nullptr;
  };

  switch (parsed.kind) {
    case LegacyChatInputKind::empty:
      return true;
    case LegacyChatInputKind::command:
      return handle_legacy_chat_command(command, locator, parsed, now_ms, dispatch);
    default:
      break;
  }

  if (command.text == locator.latest_say_text &&
      now_ms >= locator.bomb_say_time_ms &&
      now_ms - locator.bomb_say_time_ms < kBombSayWindowMs) {
    ++locator.bomb_say_count;
    if (locator.bomb_say_count >= 2) {
      locator.auto_shut_up_until_ms = now_ms + kAutoShutUpMs;
      queue_system("[由于您重复发出相同内容，一分钟内将被禁止交谈。]");
    }
  } else {
    locator.latest_say_text = command.text;
    locator.bomb_say_time_ms = now_ms;
    locator.bomb_say_count = 0;
  }

  if (now_ms > locator.auto_shut_up_until_ms) {
    locator.auto_shut_up_until_ms = 0;
  }
  auto shut_up = locator.auto_shut_up_until_ms != 0;
  const auto shut_up_key = legacy_character_key(locator.character_name);
  if (auto shut_up_it = legacy_shut_up_list_.find(shut_up_key);
      shut_up_it != legacy_shut_up_list_.end()) {
    if (now_ms > shut_up_it->second.expire_ms) {
      legacy_shut_up_list_.erase(shut_up_it);
    } else {
      shut_up = true;
    }
  }
  if (shut_up) {
    queue_system("禁止聊天");
    return true;
  }

  switch (parsed.kind) {
    case LegacyChatInputKind::normal:
      return false;
    case LegacyChatInputKind::whisper: {
      if (parsed.target_name.empty()) {
        return true;
      }
      auto* target = find_locator(parsed.target_name);
      if (target == nullptr) {
        queue_system(parsed.target_name + "无法查找");
        return true;
      }
      if (!target->hear_whisper ||
          std::find(target->whisper_block_list.begin(),
                    target->whisper_block_list.end(),
                    legacy_character_key(locator.character_name)) !=
              target->whisper_block_list.end()) {
        queue_system(parsed.target_name + "拒绝密语");
        return true;
      }
      queue_delivery(target->map_id, target->actor_id, LegacyChatDeliveryKind::whisper,
                     locator.character_name + "=> " + parsed.message_text,
                     locator.actor_id);
      return true;
    }
    case LegacyChatInputKind::group: {
      const auto group_it = legacy_groups_.find(locator.legacy_group_id);
      if (locator.legacy_group_id == 0 || group_it == legacy_groups_.end()) {
        return true;
      }
      const auto payload = "-" + locator.character_name + ": " + parsed.message_text;
      for (const auto member_session_id : group_it->second.members) {
        const auto member_it = session_index_.find(member_session_id);
        if (member_it == session_index_.end()) {
          continue;
        }
        queue_delivery(member_it->second.map_id, member_it->second.actor_id,
                       LegacyChatDeliveryKind::group, payload, locator.actor_id);
      }
      return true;
    }
    case LegacyChatInputKind::guild: {
      const auto character = snapshot_character_actor(locator.character_name);
      if (!character.has_value() || character->guild_name.empty()) {
        return true;
      }
      const auto* guild = find_runtime_guild_state(guild_castle_snapshot_, character->guild_name);
      if (guild == nullptr) {
        return true;
      }
      const auto payload = locator.character_name + ":" + parsed.message_text;
      for (const auto& member_name : guild->members) {
        const auto* member = find_locator(member_name);
        if (member == nullptr || !member->hear_guild_msg) {
          continue;
        }
        queue_delivery(member->map_id, member->actor_id, LegacyChatDeliveryKind::guild,
                       payload, member->actor_id);
      }
      return true;
    }
    case LegacyChatInputKind::shout: {
      const auto resolved_map_id = resolve_map_id(locator.map_id);
      const auto map_it = std::find_if(config_.maps.begin(), config_.maps.end(),
                                       [&](const MapConfig& map) {
                                         return map.id == resolved_map_id;
                                       });
      if (map_it != config_.maps.end() && map_it->quiz_zone) {
        queue_system("无法使用");
        return true;
      }
      if (locator.has_latest_cry_time &&
          now_ms - locator.latest_cry_time_ms <= kCryCooldownMs) {
        const auto seconds = 10 - ((now_ms - locator.latest_cry_time_ms) / 1000);
        queue_system(std::to_string(seconds) + "秒以后才能再次使用喊话。");
        return true;
      }
      const auto character = snapshot_character_actor(locator.character_name);
      if (!character.has_value() || character->ability.level <= 7) {
        queue_system("喊话功能只有7级以上才可以使用");
        return true;
      }
      locator.has_latest_cry_time = true;
      locator.latest_cry_time_ms = now_ms;
      const auto speaker = snapshot_character_actor(locator.character_name);
      if (!speaker.has_value()) {
        return true;
      }
      const auto line = "(!)" + locator.character_name + ":" + parsed.message_text;
      for (const auto& [_, target] : session_index_) {
        if (target.map_id != locator.map_id || !target.hear_cry) {
          continue;
        }
        const auto target_character = snapshot_character_actor(target.character_name);
        if (!target_character.has_value()) {
          continue;
        }
        if (std::abs(target_character->x - speaker->x) >= 50 ||
            std::abs(target_character->y - speaker->y) >= 50) {
          continue;
        }
        queue_delivery(target.map_id, target.actor_id, LegacyChatDeliveryKind::shout_direct,
                       line, 0);
      }
      return true;
    }
    case LegacyChatInputKind::empty:
    case LegacyChatInputKind::command:
      return true;
  }

  return true;
}

bool LogicRuntime::is_merchant_npc_config(const NpcConfig& npc, const ActorMail& mail) const {
  const auto service = util::lower_copy(npc.service);
  return !npc.merchant_goods.empty() || !npc.merchant_products.empty() ||
         !mail.merchant_items.empty() ||
         service.find("buy") != std::string::npos ||
         service.find("sell") != std::string::npos ||
         service.find("repair") != std::string::npos ||
         service.find("storage") != std::string::npos ||
         service.find("upgrade") != std::string::npos ||
         service.find("merchant") != std::string::npos ||
         service.find("shop") != std::string::npos ||
         dialog_has_weapon_upgrade_link(npc.dialog_sections);
}

void LogicRuntime::add_stage_trace(RuntimeDispatch& dispatch, std::string stage,
                                   std::string action, std::uint64_t now_ms,
                                   std::size_t cursor, std::size_t sub_cursor) const {
  dispatch.legacy_traces.push_back(LegacyRuntimeTrace{
      std::move(stage),
      std::move(action),
      {},
      {},
      0,
      now_ms,
      current_tick_,
      cursor,
      sub_cursor,
      0});
}

RuntimeDispatch LogicRuntime::route_logic_command(const LogicCommand& command) {
  RuntimeDispatch dispatch;

  switch (command.kind) {
    case LogicCommandKind::enter_world: {
      LegacyReadyUser ready;
      ready.session_id = command.session_id;
      ready.gateway = command.gateway.empty() ? "game_gateway" : command.gateway;
      ready.account_id = command.account_id;
      ready.character_name = command.character_name;
      ready.map_id = command.map_id;
      ready.x = command.x;
      ready.y = command.y;
      ready.character = command.character;
      ready.fast_initialize = true;
      append_dispatch(dispatch, enqueue_ready_user(std::move(ready)));
      break;
    }
    case LogicCommandKind::group_create:
      create_legacy_group(command.session_id, command.text);
      break;
    case LogicCommandKind::group_add_member:
      add_legacy_group_member(command.session_id, command.text);
      break;
    case LogicCommandKind::group_remove_member:
      remove_legacy_group_member_by_name(command.session_id, command.text);
      break;
    case LogicCommandKind::turn:
    case LogicCommandKind::walk:
    case LogicCommandKind::run:
    case LogicCommandKind::attack:
    case LogicCommandKind::spell:
    case LogicCommandKind::say:
    case LogicCommandKind::click_npc:
    case LogicCommandKind::merchant_select:
    case LogicCommandKind::query_username:
    case LogicCommandKind::query_bag_items:
    case LogicCommandKind::query_storage_items:
    case LogicCommandKind::query_detail_goods:
    case LogicCommandKind::query_sell_price:
    case LogicCommandKind::query_repair_cost:
    case LogicCommandKind::drop_item:
    case LogicCommandKind::pickup_item:
    case LogicCommandKind::take_on_item:
    case LogicCommandKind::take_off_item:
    case LogicCommandKind::eat_item:
    case LogicCommandKind::drop_gold:
    case LogicCommandKind::revive:
    case LogicCommandKind::buy_item:
    case LogicCommandKind::sell_item:
    case LogicCommandKind::repair_item:
    case LogicCommandKind::storage_item:
    case LogicCommandKind::take_back_storage_item:
    case LogicCommandKind::trade_try:
    case LogicCommandKind::trade_cancel:
    case LogicCommandKind::trade_add_item:
    case LogicCommandKind::trade_remove_item:
    case LogicCommandKind::trade_set_gold:
    case LogicCommandKind::trade_accept:
    case LogicCommandKind::logout: {
      auto it = session_index_.find(command.session_id);
      if (it == session_index_.end()) {
        break;
      }

      if (command.kind == LogicCommandKind::logout) {
        append_dispatch(dispatch,
                        mark_session_disconnected(command.session_id, "logout"));
      } else {
        const auto command_now_ms = command.timestamp_ms != 0 ? command.timestamp_ms : last_now_ms_;
        if (command.kind == LogicCommandKind::say &&
            route_legacy_chat_command(command, it->second, command_now_ms, dispatch)) {
          break;
        }
        const auto mail = make_player_mail(command, it->second);
        if (auto map_it = maps_.find(it->second.map_id); map_it != maps_.end()) {
          static_cast<void>(map_it->second->enqueue_legacy_player_command(mail, command_now_ms));
        }
      }
      break;
    }
    case LogicCommandKind::authenticate:
    case LogicCommandKind::raw_packet:
      break;
  }

  return dispatch;
}

RuntimeDispatch LogicRuntime::route_actor_mail(const ActorMail& mail) {
  RuntimeDispatch dispatch;
  const auto map_id = resolve_map_id(mail.map_id);
  auto map_it = maps_.find(map_id);
  if (map_it != maps_.end()) {
    if (mail.kind == ActorMailKind::spawn_player) {
      append_dispatch(dispatch,
                      map_it->second->legacy_spawn_player(mail, current_tick_, last_now_ms_, true));
    } else {
      map_it->second->enqueue_mail(mail);
    }
  }
  return dispatch;
}

RuntimeDispatch LogicRuntime::enqueue_ready_user(LegacyReadyUser ready_user) {
  RuntimeDispatch dispatch;
  if (ready_user.character.account_id.empty()) {
    ready_user.character.account_id = ready_user.account_id;
  }
  if (ready_user.character.character_name.empty()) {
    ready_user.character.character_name = ready_user.character_name;
  }
  if (ready_user.account_id.empty()) {
    ready_user.account_id = ready_user.character.account_id;
  }
  if (ready_user.character_name.empty()) {
    ready_user.character_name = ready_user.character.character_name;
  }
  const auto key = util::lower_copy(ready_user.character_name);
  if (ready_user.session_id == 0 || key.empty()) {
    return dispatch;
  }
  bool duplicate = has_live_or_closing_character(key);
  for (const auto& queued : ready_users_) {
    if (util::lower_copy(queued.character_name.empty() ? queued.character.character_name
                                                       : queued.character_name) == key) {
      duplicate = true;
      break;
    }
  }
  if (duplicate) {
    dispatch.session_events.push_back(
        SessionEvent{SessionEventKind::force_disconnect,
                     ready_user.gateway.empty() ? "game_gateway" : ready_user.gateway,
                     ready_user.session_id,
                     {},
                     {},
                     "duplicate_login"});
    dispatch.audit_events.push_back(
        AuditEvent{"world.duplicate_login", ready_user.account_id + ":" +
                                                ready_user.character_name,
                   ready_user.gateway});
    return dispatch;
  }
  ready_user.character.map_id =
      resolve_map_id(ready_user.character.map_id.empty() ? ready_user.map_id
                                                         : ready_user.character.map_id);
  make_index_allocator_.observe(ready_user.character);
  ready_users_.push_back(std::move(ready_user));
  return dispatch;
}

RuntimeDispatch LogicRuntime::relocate_no_reconnect_player(std::uint64_t session_id,
                                                           std::uint64_t now_ms) {
  RuntimeDispatch dispatch;
  const auto locator_it = session_index_.find(session_id);
  if (locator_it == session_index_.end()) {
    return dispatch;
  }
  const auto locator = locator_it->second;
  const auto map_config_it =
      std::find_if(config_.maps.begin(), config_.maps.end(), [&](const MapConfig& map) {
        return map.id == locator.map_id;
      });
  if (map_config_it == config_.maps.end() || !map_config_it->no_reconnect ||
      map_config_it->back_map.empty()) {
    return dispatch;
  }
  const auto source_it = maps_.find(locator.map_id);
  const auto target_it = maps_.find(map_config_it->back_map);
  if (source_it == maps_.end() || target_it == maps_.end()) {
    return dispatch;
  }
  const auto target = target_it->second->legacy_random_space_move_target(legacy_random_);
  if (!target.has_value()) {
    return dispatch;
  }
  append_dispatch(dispatch,
                  source_it->second->legacy_space_move_player(
                      locator.actor_id, map_config_it->back_map, target->first, target->second,
                      false, current_tick_, now_ms));
  process_cross_map_mails(dispatch);
  return dispatch;
}

RuntimeDispatch LogicRuntime::mark_session_disconnected(std::uint64_t session_id,
                                                        std::string reason) {
  RuntimeDispatch dispatch;
  for (auto it = ready_users_.begin(); it != ready_users_.end(); ++it) {
    if (it->session_id != session_id) {
      continue;
    }
    const auto character_name = it->character_name.empty() ? it->character.character_name
                                                          : it->character_name;
    if (!character_name.empty()) {
      close_records_[util::lower_copy(character_name)] =
          CloseRecord{session_id, it->account_id, character_name, last_now_ms_, std::move(reason)};
    }
    ready_users_.erase(it);
    return dispatch;
  }

  const auto locator_it = session_index_.find(session_id);
  if (locator_it == session_index_.end()) {
    return dispatch;
  }
  remove_legacy_group_member(session_id);
  append_dispatch(dispatch, relocate_no_reconnect_player(session_id, last_now_ms_));
  const auto relocated_locator_it = session_index_.find(session_id);
  if (relocated_locator_it == session_index_.end()) {
    return dispatch;
  }
  if (auto map_it = maps_.find(relocated_locator_it->second.map_id); map_it != maps_.end()) {
    append_dispatch(dispatch,
                    map_it->second->legacy_disconnect_player(
                        relocated_locator_it->second.actor_id, last_now_ms_));
  }
  return dispatch;
}

RuntimeDispatch LogicRuntime::tick() {
  const auto now_ms =
      (current_tick_ + 1) * static_cast<std::uint64_t>(std::max<std::uint32_t>(config_.budgets.tick_ms, 1));
  return tick(now_ms);
}

RuntimeDispatch LogicRuntime::tick(std::uint64_t now_ms) {
  return tick(now_ms, LegacyRuntimeContext{});
}

RuntimeDispatch LogicRuntime::tick(std::uint64_t now_ms, LegacyRuntimeContext context) {
  RuntimeDispatch combined;
  last_now_ms_ = now_ms;
  ++current_tick_;

  cleanup_close_records(now_ms);
  process_ready_users(now_ms, combined);
  process_user_humans(now_ms, context, combined);
  process_monsters(now_ms, combined);
  process_merchants(now_ms, combined);
  process_npcs(now_ms, combined);
  process_user_engine_timers(now_ms, combined);

  for (const auto& map_id : map_order_) {
    auto map_it = maps_.find(map_id);
    if (map_it == maps_.end()) {
      continue;
    }
    append_dispatch(combined, map_it->second->tick(current_tick_, now_ms));
  }

  process_legacy_event_creates(combined, now_ms);
  process_legacy_random_space_moves(combined, now_ms);
  process_cross_map_mails(combined);
  return combined;
}

RuntimeDispatch LogicRuntime::run_legacy_event_manager(std::uint64_t now_ms) {
  RuntimeDispatch dispatch;
  refresh_legacy_holy_curtain_groups(dispatch, now_ms);
  auto result = legacy_event_manager_.run(now_ms, current_tick_);
  append_dispatch(dispatch, std::move(result.dispatch));
  for (const auto& event : result.fire_burn_events) {
    if (auto map_it = maps_.find(event.map_id); map_it != maps_.end()) {
      append_dispatch(dispatch,
                      map_it->second->legacy_apply_fire_burn_event(event, current_tick_, now_ms));
    }
  }
  for (const auto& event : result.closed_events) {
    if (auto map_it = maps_.find(event.map_id); map_it != maps_.end()) {
      map_it->second->legacy_remove_event_object(event.id, event.x, event.y, &dispatch);
    }
  }
  return dispatch;
}

std::uint64_t LogicRuntime::enqueue_legacy_event(LegacyEventRecord record) {
  record.map_id = resolve_map_id(record.map_id);
  const auto event_id = legacy_event_manager_.enqueue(record, last_now_ms_);
  if (event_id != 0) {
    if (auto map_it = maps_.find(record.map_id); map_it != maps_.end()) {
      static_cast<void>(map_it->second->legacy_add_event_object(
          event_id, record.x, record.y, last_now_ms_, record.blocks_walk, nullptr,
          record.type));
    }
  }
  return event_id;
}

std::optional<LegacyEventRecord> LogicRuntime::find_legacy_event(
    const std::string& map_id, std::int32_t x, std::int32_t y,
    LegacyEventType type) const {
  return legacy_event_manager_.find(resolve_map_id(map_id), x, y, type);
}

void LogicRuntime::process_legacy_event_creates(RuntimeDispatch& dispatch,
                                                std::uint64_t now_ms) {
  if (dispatch.legacy_event_creates.empty() &&
      dispatch.legacy_holy_curtain_groups.empty()) {
    return;
  }
  auto event_creates = std::move(dispatch.legacy_event_creates);
  auto holy_groups = std::move(dispatch.legacy_holy_curtain_groups);
  dispatch.legacy_event_creates.clear();
  dispatch.legacy_holy_curtain_groups.clear();

  std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> group_event_ids;
  for (auto& event : event_creates) {
    event.map_id = resolve_map_id(event.map_id);
    const auto event_id = enqueue_legacy_event(event);
    if (event_id != 0 && event.holy_group_id != 0) {
      group_event_ids[event.holy_group_id].push_back(event_id);
    }
  }

  for (auto& group : holy_groups) {
    group.map_id = resolve_map_id(group.map_id);
    if (auto ids = group_event_ids.find(group.id); ids != group_event_ids.end()) {
      group.event_ids = std::move(ids->second);
    }
    if (!group.event_ids.empty() && !group.seized_actor_ids.empty()) {
      static_cast<void>(legacy_event_manager_.enqueue_holy_curtain_group(
          std::move(group), now_ms));
    }
  }
}

void LogicRuntime::process_legacy_random_space_moves(RuntimeDispatch& dispatch,
                                                     std::uint64_t now_ms) {
  while (!dispatch.legacy_random_space_moves.empty()) {
    auto requests = std::move(dispatch.legacy_random_space_moves);
    dispatch.legacy_random_space_moves.clear();
    for (auto& request : requests) {
      const auto source_map_id = resolve_map_id(request.source_map_id);
      const auto target_map_id = resolve_map_id(
          request.target_map_id.empty() ? default_map_id_ : request.target_map_id);
      const auto source_it = maps_.find(source_map_id);
      const auto target_it = maps_.find(target_map_id);
      if (source_it == maps_.end() || target_it == maps_.end()) {
        continue;
      }
      const auto target = target_it->second->legacy_random_space_move_target(legacy_random_);
      if (!target.has_value()) {
        continue;
      }
      append_dispatch(dispatch,
                      source_it->second->legacy_space_move_player(
                          request.actor_id, target_map_id, target->first, target->second,
                          true, current_tick_, now_ms));
    }
  }
}

void LogicRuntime::process_cross_map_mails(RuntimeDispatch& combined) {
  while (!combined.cross_map_mails.empty()) {
    auto cross_map_mails = std::move(combined.cross_map_mails);
    combined.cross_map_mails.clear();
    for (const auto& cross_map_mail : cross_map_mails) {
      auto routed = route_actor_mail(cross_map_mail);
      append_dispatch(combined, std::move(routed));
      if (cross_map_mail.kind == ActorMailKind::spawn_player &&
          cross_map_mail.session_id != 0) {
        const auto target_map_id = resolve_map_id(cross_map_mail.map_id);
        if (auto target_it = maps_.find(target_map_id); target_it != maps_.end() &&
            target_it->second->legacy_player_state(cross_map_mail.actor_id).has_value()) {
          auto updated = ActorLocator{target_map_id, cross_map_mail.actor_id,
                                      cross_map_mail.character.account_id,
                                      cross_map_mail.character.character_name};
          if (auto previous = session_index_.find(cross_map_mail.session_id);
              previous != session_index_.end()) {
            updated.latest_say_text = std::move(previous->second.latest_say_text);
            updated.bomb_say_time_ms = previous->second.bomb_say_time_ms;
            updated.bomb_say_count = previous->second.bomb_say_count;
            updated.auto_shut_up_until_ms = previous->second.auto_shut_up_until_ms;
            updated.has_latest_cry_time = previous->second.has_latest_cry_time;
            updated.latest_cry_time_ms = previous->second.latest_cry_time_ms;
            updated.hear_whisper = previous->second.hear_whisper;
            updated.hear_cry = previous->second.hear_cry;
            updated.hear_guild_msg = previous->second.hear_guild_msg;
            updated.whisper_block_list = std::move(previous->second.whisper_block_list);
            updated.legacy_group_id = previous->second.legacy_group_id;
            updated.user_degree = previous->second.user_degree;
            updated.legacy_sysop_mode = previous->second.legacy_sysop_mode;
            updated.legacy_supervisor_mode = previous->second.legacy_supervisor_mode;
            updated.legacy_superman_mode = previous->second.legacy_superman_mode;
            updated.legacy_sys_mission = previous->second.legacy_sys_mission;
            updated.legacy_sys_mission_map = previous->second.legacy_sys_mission_map;
            updated.legacy_sys_mission_x = previous->second.legacy_sys_mission_x;
            updated.legacy_sys_mission_y = previous->second.legacy_sys_mission_y;
          }
          session_index_[cross_map_mail.session_id] = std::move(updated);
        }
      }
    }
  }
}

void LogicRuntime::refresh_legacy_holy_curtain_groups(RuntimeDispatch& dispatch,
                                                      std::uint64_t now_ms) {
  const auto groups = legacy_event_manager_.active_holy_groups();
  for (const auto& group : groups) {
    const auto map_it = maps_.find(group.map_id);
    if (map_it == maps_.end()) {
      auto result = legacy_event_manager_.update_holy_group_seized(
          group.id, {}, now_ms, current_tick_);
      append_dispatch(dispatch, std::move(result.dispatch));
      for (const auto& event : result.closed_events) {
        if (auto event_map_it = maps_.find(event.map_id); event_map_it != maps_.end()) {
          event_map_it->second->legacy_remove_event_object(event.id, event.x, event.y,
                                                           &dispatch);
        }
      }
      continue;
    }
    auto active_ids =
        map_it->second->legacy_active_holy_seize_actor_ids(group.seized_actor_ids, now_ms);
    auto result = legacy_event_manager_.update_holy_group_seized(
        group.id, std::move(active_ids), now_ms, current_tick_);
    append_dispatch(dispatch, std::move(result.dispatch));
    for (const auto& event : result.closed_events) {
      if (auto event_map_it = maps_.find(event.map_id); event_map_it != maps_.end()) {
        event_map_it->second->legacy_remove_event_object(event.id, event.x, event.y,
                                                         &dispatch);
      }
    }
  }
}

void LogicRuntime::process_ready_users(std::uint64_t now_ms, RuntimeDispatch& dispatch) {
  constexpr std::uint64_t kReadyIntervalMs = 200;
  const auto ready_due =
      last_ready_process_ms_ == 0 || now_ms >= last_ready_process_ms_ + kReadyIntervalMs;
  if (!ready_due && (ready_users_.empty() || !ready_users_.front().fast_initialize)) {
    return;
  }
  if (ready_due) {
    last_ready_process_ms_ = now_ms;
  }

  while (!ready_users_.empty()) {
    if (!ready_due && !ready_users_.front().fast_initialize) {
      break;
    }
    auto ready = std::move(ready_users_.front());
    ready_users_.pop_front();

    CharacterRecord character = ready.character;
    if (character.account_id.empty()) {
      character.account_id = ready.account_id;
    }
    if (character.character_name.empty()) {
      character.character_name = ready.character_name;
    }
    if (character.character_name.empty() || ready.session_id == 0) {
      continue;
    }
    if (has_live_or_closing_character(character.character_name)) {
      dispatch.session_events.push_back(
          SessionEvent{SessionEventKind::force_disconnect,
                       ready.gateway.empty() ? "game_gateway" : ready.gateway,
                       ready.session_id,
                       {},
                       {},
                       "duplicate_login"});
      continue;
    }

    character.map_id = resolve_map_id(character.map_id.empty() ? ready.map_id : character.map_id);
    if (character.x == 0 && character.y == 0) {
      character.x = ready.x;
      character.y = ready.y;
    }
    if (character.x == 0 && character.y == 0) {
      for (const auto& map : config_.maps) {
        if (map.id == character.map_id) {
          character.x = map.home_x;
          character.y = map.home_y;
          break;
        }
      }
    }

    const auto map_it = maps_.find(character.map_id);
    if (map_it == maps_.end()) {
      continue;
    }

    ActorMail mail;
    mail.kind = ActorMailKind::spawn_player;
    mail.map_id = character.map_id;
    mail.actor_id = next_actor_id_++;
    mail.session_id = ready.session_id;
    mail.character = character;
    mail.name = character.character_name;

    append_dispatch(dispatch,
                    map_it->second->legacy_spawn_player(mail, current_tick_, now_ms,
                                                        ready.fast_initialize));
    if (!map_it->second->legacy_player_state(mail.actor_id).has_value()) {
      continue;
    }

    auto locator = ActorLocator{character.map_id, mail.actor_id, character.account_id,
                                character.character_name};
    locator.user_degree =
        resolve_legacy_user_degree(character.account_id, character.character_name);
    session_index_[ready.session_id] = std::move(locator);
    run_user_order_.push_back(ready.session_id);
    dispatch.audit_events.push_back(
        AuditEvent{"world.enter", character.account_id + ":" + character.character_name,
                   character.map_id});
    dispatch.audit_events.push_back(
        AuditEvent{"world.ready_user", character.account_id + ":" + character.character_name,
                   character.map_id});
  }
}

void LogicRuntime::process_user_humans(std::uint64_t now_ms,
                                       const LegacyRuntimeContext& context,
                                       RuntimeDispatch& dispatch) {
  add_stage_trace(dispatch, "ProcessUserHumans", "begin", now_ms, hum_cur_, 0);
  if (run_user_order_.empty()) {
    hum_cur_ = 0;
    return;
  }
  if (hum_cur_ >= run_user_order_.size()) {
    hum_cur_ = 0;
  }

  const auto started = std::chrono::steady_clock::now();
  const auto budget_ms = static_cast<std::int64_t>(config_.budgets.player_budget_ms);
  const auto player_input_budget_per_tick =
      context.player_input_budget_per_tick != 0
          ? context.player_input_budget_per_tick
          : std::max<std::size_t>(
                1, static_cast<std::size_t>(config_.budgets.player_input_budget_per_tick));
  std::size_t processed = 0;
  const auto initial_size = run_user_order_.size();
  while (!run_user_order_.empty() && processed < initial_size) {
    if (context.player_process_limit > 0 && processed >= context.player_process_limit) {
      break;
    }
    if (hum_cur_ >= run_user_order_.size()) {
      hum_cur_ = 0;
    }
    const auto index = hum_cur_;
    const auto session_id = run_user_order_[index];
    const auto locator_it = session_index_.find(session_id);
    if (locator_it == session_index_.end()) {
      run_user_order_.erase(run_user_order_.begin() + static_cast<std::ptrdiff_t>(index));
      ++processed;
      continue;
    }

    const auto locator = locator_it->second;
    auto map_it = maps_.find(locator.map_id);
    if (map_it == maps_.end()) {
      remove_legacy_group_member(session_id);
      session_index_.erase(locator_it);
      run_user_order_.erase(run_user_order_.begin() + static_cast<std::ptrdiff_t>(index));
      ++processed;
      continue;
    }

    append_dispatch(dispatch,
                    map_it->second->legacy_process_player(locator.actor_id, current_tick_,
                                                          now_ms,
                                                          context.persistence_overloaded,
                                                          player_input_budget_per_tick));
    const auto state = map_it->second->legacy_player_state(locator.actor_id);
    if (!state.has_value() || *state == LegacyPlayerState::closed) {
      close_records_[util::lower_copy(locator.character_name)] =
          CloseRecord{session_id, locator.account_id, locator.character_name, now_ms,
                      "closed"};
      remove_legacy_group_member(session_id);
      session_index_.erase(session_id);
      run_user_order_.erase(run_user_order_.begin() + static_cast<std::ptrdiff_t>(index));
    } else {
      hum_cur_ = (index + 1) % run_user_order_.size();
    }

    ++processed;
    if (budget_ms > 0) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - started)
                               .count();
      if (elapsed >= budget_ms) {
        break;
      }
    }
  }
}

void LogicRuntime::prune_monster_group(MonsterGroup& group) {
  group.monsters.erase(
      std::remove_if(group.monsters.begin(), group.monsters.end(),
                     [&](const ActorRef& ref) {
                       const auto map_it = maps_.find(ref.map_id);
                       return map_it == maps_.end() ||
                              !map_it->second->legacy_monster_alive(ref.actor_id);
                      }),
      group.monsters.end());
}

std::int32_t LogicRuntime::legacy_monster_live_count_for_spawn(
    const MonsterGroup& group) const {
  std::int32_t count = 0;
  for (const auto& ref : group.monsters) {
    const auto map_it = maps_.find(ref.map_id);
    if (map_it != maps_.end() && map_it->second->legacy_monster_counts_for_spawn(ref.actor_id)) {
      ++count;
    }
  }
  return count;
}

std::uint64_t LogicRuntime::legacy_zen_time_ms(std::uint32_t zen_time_ms) const {
  constexpr std::uint64_t kLegacyFastZenLimitMs = 30ULL * 60ULL * 1000ULL;
  auto result = static_cast<std::uint64_t>(zen_time_ms);
  if (result >= kLegacyFastZenLimitMs) {
    return result;
  }

  const auto user_full = std::max(config_.runtime.legacy_user_full_count, 0);
  const auto online = static_cast<std::int32_t>(
      std::min<std::size_t>(session_index_.size(), static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())));
  if (online <= user_full) {
    return result;
  }

  const auto fast_step = std::max(config_.runtime.legacy_zen_fast_step, 1);
  auto ratio = static_cast<double>(online - user_full) / static_cast<double>(fast_step);
  ratio = std::clamp(ratio, 0.0, 6.0);
  const auto delta = static_cast<std::uint64_t>(
      std::llround((static_cast<double>(result) / 10.0) * ratio));
  return delta < result ? result - delta : 0;
}

void LogicRuntime::roll_legacy_monster_items_for_spawn(const MonsterGroup& group,
                                                       ActorMail& mail,
                                                       std::uint64_t now_ms,
                                                       RuntimeDispatch* dispatch) {
  const auto drops_it = monster_drops_.find(normalized_key(group.name));
  if (drops_it == monster_drops_.end()) {
    return;
  }

  for (const auto& drop : drops_it->second) {
    if (drop.max_point <= 0 || drop.sel_point < legacy_random_.random(drop.max_point)) {
      continue;
    }
    const auto item_key = normalized_key(drop.item_name);
    if (is_legacy_gold_name(item_key)) {
      mail.monster_drop_gold +=
          drop.count / 2 + legacy_random_.random(std::max(drop.count, 1));
      continue;
    }

    const auto item_it = item_configs_by_name_.find(item_key);
    if (item_it == item_configs_by_name_.end()) {
      if (dispatch != nullptr) {
        LegacyRuntimeTrace trace;
        trace.stage = "MonsterDrop";
        trace.action = "unknown_item";
        trace.map_id = group.map_id;
        trace.object_name = group.name;
        trace.actor_id = mail.actor_id;
        trace.now_ms = now_ms;
        trace.current_tick = current_tick_;
        trace.command = drop.item_name;
        trace.success = false;
        dispatch->legacy_traces.push_back(std::move(trace));
      }
      continue;
    }

    for (std::int32_t index = 0; index < drop.count; ++index) {
      LegacyUserItem item;
      item.index = static_cast<std::uint16_t>(std::clamp(item_it->second.id, 0, 65535));
      item.make_index = make_index_allocator_.allocate();
      const auto dura_max = std::clamp(
          item_it->second.dura_max > 0 ? item_it->second.dura_max : 1000, 1, 65535);
      item.dura_max = static_cast<std::uint16_t>(dura_max);
      item.dura = static_cast<std::uint16_t>(
          std::clamp(dura_max * (20 + legacy_random_.random(80)) / 100, 1, 65535));
      if (legacy_random_.random(10) == 0) {
        legacy_random_upgrade_monster_drop_item(item_it->second, item, legacy_random_);
      }
      legacy_random_set_unknown_monster_drop_item(item_it->second, item, legacy_random_);
      mail.monster_drop_items.push_back(item);
    }
  }
}

ActorMail LogicRuntime::make_monster_spawn_mail(const MonsterGroup& group,
                                                std::uint64_t actor_id,
                                                std::int32_t x,
                                                std::int32_t y,
                                                std::uint64_t now_ms,
                                                RuntimeDispatch* dispatch) {
  const auto& spawn = group.spawn;
  const auto def_it = monster_defs_.find(normalized_key(group.name));
  const auto* def = def_it != monster_defs_.end() ? &def_it->second : nullptr;

  ActorMail mail;
  mail.kind = ActorMailKind::spawn_monster;
  mail.map_id = group.map_id;
  mail.actor_id = actor_id;
  mail.name = group.name;
  mail.x = x;
  mail.y = y;
  mail.level = def != nullptr ? def->level : spawn.level;
  mail.max_hp = def != nullptr ? def->hp : spawn.max_hp;
  mail.max_mp = def != nullptr ? def->mp : 0;
  mail.dc_min = def != nullptr ? std::max(def->dc, 0) : std::max(spawn.attack_power, 0);
  mail.dc_max =
      def != nullptr ? std::max(def->dc_max > 0 ? def->dc_max : def->dc, mail.dc_min)
                     : std::max(spawn.attack_power, 0);
  mail.attack_power = std::max(mail.dc_max, 1);
  mail.defense = def != nullptr ? def->ac : spawn.defense;
  mail.magic_defense = def != nullptr ? def->mac : spawn.magic_defense;
  mail.mc = def != nullptr ? def->mc : 0;
  mail.sc = def != nullptr ? def->sc : 0;
  mail.exp_reward = def != nullptr ? def->exp : spawn.exp_reward;
  mail.life_attrib = def != nullptr && def->undead ? 1 : spawn.life_attrib;
  mail.monster_tameable = def != nullptr ? def->tameable : spawn.tameable;
  mail.race_server = def != nullptr ? def->race_server : 0;
  mail.race_image = def != nullptr ? def->race_image : 0;
  mail.appearance = def != nullptr ? def->appearance : 0;
  mail.cool_eye = def != nullptr ? def->cool_eye : 0;
  mail.speed = def != nullptr ? def->agility : 0;
  mail.accuracy = def != nullptr ? def->accurate : 0;
  mail.walk_speed_ms = def != nullptr ? std::max(def->walk_speed_ms, 200) : 20;
  mail.walk_step = def != nullptr ? std::max(def->walk_step, 1) : 1;
  mail.walk_wait_ms = def != nullptr ? std::max(def->walk_wait_ms, 0) : 0;
  mail.attack_speed_ms = def != nullptr ? std::max(def->attack_speed_ms, 200) : 100;
  mail.monster_ai_profile =
      def != nullptr ? infer_monster_ai_profile(*def) : MonsterAiProfile::basic;
  mail.respawn_ms = group.respawn_ms;
  mail.legacy_spawn_group = group.legacy_group;
  mail.home_x = group.x;
  mail.home_y = group.y;
  mail.home_area = group.area;

  roll_legacy_monster_items_for_spawn(group, mail, now_ms, dispatch);

  if (mail.monster_ai_profile == MonsterAiProfile::aggressive) {
    mail.monster_search_rate_ms =
        1500 + static_cast<std::uint64_t>(legacy_random_.random(1500));
  } else {
    mail.monster_search_rate_ms =
        3000 + static_cast<std::uint64_t>(legacy_random_.random(2000));
  }
  mail.dir = static_cast<std::uint8_t>(legacy_random_.random(8));

  return mail;
}

std::int32_t LogicRuntime::spawn_legacy_gm_monsters(
    RuntimeDispatch& dispatch, std::string map_id, std::int32_t x, std::int32_t y,
    std::string monster_name, std::int32_t count, std::uint64_t now_ms,
    std::uint64_t master_actor_id, std::uint8_t slave_exp_level,
    std::optional<std::pair<std::int32_t, std::int32_t>> target_xy) {
  const auto resolved_map_id = resolve_map_id(map_id);
  const auto map_it = maps_.find(resolved_map_id);
  if (map_it == maps_.end() || monster_defs_.find(normalized_key(monster_name)) == monster_defs_.end()) {
    return 0;
  }
  if (!map_it->second->legacy_can_spawn_monster(x, y)) {
    return 0;
  }

  MonsterGroup group;
  group.name = std::move(monster_name);
  group.map_id = resolved_map_id;
  group.x = x;
  group.y = y;
  group.area = 0;
  group.count = std::clamp(count, 1, 500);
  group.spawn.map_id = resolved_map_id;
  group.spawn.name = group.name;
  group.spawn.x = x;
  group.spawn.y = y;
  group.legacy_group = false;
  group.respawn_ms = 0;
  group.start_time_ms = now_ms;

  auto spawned = 0;
  for (auto index = 0; index < group.count; ++index) {
    const auto actor_id = next_actor_id_++;
    auto mail = make_monster_spawn_mail(group, actor_id, x, y, now_ms, &dispatch);
    mail.legacy_spawn_group = true;
    mail.monster_no_item = true;
    if (master_actor_id != 0) {
      mail.master_actor_id = master_actor_id;
      mail.monster_is_slave = true;
      mail.slave_make_level = 3;
      mail.slave_exp_level = slave_exp_level;
      mail.master_royalty_time_ms = now_ms + 24ULL * 60ULL * 60ULL * 1000ULL;
    }
    if (target_xy.has_value()) {
      mail.monster_has_target_xy = true;
      mail.monster_target_x = target_xy->first;
      mail.monster_target_y = target_xy->second;
    }
    map_it->second->enqueue_mail(mail);
    group.monsters.push_back(ActorRef{resolved_map_id, actor_id, group.name});
    ++spawned;
  }
  monster_groups_.push_back(std::move(group));
  return spawned;
}

void LogicRuntime::process_monster_spawn_group(std::size_t group_index,
                                               std::uint64_t now_ms,
                                               RuntimeDispatch& dispatch) {
  if (group_index >= monster_groups_.size()) {
    return;
  }
  auto& group = monster_groups_[group_index];
  if (!group.legacy_group) {
    return;
  }
  prune_monster_group(group);
  const auto due_zen_ms = legacy_zen_time_ms(group.zen_time_ms);
  if (group.start_time_ms != 0 && group.zen_time_ms != 0 &&
      now_ms <= group.start_time_ms + due_zen_ms) {
    return;
  }

  const auto live_count = legacy_monster_live_count_for_spawn(group);
  if (live_count >= group.count) {
    group.start_time_ms = now_ms;
    return;
  }

  const auto map_it = maps_.find(group.map_id);
  if (map_it == maps_.end()) {
    return;
  }

  constexpr std::array<std::pair<std::int32_t, std::int32_t>, 30> relocate_offsets{{
      {0, 0},   {1, 0},   {-1, 0},  {0, 1},   {0, -1},  {1, 1},
      {-1, 1},  {1, -1},  {-1, -1}, {2, 0},   {-2, 0},  {0, 2},
      {0, -2},  {2, 1},   {-2, 1},  {2, -1},  {-2, -1}, {1, 2},
      {-1, 2},  {1, -2},  {-1, -2}, {2, 2},   {-2, 2},  {2, -2},
      {-2, -2}, {3, 0},   {-3, 0},  {0, 3},   {0, -3},  {3, 1},
  }};

  const auto missing = group.count - live_count;
  const auto area_span = group.area * 2 + 1;
  const auto clustered =
      group.small_zen_rate > 0 && legacy_random_.random(100) < group.small_zen_rate;
  auto cluster_x = group.x;
  auto cluster_y = group.y;
  if (clustered) {
    cluster_x = group.x - group.area + legacy_random_.random(std::max(area_span, 1));
    cluster_y = group.y - group.area + legacy_random_.random(std::max(area_span, 1));
  }

  for (std::int32_t index = 0; index < missing; ++index) {
    const auto base_x = clustered
                            ? cluster_x - 10 + legacy_random_.random(20)
                            : group.x - group.area + legacy_random_.random(std::max(area_span, 1));
    const auto base_y = clustered
                            ? cluster_y - 10 + legacy_random_.random(20)
                            : group.y - group.area + legacy_random_.random(std::max(area_span, 1));
    std::optional<std::pair<std::int32_t, std::int32_t>> chosen;
    for (const auto& [dx, dy] : relocate_offsets) {
      const auto try_x = base_x + dx;
      const auto try_y = base_y + dy;
      if (map_it->second->legacy_can_spawn_monster(try_x, try_y)) {
        chosen = std::pair{try_x, try_y};
        break;
      }
    }
    if (!chosen.has_value()) {
      LegacyRuntimeTrace trace;
      trace.stage = "MonsterSpawn";
      trace.action = "blocked";
      trace.map_id = group.map_id;
      trace.object_name = group.name;
      trace.now_ms = now_ms;
      trace.current_tick = current_tick_;
      trace.cursor = group_index;
      trace.success = false;
      dispatch.legacy_traces.push_back(std::move(trace));
      continue;
    }

    const auto actor_id = next_actor_id_++;
    auto mail =
        make_monster_spawn_mail(group, actor_id, chosen->first, chosen->second, now_ms, &dispatch);
    map_it->second->enqueue_mail(std::move(mail));
    group.monsters.push_back(ActorRef{group.map_id, actor_id, group.name});
    LegacyRuntimeTrace trace;
    trace.stage = "MonsterSpawn";
    trace.action = "spawned";
    trace.map_id = group.map_id;
    trace.object_name = group.name;
    trace.actor_id = actor_id;
    trace.now_ms = now_ms;
    trace.current_tick = current_tick_;
    trace.cursor = group_index;
    trace.value = chosen->first;
    trace.damage = chosen->second;
    trace.success = true;
    dispatch.legacy_traces.push_back(std::move(trace));
  }
  group.start_time_ms = now_ms;
}

void LogicRuntime::process_monsters(std::uint64_t now_ms, RuntimeDispatch& dispatch) {
  add_stage_trace(dispatch, "ProcessMonsters", "begin", now_ms, mon_cur_, mon_sub_cur_);
  if (monster_groups_.empty()) {
    mon_cur_ = 0;
    mon_sub_cur_ = 0;
    gen_cur_ = 0;
    one_zen_time_initialized_ = false;
    return;
  }

  constexpr std::uint64_t kZenIntervalMs = 200;
  if (!one_zen_time_initialized_) {
    one_zen_time_ms_ = now_ms;
    one_zen_time_initialized_ = true;
  } else if (now_ms > one_zen_time_ms_ + kZenIntervalMs) {
    one_zen_time_ms_ = now_ms;
    add_stage_trace(dispatch, "ProcessMonsters", "gen_check", now_ms, gen_cur_, 0);
    process_monster_spawn_group(gen_cur_, now_ms, dispatch);
    gen_cur_ = gen_cur_ + 1 < monster_groups_.size() ? gen_cur_ + 1 : 0;
  }

  if (mon_cur_ >= monster_groups_.size()) {
    mon_cur_ = 0;
    mon_sub_cur_ = 0;
  }

  const auto started = std::chrono::steady_clock::now();
  const auto budget_ms = static_cast<std::int64_t>(config_.budgets.monster_budget_ms);
  bool lack = false;
  std::size_t processed = 0;

  std::size_t i = mon_cur_;
  for (; i < monster_groups_.size(); ++i) {
    auto& group = monster_groups_[i];
    std::size_t k = mon_sub_cur_ < group.monsters.size() ? mon_sub_cur_ : 0;
    mon_sub_cur_ = 0;
    for (; k < group.monsters.size(); ++k) {
      const auto& monster = group.monsters[k];
      if (auto map_it = maps_.find(monster.map_id); map_it != maps_.end()) {
        append_dispatch(dispatch,
                        map_it->second->legacy_process_monster(monster.actor_id, current_tick_,
                                                               now_ms, i, k));
      }
      ++processed;
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - started)
                               .count();
      if ((budget_ms == 0 && processed > 0) || (budget_ms > 0 && elapsed > budget_ms)) {
        lack = true;
        const auto next_sub = k + 1;
        if (next_sub >= group.monsters.size()) {
          mon_cur_ = i + 1;
          mon_sub_cur_ = 0;
        } else {
          mon_cur_ = i;
          mon_sub_cur_ = next_sub;
        }
        break;
      }
    }
    if (lack) {
      break;
    }
  }

  if (lack) {
    if (mon_cur_ >= monster_groups_.size()) {
      mon_cur_ = 0;
      mon_sub_cur_ = 0;
    }
    add_stage_trace(dispatch, "ProcessMonsters", "budget_exhausted", now_ms,
                    mon_cur_, mon_sub_cur_);
  } else {
    mon_cur_ = 0;
    mon_sub_cur_ = 0;
    add_stage_trace(dispatch, "ProcessMonsters", "complete", now_ms, mon_cur_,
                    mon_sub_cur_);
  }
}

void LogicRuntime::process_merchants(std::uint64_t now_ms, RuntimeDispatch& dispatch) {
  add_stage_trace(dispatch, "ProcessMerchants", "begin", now_ms, mer_cur_, 0);
  if (merchant_order_.empty()) {
    mer_cur_ = 0;
    return;
  }
  if (mer_cur_ >= merchant_order_.size()) {
    mer_cur_ = 0;
  }

  const auto started = std::chrono::steady_clock::now();
  const auto budget_ms = static_cast<std::int64_t>(config_.budgets.npc_budget_ms);
  std::size_t processed = 0;
  bool lack = false;
  std::size_t i = mer_cur_;
  for (; i < merchant_order_.size(); ++i) {
    const auto& merchant = merchant_order_[i];
    if (auto map_it = maps_.find(merchant.map_id); map_it != maps_.end()) {
      append_dispatch(dispatch,
                      map_it->second->legacy_process_merchant(merchant.actor_id, current_tick_,
                                                              now_ms, i));
    }
    ++processed;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();
    if ((budget_ms == 0 && processed > 0) || (budget_ms > 0 && elapsed >= budget_ms)) {
      lack = true;
      break;
    }
  }
  mer_cur_ = lack ? i : 0;
  if (lack && mer_cur_ + 1 < merchant_order_.size()) {
    ++mer_cur_;
  } else if (lack) {
    mer_cur_ = 0;
  }
}

void LogicRuntime::process_npcs(std::uint64_t now_ms, RuntimeDispatch& dispatch) {
  add_stage_trace(dispatch, "ProcessNpcs", "begin", now_ms, npc_cur_, 0);
  if (npc_order_.empty()) {
    npc_cur_ = 0;
    return;
  }
  if (npc_cur_ >= npc_order_.size()) {
    npc_cur_ = 0;
  }

  const auto started = std::chrono::steady_clock::now();
  const auto budget_ms = static_cast<std::int64_t>(config_.budgets.npc_budget_ms);
  std::size_t processed = 0;
  bool lack = false;
  std::size_t i = npc_cur_;
  for (; i < npc_order_.size(); ++i) {
    const auto& npc = npc_order_[i];
    if (auto map_it = maps_.find(npc.map_id); map_it != maps_.end()) {
      append_dispatch(dispatch,
                      map_it->second->legacy_process_npc(npc.actor_id, current_tick_, now_ms,
                                                         i));
    }
    ++processed;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();
    if ((budget_ms == 0 && processed > 0) || (budget_ms > 0 && elapsed >= budget_ms)) {
      lack = true;
      break;
    }
  }
  npc_cur_ = lack ? i : 0;
  if (lack && npc_cur_ + 1 < npc_order_.size()) {
    ++npc_cur_;
  } else if (lack) {
    npc_cur_ = 0;
  }
}

void LogicRuntime::process_user_engine_timers(std::uint64_t now_ms, RuntimeDispatch& dispatch) {
  constexpr std::uint64_t kMissionIntervalMs = 1000;
  constexpr std::uint64_t kDoorIntervalMs = 500;
  constexpr std::uint64_t kTimer10SecMs = 10ULL * 1000ULL;
  constexpr std::uint64_t kTimer10MinMs = 10ULL * 60ULL * 1000ULL;

  if (!user_engine_timers_initialized_) {
    mission_time_ms_ = now_ms;
    open_door_check_ms_ = now_ms;
    timer10min_ms_ = now_ms;
    timer10sec_ms_ = now_ms;
    user_engine_timers_initialized_ = true;
    return;
  }

  if (elapsed_gt(now_ms, mission_time_ms_, kMissionIntervalMs)) {
    mission_time_ms_ = now_ms;
    add_stage_trace(dispatch, "LegacyMission", "ProcessMissions", now_ms, 0, 0);
    add_stage_trace(dispatch, "LegacyMission", "CheckServerWaitTimeOut", now_ms, 0, 0);
    add_stage_trace(dispatch, "LegacyMission", "CheckHolySeizeValid", now_ms, 0, 0);
  }

  if (elapsed_gt(now_ms, open_door_check_ms_, kDoorIntervalMs)) {
    open_door_check_ms_ = now_ms;
    add_stage_trace(dispatch, "LegacyTimer", "DoorTimer", now_ms, 0, 0);
    for (const auto& map_id : map_order_) {
      auto map_it = maps_.find(map_id);
      if (map_it == maps_.end()) {
        continue;
      }
      append_dispatch(dispatch, map_it->second->close_expired_doors(now_ms));
    }
  }

  if (elapsed_gt(now_ms, timer10min_ms_, kTimer10MinMs)) {
    timer10min_ms_ = now_ms;
    add_stage_trace(dispatch, "LegacyTimer", "Timer10Min", now_ms, 0, 0);
    add_stage_trace(dispatch, "LegacyTimer", "NoticeMan.RefreshNoticeList", now_ms, 0, 0);
    add_stage_trace(dispatch, "LegacyTimer", "UserCastle.SaveAll", now_ms, 0, 0);
  }

  if (elapsed_gt(now_ms, timer10sec_ms_, kTimer10SecMs)) {
    timer10sec_ms_ = now_ms;
    add_stage_trace(dispatch, "LegacyTimer", "Timer10Sec", now_ms, 0, 0);
    add_stage_trace(dispatch, "LegacyTimer", "FrmIDSoc.SendUserCount", now_ms, 0, 0);
    add_stage_trace(dispatch, "LegacyTimer", "GuildMan.CheckGuildWarTimeOut", now_ms, 0, 0);
    add_stage_trace(dispatch, "LegacyTimer", "UserCastle.Run", now_ms, 0, 0);
    add_stage_trace(dispatch, "LegacyTimer", "ShutUpList.Cleanup", now_ms, 0, 0);
    for (auto it = legacy_shut_up_list_.begin(); it != legacy_shut_up_list_.end();) {
      if (now_ms > it->second.expire_ms) {
        it = legacy_shut_up_list_.erase(it);
      } else {
        ++it;
      }
    }
  }
}

void LogicRuntime::cleanup_close_records(std::uint64_t now_ms) {
  constexpr std::uint64_t kCloseRecordTtlMs = 5ULL * 60ULL * 1000ULL;
  for (auto it = close_records_.begin(); it != close_records_.end();) {
    if (now_ms >= it->second.closed_ms + kCloseRecordTtlMs) {
      it = close_records_.erase(it);
    } else {
      ++it;
    }
  }
}

std::optional<std::pair<std::string, std::uint64_t>> LogicRuntime::locate_character_actor(
    std::string_view character_name) const {
  for (const auto& [_, locator] : session_index_) {
    if (util::lower_copy(locator.character_name) == util::lower_copy(character_name)) {
      return std::make_pair(locator.map_id, locator.actor_id);
    }
  }
  return std::nullopt;
}

std::optional<CharacterRecord> LogicRuntime::snapshot_character_actor(
    std::string_view character_name) const {
  const auto located = locate_character_actor(character_name);
  if (!located.has_value()) {
    return std::nullopt;
  }

  const auto map_it = maps_.find(located->first);
  if (map_it == maps_.end()) {
    return std::nullopt;
  }
  return map_it->second->snapshot_player(located->second);
}

std::vector<CharacterRecord> LogicRuntime::snapshot_online_characters() {
  std::vector<CharacterRecord> characters;
  characters.reserve(session_index_.size());
  for (const auto& [_, locator] : session_index_) {
    const auto map_it = maps_.find(locator.map_id);
    if (map_it == maps_.end()) {
      continue;
    }
    if (auto character = map_it->second->persistent_snapshot_player(locator.actor_id, last_now_ms_);
        character.has_value()) {
      characters.push_back(*character);
    }
  }
  return characters;
}

std::optional<MonsterSnapshot> LogicRuntime::legacy_monster_snapshot(
    std::string_view map_id, std::uint64_t actor_id) const {
  const auto map_it = maps_.find(std::string(map_id));
  if (map_it == maps_.end()) {
    return std::nullopt;
  }
  return map_it->second->legacy_monster_snapshot(actor_id);
}

std::optional<LegacyPlayerState> LogicRuntime::legacy_session_state(
    std::uint64_t session_id) const {
  const auto locator_it = session_index_.find(session_id);
  if (locator_it == session_index_.end()) {
    return std::nullopt;
  }
  const auto map_it = maps_.find(locator_it->second.map_id);
  if (map_it == maps_.end()) {
    return std::nullopt;
  }
  return map_it->second->legacy_player_state(locator_it->second.actor_id);
}

std::size_t LogicRuntime::legacy_session_inbox_size(std::uint64_t session_id) const {
  const auto locator_it = session_index_.find(session_id);
  if (locator_it == session_index_.end()) {
    return 0;
  }
  const auto map_it = maps_.find(locator_it->second.map_id);
  if (map_it == maps_.end()) {
    return 0;
  }
  return map_it->second->legacy_player_inbox_size(locator_it->second.actor_id);
}

std::vector<std::uint64_t> LogicRuntime::legacy_session_inbox_sequences(
    std::uint64_t session_id) const {
  const auto locator_it = session_index_.find(session_id);
  if (locator_it == session_index_.end()) {
    return {};
  }
  const auto map_it = maps_.find(locator_it->second.map_id);
  if (map_it == maps_.end()) {
    return {};
  }
  return map_it->second->legacy_player_inbox_session_sequences(locator_it->second.actor_id);
}

std::int64_t LogicRuntime::legacy_session_run_time_ms(std::uint64_t session_id) const {
  const auto locator_it = session_index_.find(session_id);
  if (locator_it == session_index_.end()) {
    return 0;
  }
  const auto map_it = maps_.find(locator_it->second.map_id);
  if (map_it == maps_.end()) {
    return 0;
  }
  return map_it->second->legacy_player_run_time_ms(locator_it->second.actor_id);
}

std::string LogicRuntime::resolve_map_id(const std::string& requested_map) const {
  if (!requested_map.empty() && maps_.contains(requested_map)) {
    return requested_map;
  }
  return default_map_id_;
}

void LogicRuntime::append_dispatch(RuntimeDispatch& target, RuntimeDispatch source) {
  target.session_events.insert(target.session_events.end(),
                               std::make_move_iterator(source.session_events.begin()),
                               std::make_move_iterator(source.session_events.end()));
  target.audit_events.insert(target.audit_events.end(),
                             std::make_move_iterator(source.audit_events.begin()),
                             std::make_move_iterator(source.audit_events.end()));
  target.persist_requests.insert(target.persist_requests.end(),
                                 std::make_move_iterator(source.persist_requests.begin()),
                                 std::make_move_iterator(source.persist_requests.end()));
  target.cross_map_mails.insert(target.cross_map_mails.end(),
                                std::make_move_iterator(source.cross_map_mails.begin()),
                                std::make_move_iterator(source.cross_map_mails.end()));
  target.legacy_event_creates.insert(
      target.legacy_event_creates.end(),
      std::make_move_iterator(source.legacy_event_creates.begin()),
      std::make_move_iterator(source.legacy_event_creates.end()));
  target.legacy_holy_curtain_groups.insert(
      target.legacy_holy_curtain_groups.end(),
      std::make_move_iterator(source.legacy_holy_curtain_groups.begin()),
      std::make_move_iterator(source.legacy_holy_curtain_groups.end()));
  target.legacy_random_space_moves.insert(
      target.legacy_random_space_moves.end(),
      std::make_move_iterator(source.legacy_random_space_moves.begin()),
      std::make_move_iterator(source.legacy_random_space_moves.end()));
  target.legacy_traces.insert(target.legacy_traces.end(),
                              std::make_move_iterator(source.legacy_traces.begin()),
                              std::make_move_iterator(source.legacy_traces.end()));
}

}  // namespace mir2
