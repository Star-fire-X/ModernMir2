#include "world/guild_manager.hpp"

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

Guild::Guild(std::string name) : name_(normalize_name(std::move(name))) {}

std::size_t Guild::member_count() const {
  std::size_t count = 0;
  for (const auto& rank : ranks_) {
    count += rank.members.size();
  }
  return count;
}

std::optional<std::string_view> Guild::lord_name() const {
  if (ranks_.empty() || ranks_.front().rank != kGuildLordRank ||
      ranks_.front().members.empty()) {
    return std::nullopt;
  }
  return std::string_view{ranks_.front().members.front().name};
}

const GuildMember* Guild::find_member(std::string_view name) const {
  for (const auto& rank : ranks_) {
    for (const auto& member : rank.members) {
      if (equals_name(member.name, name)) {
        return &member;
      }
    }
  }
  return nullptr;
}

GuildMember* Guild::find_member(std::string_view name) {
  for (auto& rank : ranks_) {
    for (auto& member : rank.members) {
      if (equals_name(member.name, name)) {
        return &member;
      }
    }
  }
  return nullptr;
}

bool Guild::is_lord(std::string_view name) const {
  const auto lord = lord_name();
  return lord.has_value() && equals_name(*lord, name);
}

bool Guild::add_lord(std::string name, std::string rank_name, std::uint64_t online_actor_id) {
  name = normalize_name(std::move(name));
  rank_name = util::trim(std::move(rank_name));
  if (name.empty() || has_member(name)) {
    return false;
  }
  auto& rank = ensure_rank(kGuildLordRank, std::move(rank_name));
  rank.members.push_back(GuildMember{name, rank.rank, rank.rank_name, online_actor_id});
  return true;
}

bool Guild::add_member(std::string name, std::string rank_name, std::uint64_t online_actor_id) {
  name = normalize_name(std::move(name));
  rank_name = util::trim(std::move(rank_name));
  if (name.empty() || has_member(name)) {
    return false;
  }
  auto& rank = ensure_rank(kGuildDefaultRank, std::move(rank_name));
  rank.members.push_back(GuildMember{name, rank.rank, rank.rank_name, online_actor_id});
  return true;
}

bool Guild::remove_member(std::string_view name) {
  for (auto rank_it = ranks_.begin(); rank_it != ranks_.end(); ++rank_it) {
    auto& members = rank_it->members;
    const auto old_size = members.size();
    members.erase(std::remove_if(members.begin(), members.end(),
                                 [&](const GuildMember& member) {
                                   return equals_name(member.name, name);
                                 }),
                  members.end());
    if (members.size() != old_size) {
      if (members.empty()) {
        ranks_.erase(rank_it);
      }
      return true;
    }
  }
  return false;
}

bool Guild::set_member_online_actor(std::string_view name, std::uint64_t online_actor_id) {
  auto* member = find_member(name);
  if (member == nullptr) {
    return false;
  }
  member->online_actor_id = online_actor_id;
  return true;
}

bool Guild::clear_member_online_actor(std::string_view name, std::uint64_t online_actor_id) {
  auto* member = find_member(name);
  if (member == nullptr || member->online_actor_id != online_actor_id) {
    return false;
  }
  member->online_actor_id = 0;
  return true;
}

void Guild::set_notice_lines(std::vector<std::string> notice_lines) {
  notice_lines_ = std::move(notice_lines);
}

GuildRankGroup& Guild::ensure_rank(std::uint8_t rank, std::string rank_name) {
  rank_name = util::trim(std::move(rank_name));
  const auto it = std::find_if(ranks_.begin(), ranks_.end(),
                               [&](const GuildRankGroup& group) {
                                 return group.rank == rank;
                               });
  if (it != ranks_.end()) {
    if (!rank_name.empty()) {
      it->rank_name = std::move(rank_name);
      for (auto& member : it->members) {
        member.rank_name = it->rank_name;
      }
    }
    return *it;
  }

  GuildRankGroup group;
  group.rank = rank;
  group.rank_name = std::move(rank_name);
  ranks_.push_back(std::move(group));
  return ranks_.back();
}

Guild* GuildManager::find_guild(std::string_view name) {
  for (auto& guild : guilds_) {
    if (equals_name(guild.name(), name)) {
      return &guild;
    }
  }
  return nullptr;
}

const Guild* GuildManager::find_guild(std::string_view name) const {
  for (const auto& guild : guilds_) {
    if (equals_name(guild.name(), name)) {
      return &guild;
    }
  }
  return nullptr;
}

Guild* GuildManager::find_guild_by_member(std::string_view member_name) {
  for (auto& guild : guilds_) {
    if (guild.has_member(member_name)) {
      return &guild;
    }
  }
  return nullptr;
}

const Guild* GuildManager::find_guild_by_member(std::string_view member_name) const {
  for (const auto& guild : guilds_) {
    if (guild.has_member(member_name)) {
      return &guild;
    }
  }
  return nullptr;
}

Guild* GuildManager::create_guild(std::string name, std::string lord_name,
                                  std::string lord_rank_name,
                                  std::uint64_t lord_actor_id) {
  name = normalize_name(std::move(name));
  lord_name = normalize_name(std::move(lord_name));
  if (name.empty() || lord_name.empty() || find_guild(name) != nullptr ||
      find_guild_by_member(lord_name) != nullptr) {
    return nullptr;
  }
  Guild guild{name};
  if (!guild.add_lord(std::move(lord_name), std::move(lord_rank_name), lord_actor_id)) {
    return nullptr;
  }
  guilds_.push_back(std::move(guild));
  return &guilds_.back();
}

bool GuildManager::erase_guild(std::string_view name) {
  const auto old_size = guilds_.size();
  guilds_.erase(std::remove_if(guilds_.begin(), guilds_.end(),
                               [&](const Guild& guild) {
                                 return equals_name(guild.name(), name);
                               }),
                guilds_.end());
  return guilds_.size() != old_size;
}

}  // namespace mir2
