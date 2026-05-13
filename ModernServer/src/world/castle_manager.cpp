#include "world/castle_manager.hpp"

#include <algorithm>
#include <utility>

#include "util/string_utils.hpp"

namespace mir2 {

namespace {

bool equals_name(std::string_view lhs, std::string_view rhs) {
  return util::lower_copy(lhs) == util::lower_copy(rhs);
}

std::string normalize_name(std::string name) {
  return util::trim(std::move(name));
}

}  // namespace

CastleManager::CastleManager(std::string castle_name)
    : castle_name_(normalize_name(std::move(castle_name))) {}

void CastleManager::set_owner(std::string guild_name, std::string lord_name) {
  owner_guild_ = normalize_name(std::move(guild_name));
  owner_lord_ = normalize_name(std::move(lord_name));
}

CastleWarOpResult CastleManager::propose_castle_war(std::string guild_name,
                                                    bool requester_is_lord,
                                                    bool has_zuma_piece,
                                                    std::int32_t current_day,
                                                    std::int32_t delay_days) {
  guild_name = normalize_name(std::move(guild_name));
  if (guild_name.empty()) {
    return CastleWarOpResult::empty_guild;
  }
  if (!requester_is_lord) {
    return CastleWarOpResult::requester_not_lord;
  }
  if (equals_name(guild_name, owner_guild_)) {
    return CastleWarOpResult::already_owner;
  }
  if (!has_zuma_piece) {
    return CastleWarOpResult::missing_zuma_piece;
  }
  if (is_registered(guild_name)) {
    return CastleWarOpResult::already_registered;
  }
  registrations_.push_back(CastleWarRegistration{std::move(guild_name),
                                                 current_day + delay_days});
  return CastleWarOpResult::ok;
}

CastleWarOpResult CastleManager::start_due_war(std::int32_t current_day,
                                               std::uint64_t now_ms,
                                               std::vector<CastleWarEvent>& events) {
  if (under_attack_) {
    return CastleWarOpResult::already_under_attack;
  }

  std::vector<std::string> attackers;
  for (auto it = registrations_.begin(); it != registrations_.end();) {
    if (it->attack_day == current_day) {
      attackers.push_back(it->guild_name);
      it = registrations_.erase(it);
    } else {
      ++it;
    }
  }
  if (attackers.empty()) {
    return CastleWarOpResult::no_due_war;
  }

  rush_guilds_ = std::move(attackers);
  if (!owner_guild_.empty() && !is_rush_guild(owner_guild_)) {
    rush_guilds_.push_back(owner_guild_);
  }
  under_attack_ = true;
  timeout_warning_sent_ = false;
  latest_war_start_ms_ = now_ms;
  castle_attack_started_ms_ = now_ms;
  events.push_back(CastleWarEvent{CastleWarEventType::start, castle_name_});
  return CastleWarOpResult::ok;
}

CastleWarOpResult CastleManager::try_occupy(
    std::string guild_name, const std::vector<CastleCoreOccupant>& core_occupants,
    std::uint64_t now_ms, std::vector<CastleWarEvent>& events) {
  guild_name = normalize_name(std::move(guild_name));
  if (!under_attack_) {
    return CastleWarOpResult::not_under_attack;
  }
  if (guild_name.empty()) {
    return CastleWarOpResult::empty_guild;
  }
  if (equals_name(guild_name, owner_guild_)) {
    return CastleWarOpResult::already_owner;
  }
  if (!is_rush_guild(guild_name)) {
    return CastleWarOpResult::guild_not_registered;
  }
  if (now_ms < castle_attack_started_ms_ + kOccupationDelayMs) {
    return CastleWarOpResult::occupation_too_early;
  }

  bool candidate_present = false;
  for (const auto& occupant : core_occupants) {
    if (!occupant.alive) {
      continue;
    }
    if (!equals_name(occupant.guild_name, guild_name)) {
      return CastleWarOpResult::core_not_controlled;
    }
    candidate_present = true;
  }
  if (!candidate_present) {
    return CastleWarOpResult::core_not_controlled;
  }

  owner_guild_ = guild_name;
  owner_lord_.clear();
  events.push_back(CastleWarEvent{CastleWarEventType::owner_changed, owner_guild_});
  if (rush_guilds_.size() <= 1) {
    finish_war(events);
  }
  return CastleWarOpResult::ok;
}

void CastleManager::run(std::uint64_t now_ms, std::vector<CastleWarEvent>& events) {
  if (!under_attack_) {
    return;
  }
  if (!timeout_warning_sent_ &&
      now_ms >= latest_war_start_ms_ + kWarDurationMs - kTimeoutWarningLeadMs) {
    timeout_warning_sent_ = true;
    events.push_back(CastleWarEvent{CastleWarEventType::timeout_warning, castle_name_});
  }
  if (now_ms >= latest_war_start_ms_ + kWarDurationMs) {
    finish_war(events);
  }
}

void CastleManager::finish_war(std::vector<CastleWarEvent>& events) {
  if (!under_attack_) {
    return;
  }
  under_attack_ = false;
  rush_guilds_.clear();
  events.push_back(CastleWarEvent{CastleWarEventType::finish, castle_name_});
}

bool CastleManager::is_registered(std::string_view guild_name) const {
  return std::any_of(registrations_.begin(), registrations_.end(),
                     [&](const CastleWarRegistration& registration) {
                       return equals_name(registration.guild_name, guild_name);
                     });
}

bool CastleManager::is_rush_guild(std::string_view guild_name) const {
  return std::any_of(rush_guilds_.begin(), rush_guilds_.end(),
                     [&](const std::string& rush_guild) {
                       return equals_name(rush_guild, guild_name);
                     });
}

}  // namespace mir2
