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

std::vector<GuildChatDelivery> Guild::guild_chat_deliveries(std::string_view speaker_name,
                                                            std::string_view text) const {
  std::vector<GuildChatDelivery> deliveries;
  if (text.empty()) {
    return deliveries;
  }
  const auto message = std::string(speaker_name) + ":" + std::string(text);
  for (const auto& rank : ranks_) {
    for (const auto& member : rank.members) {
      if (member.online_actor_id != 0 && member.hears_guild_chat) {
        deliveries.push_back(GuildChatDelivery{member.name, member.online_actor_id, message});
      }
    }
  }
  return deliveries;
}

std::vector<std::uint64_t> Guild::online_member_actor_ids() const {
  std::vector<std::uint64_t> actor_ids;
  for (const auto& rank : ranks_) {
    for (const auto& member : rank.members) {
      if (member.online_actor_id != 0) {
        actor_ids.push_back(member.online_actor_id);
      }
    }
  }
  return actor_ids;
}

bool Guild::add_lord(std::string name, std::string rank_name, std::uint64_t online_actor_id) {
  name = normalize_name(std::move(name));
  rank_name = util::trim(std::move(rank_name));
  if (name.empty() || has_member(name)) {
    return false;
  }
  auto& rank = ensure_rank(kGuildLordRank, std::move(rank_name));
  rank.members.push_back(GuildMember{name, rank.rank, rank.rank_name, online_actor_id, true});
  return true;
}

bool Guild::add_member(std::string name, std::string rank_name, std::uint64_t online_actor_id) {
  name = normalize_name(std::move(name));
  rank_name = util::trim(std::move(rank_name));
  if (name.empty() || has_member(name)) {
    return false;
  }
  auto& rank = ensure_rank(kGuildDefaultRank, std::move(rank_name));
  rank.members.push_back(GuildMember{name, rank.rank, rank.rank_name, online_actor_id, true});
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

bool Guild::set_member_hears_guild_chat(std::string_view name, bool hears_guild_chat) {
  auto* member = find_member(name);
  if (member == nullptr) {
    return false;
  }
  member->hears_guild_chat = hears_guild_chat;
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
  if (rank == kGuildLordRank) {
    return *ranks_.insert(ranks_.begin(), std::move(group));
  }
  ranks_.push_back(std::move(group));
  return ranks_.back();
}

bool Guild::transfer_lord(std::string_view current_lord, std::string_view target_name,
                          std::string old_lord_rank_name, std::string new_lord_rank_name) {
  if (!is_lord(current_lord) || is_lord(target_name) || find_member(target_name) == nullptr) {
    return false;
  }

  std::string target_copy;
  std::uint64_t target_actor_id = 0;
  bool target_hears_guild_chat = true;
  for (auto rank_it = ranks_.begin(); rank_it != ranks_.end(); ++rank_it) {
    auto& members = rank_it->members;
    const auto member_it = std::find_if(members.begin(), members.end(),
                                        [&](const GuildMember& member) {
                                          return equals_name(member.name, target_name);
                                        });
    if (member_it == members.end()) {
      continue;
    }
    target_copy = member_it->name;
    target_actor_id = member_it->online_actor_id;
    target_hears_guild_chat = member_it->hears_guild_chat;
    members.erase(member_it);
    if (members.empty()) {
      ranks_.erase(rank_it);
    }
    break;
  }

  auto* old_lord = find_member(current_lord);
  if (old_lord == nullptr || target_copy.empty()) {
    return false;
  }
  const auto old_lord_name = old_lord->name;
  const auto old_lord_actor_id = old_lord->online_actor_id;
  const auto old_lord_hears_guild_chat = old_lord->hears_guild_chat;
  remove_member(current_lord);

  auto& lord_rank = ensure_rank(kGuildLordRank, std::move(new_lord_rank_name));
  lord_rank.members.insert(lord_rank.members.begin(),
                           GuildMember{target_copy, kGuildLordRank, lord_rank.rank_name,
                                       target_actor_id, target_hears_guild_chat});
  if (add_member(old_lord_name, std::move(old_lord_rank_name), old_lord_actor_id)) {
    set_member_hears_guild_chat(old_lord_name, old_lord_hears_guild_chat);
  }
  return true;
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

GuildMemberOpResult GuildManager::add_member_by_lord(
    std::string_view guild_name, std::string_view requester_name, std::string target_name,
    const GuildAddMemberContext& context) {
  auto* guild = find_guild(guild_name);
  if (guild == nullptr) {
    return GuildMemberOpResult::guild_not_found;
  }
  if (!guild->is_lord(requester_name)) {
    return GuildMemberOpResult::requester_not_lord;
  }
  target_name = normalize_name(std::move(target_name));
  if (target_name.empty()) {
    return GuildMemberOpResult::target_not_found;
  }
  if (!context.target_online) {
    return GuildMemberOpResult::target_not_online;
  }
  if (!context.target_facing_requester) {
    return GuildMemberOpResult::target_not_facing_requester;
  }
  if (!context.target_allows_guild) {
    return GuildMemberOpResult::target_rejects_guild;
  }
  if (guild->has_member(target_name)) {
    return GuildMemberOpResult::already_member;
  }
  if (find_guild_by_member(target_name) != nullptr) {
    return GuildMemberOpResult::target_in_other_guild;
  }
  if (context.max_member_count > 0 && guild->member_count() >= context.max_member_count) {
    return GuildMemberOpResult::member_limit_reached;
  }
  return guild->add_member(std::move(target_name), "Guild Member", context.target_actor_id)
             ? GuildMemberOpResult::ok
             : GuildMemberOpResult::target_not_found;
}

GuildMemberOpResult GuildManager::remove_member_by_lord(std::string_view guild_name,
                                                        std::string_view requester_name,
                                                        std::string_view target_name) {
  auto* guild = find_guild(guild_name);
  if (guild == nullptr) {
    return GuildMemberOpResult::guild_not_found;
  }
  if (!guild->is_lord(requester_name)) {
    return GuildMemberOpResult::requester_not_lord;
  }
  if (!guild->has_member(target_name)) {
    return GuildMemberOpResult::not_member;
  }
  const auto remove_self = equals_name(requester_name, target_name);
  if (!guild->remove_member(target_name)) {
    return GuildMemberOpResult::not_member;
  }
  if (guild->empty() || remove_self) {
    erase_guild(guild_name);
  }
  return GuildMemberOpResult::ok;
}

GuildMemberOpResult GuildManager::leave_member(std::string_view member_name) {
  auto* guild = find_guild_by_member(member_name);
  if (guild == nullptr) {
    return GuildMemberOpResult::not_member;
  }
  if (guild->is_lord(member_name)) {
    return GuildMemberOpResult::lord_cannot_leave;
  }
  return guild->remove_member(member_name) ? GuildMemberOpResult::ok
                                           : GuildMemberOpResult::not_member;
}

GuildMemberOpResult GuildManager::transfer_lord(std::string_view guild_name,
                                                std::string_view requester_name,
                                                std::string_view target_name) {
  auto* guild = find_guild(guild_name);
  if (guild == nullptr) {
    return GuildMemberOpResult::guild_not_found;
  }
  if (!guild->is_lord(requester_name)) {
    return GuildMemberOpResult::requester_not_lord;
  }
  if (!guild->has_member(target_name)) {
    return GuildMemberOpResult::not_member;
  }
  if (guild->is_lord(target_name)) {
    return GuildMemberOpResult::target_is_lord;
  }
  return guild->transfer_lord(requester_name, target_name) ? GuildMemberOpResult::ok
                                                           : GuildMemberOpResult::not_member;
}

std::vector<GuildChatDelivery> GuildManager::guild_chat_deliveries(
    std::string_view guild_name, std::string_view speaker_name, std::string_view text) const {
  const auto* guild = find_guild(guild_name);
  if (guild == nullptr) {
    return {};
  }
  return guild->guild_chat_deliveries(speaker_name, text);
}

}  // namespace mir2
