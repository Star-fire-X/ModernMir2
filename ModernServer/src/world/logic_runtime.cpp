#include "world/logic_runtime.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iterator>
#include <limits>

#include "spdlog/spdlog.h"
#include "util/string_utils.hpp"
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

  for (const auto& map : config_.maps) {
    auto [map_it, inserted] = maps_.emplace(
        map.id, std::make_unique<MapActor>(map, config_.budgets, item_configs_, magic_configs_,
                                           config_.map_quests, castle_dialog_context_,
                                           monster_defs_, &make_index_allocator_));
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
    default:
      break;
  }
  return mail;
}

bool LogicRuntime::is_merchant_npc_config(const NpcConfig& npc, const ActorMail& mail) const {
  const auto service = util::lower_copy(npc.service);
  return !npc.merchant_goods.empty() || !npc.merchant_products.empty() ||
         !mail.merchant_items.empty() ||
         service.find("buy") != std::string::npos ||
         service.find("sell") != std::string::npos ||
         service.find("repair") != std::string::npos ||
         service.find("storage") != std::string::npos ||
         service.find("merchant") != std::string::npos ||
         service.find("shop") != std::string::npos;
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
    case LogicCommandKind::logout: {
      auto it = session_index_.find(command.session_id);
      if (it == session_index_.end()) {
        break;
      }

      if (command.kind == LogicCommandKind::logout) {
        append_dispatch(dispatch,
                        mark_session_disconnected(command.session_id, "logout"));
      } else {
        const auto mail = make_player_mail(command, it->second);
        if (auto map_it = maps_.find(it->second.map_id); map_it != maps_.end()) {
          static_cast<void>(map_it->second->enqueue_legacy_player_command(mail, last_now_ms_));
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
  if (auto map_it = maps_.find(locator_it->second.map_id); map_it != maps_.end()) {
    append_dispatch(dispatch, map_it->second->legacy_disconnect_player(locator_it->second.actor_id,
                                                                       last_now_ms_));
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
          session_index_[cross_map_mail.session_id] =
              ActorLocator{target_map_id, cross_map_mail.actor_id,
                           cross_map_mail.character.account_id,
                           cross_map_mail.character.character_name};
        }
      }
    }
  }
  return combined;
}

RuntimeDispatch LogicRuntime::run_legacy_event_manager(std::uint64_t now_ms) {
  auto result = legacy_event_manager_.run(now_ms, current_tick_);
  RuntimeDispatch dispatch = std::move(result.dispatch);
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
  if (auto map_it = maps_.find(record.map_id); map_it != maps_.end()) {
    static_cast<void>(map_it->second->legacy_add_event_object(
        event_id, record.x, record.y, last_now_ms_));
  }
  return event_id;
}

std::optional<LegacyEventRecord> LogicRuntime::find_legacy_event(
    const std::string& map_id, std::int32_t x, std::int32_t y,
    LegacyEventType type) const {
  return legacy_event_manager_.find(resolve_map_id(map_id), x, y, type);
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

    session_index_[ready.session_id] =
        ActorLocator{character.map_id, mail.actor_id, character.account_id,
                     character.character_name};
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
      session_index_.erase(locator_it);
      run_user_order_.erase(run_user_order_.begin() + static_cast<std::ptrdiff_t>(index));
      ++processed;
      continue;
    }

    append_dispatch(dispatch,
                    map_it->second->legacy_process_player(locator.actor_id, current_tick_,
                                                          now_ms,
                                                          context.persistence_overloaded));
    const auto state = map_it->second->legacy_player_state(locator.actor_id);
    if (!state.has_value() || *state == LegacyPlayerState::closed) {
      close_records_[util::lower_copy(locator.character_name)] =
          CloseRecord{session_id, locator.account_id, locator.character_name, now_ms,
                      "closed"};
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
    return;
  }

  constexpr std::uint64_t kZenIntervalMs = 200;
  if (one_zen_time_ms_ == 0 || now_ms > one_zen_time_ms_ + kZenIntervalMs) {
    one_zen_time_ms_ = now_ms;
    add_stage_trace(dispatch, "ProcessMonsters", "gen_check", now_ms, gen_cur_, 0);
    process_monster_spawn_group(gen_cur_, now_ms, dispatch);
    gen_cur_ = gen_cur_ + 1 < monster_groups_.size() ? gen_cur_ + 1 : 0;
  }

  if (mon_cur_ >= monster_groups_.size()) {
    mon_cur_ = 0;
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
      if ((budget_ms == 0 && processed > 0) || (budget_ms > 0 && elapsed >= budget_ms)) {
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
  } else {
    mon_cur_ = 0;
    mon_sub_cur_ = 0;
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

  if (now_ms > mission_time_ms_ + kMissionIntervalMs) {
    mission_time_ms_ = now_ms;
    add_stage_trace(dispatch, "LegacyMission", "ProcessMissions", now_ms, 0, 0);
    add_stage_trace(dispatch, "LegacyMission", "CheckServerWaitTimeOut", now_ms, 0, 0);
    add_stage_trace(dispatch, "LegacyMission", "CheckHolySeizeValid", now_ms, 0, 0);
  }

  if (now_ms > open_door_check_ms_ + kDoorIntervalMs) {
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

  if (now_ms > timer10min_ms_ + kTimer10MinMs) {
    timer10min_ms_ = now_ms;
    add_stage_trace(dispatch, "LegacyTimer", "Timer10Min", now_ms, 0, 0);
    add_stage_trace(dispatch, "LegacyTimer", "NoticeMan.RefreshNoticeList", now_ms, 0, 0);
    add_stage_trace(dispatch, "LegacyTimer", "UserCastle.SaveAll", now_ms, 0, 0);
  }

  if (now_ms > timer10sec_ms_ + kTimer10SecMs) {
    timer10sec_ms_ = now_ms;
    add_stage_trace(dispatch, "LegacyTimer", "Timer10Sec", now_ms, 0, 0);
    add_stage_trace(dispatch, "LegacyTimer", "FrmIDSoc.SendUserCount", now_ms, 0, 0);
    add_stage_trace(dispatch, "LegacyTimer", "GuildMan.CheckGuildWarTimeOut", now_ms, 0, 0);
    add_stage_trace(dispatch, "LegacyTimer", "UserCastle.Run", now_ms, 0, 0);
    add_stage_trace(dispatch, "LegacyTimer", "ShutUpList.Cleanup", now_ms, 0, 0);
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
  target.legacy_traces.insert(target.legacy_traces.end(),
                              std::make_move_iterator(source.legacy_traces.begin()),
                              std::make_move_iterator(source.legacy_traces.end()));
}

}  // namespace mir2
