#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mir2 {

inline constexpr std::uint8_t kGuildLordRank = 1;
inline constexpr std::uint8_t kGuildDefaultRank = 99;

struct GuildMember {
  std::string name{};
  std::uint8_t rank{kGuildDefaultRank};
  std::string rank_name{};
  std::uint64_t online_actor_id{0};
  bool hears_guild_chat{true};
};

struct GuildChatDelivery {
  std::string member_name{};
  std::uint64_t online_actor_id{0};
  std::string text{};
};

struct GuildRankGroup {
  std::uint8_t rank{kGuildDefaultRank};
  std::string rank_name{};
  std::vector<GuildMember> members{};
};

struct GuildWarState {
  std::string enemy_guild{};
  std::uint64_t start_ms{0};
  std::uint64_t remain_ms{3 * 60 * 60 * 1000};
};

enum class GuildMemberOpResult {
  ok,
  guild_not_found,
  requester_not_lord,
  target_not_found,
  target_not_online,
  target_not_facing_requester,
  target_rejects_guild,
  already_member,
  target_in_other_guild,
  member_limit_reached,
  not_member,
  lord_cannot_leave,
  target_is_lord
};

enum class GuildRelationOpResult {
  ok,
  guild_not_found,
  target_guild_not_found,
  requester_not_lord,
  target_not_lord,
  same_guild,
  already_allied,
  not_allied,
  target_rejects_ally,
  hostile_guild
};

struct GuildAddMemberContext {
  bool target_online{true};
  bool target_facing_requester{true};
  bool target_allows_guild{true};
  std::uint64_t target_actor_id{0};
  std::size_t max_member_count{0};
};

class Guild {
 public:
  Guild() = default;
  explicit Guild(std::string name);

  [[nodiscard]] const std::string& name() const { return name_; }
  [[nodiscard]] const std::vector<std::string>& notice_lines() const { return notice_lines_; }
  [[nodiscard]] const std::vector<std::string>& ally_guilds() const { return ally_guilds_; }
  [[nodiscard]] const std::vector<GuildWarState>& hostile_guilds() const { return hostile_guilds_; }
  [[nodiscard]] const std::vector<GuildRankGroup>& ranks() const { return ranks_; }
  [[nodiscard]] bool allow_ally_guild() const { return allow_ally_guild_; }
  [[nodiscard]] std::size_t member_count() const;
  [[nodiscard]] bool empty() const { return member_count() == 0; }
  [[nodiscard]] std::optional<std::string_view> lord_name() const;
  [[nodiscard]] const GuildMember* find_member(std::string_view name) const;
  [[nodiscard]] GuildMember* find_member(std::string_view name);
  [[nodiscard]] bool has_member(std::string_view name) const { return find_member(name) != nullptr; }
  [[nodiscard]] bool is_lord(std::string_view name) const;
  [[nodiscard]] std::vector<GuildChatDelivery> guild_chat_deliveries(
      std::string_view speaker_name, std::string_view text) const;
  [[nodiscard]] std::vector<std::uint64_t> online_member_actor_ids() const;
  [[nodiscard]] bool is_ally_guild(std::string_view guild_name) const;
  [[nodiscard]] bool is_hostile_guild(std::string_view guild_name) const;

  bool add_lord(std::string name, std::string rank_name = "Guild Lord",
                std::uint64_t online_actor_id = 0);
  bool add_member(std::string name, std::string rank_name = "Guild Member",
                  std::uint64_t online_actor_id = 0);
  bool remove_member(std::string_view name);
  bool transfer_lord(std::string_view current_lord, std::string_view target_name,
                     std::string old_lord_rank_name = "Guild Member",
                     std::string new_lord_rank_name = "Guild Lord");
  bool set_member_online_actor(std::string_view name, std::uint64_t online_actor_id);
  bool clear_member_online_actor(std::string_view name, std::uint64_t online_actor_id);
  bool set_member_hears_guild_chat(std::string_view name, bool hears_guild_chat);
  void set_allow_ally_guild(bool allow_ally_guild) { allow_ally_guild_ = allow_ally_guild; }
  bool make_ally_guild(std::string guild_name);
  bool break_ally_guild(std::string_view guild_name);
  bool declare_guild_war(std::string guild_name, std::uint64_t now_ms,
                         std::uint64_t remain_ms = 3 * 60 * 60 * 1000);
  bool remove_hostile_guild(std::string_view guild_name);
  std::vector<std::string> expire_guild_wars(std::uint64_t now_ms);
  void set_notice_lines(std::vector<std::string> notice_lines);

 private:
  GuildRankGroup& ensure_rank(std::uint8_t rank, std::string rank_name);

  std::string name_{};
  std::vector<std::string> notice_lines_{};
  std::vector<std::string> ally_guilds_{};
  std::vector<GuildWarState> hostile_guilds_{};
  std::vector<GuildRankGroup> ranks_{};
  bool allow_ally_guild_{false};
};

class GuildManager {
 public:
  [[nodiscard]] const std::vector<Guild>& guilds() const { return guilds_; }
  [[nodiscard]] Guild* find_guild(std::string_view name);
  [[nodiscard]] const Guild* find_guild(std::string_view name) const;
  [[nodiscard]] Guild* find_guild_by_member(std::string_view member_name);
  [[nodiscard]] const Guild* find_guild_by_member(std::string_view member_name) const;

  Guild* create_guild(std::string name, std::string lord_name,
                      std::string lord_rank_name = "Guild Lord",
                      std::uint64_t lord_actor_id = 0);
  bool erase_guild(std::string_view name);
  GuildMemberOpResult add_member_by_lord(std::string_view guild_name,
                                         std::string_view requester_name,
                                         std::string target_name,
                                         const GuildAddMemberContext& context = {});
  GuildMemberOpResult remove_member_by_lord(std::string_view guild_name,
                                            std::string_view requester_name,
                                            std::string_view target_name);
  GuildMemberOpResult leave_member(std::string_view member_name);
  GuildMemberOpResult transfer_lord(std::string_view guild_name,
                                    std::string_view requester_name,
                                    std::string_view target_name);
  [[nodiscard]] std::vector<GuildChatDelivery> guild_chat_deliveries(
      std::string_view guild_name, std::string_view speaker_name, std::string_view text) const;
  GuildRelationOpResult make_ally(std::string_view requester_guild,
                                  std::string_view requester_name,
                                  std::string_view target_guild,
                                  std::string_view target_name);
  GuildRelationOpResult break_ally(std::string_view requester_guild,
                                   std::string_view requester_name,
                                   std::string_view target_guild);
  GuildRelationOpResult declare_guild_war(std::string_view requester_guild,
                                          std::string_view requester_name,
                                          std::string_view target_guild,
                                          std::uint64_t now_ms,
                                          std::uint64_t remain_ms = 3 * 60 * 60 * 1000);
  std::vector<std::string> expire_guild_wars(std::uint64_t now_ms);

 private:
  std::vector<Guild> guilds_{};
};

}  // namespace mir2
