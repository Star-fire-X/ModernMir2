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
};

struct GuildRankGroup {
  std::uint8_t rank{kGuildDefaultRank};
  std::string rank_name{};
  std::vector<GuildMember> members{};
};

class Guild {
 public:
  Guild() = default;
  explicit Guild(std::string name);

  [[nodiscard]] const std::string& name() const { return name_; }
  [[nodiscard]] const std::vector<std::string>& notice_lines() const { return notice_lines_; }
  [[nodiscard]] const std::vector<GuildRankGroup>& ranks() const { return ranks_; }
  [[nodiscard]] std::size_t member_count() const;
  [[nodiscard]] bool empty() const { return member_count() == 0; }
  [[nodiscard]] std::optional<std::string_view> lord_name() const;
  [[nodiscard]] const GuildMember* find_member(std::string_view name) const;
  [[nodiscard]] GuildMember* find_member(std::string_view name);
  [[nodiscard]] bool has_member(std::string_view name) const { return find_member(name) != nullptr; }
  [[nodiscard]] bool is_lord(std::string_view name) const;

  bool add_lord(std::string name, std::string rank_name = "Guild Lord",
                std::uint64_t online_actor_id = 0);
  bool add_member(std::string name, std::string rank_name = "Guild Member",
                  std::uint64_t online_actor_id = 0);
  bool remove_member(std::string_view name);
  bool set_member_online_actor(std::string_view name, std::uint64_t online_actor_id);
  bool clear_member_online_actor(std::string_view name, std::uint64_t online_actor_id);
  void set_notice_lines(std::vector<std::string> notice_lines);

 private:
  GuildRankGroup& ensure_rank(std::uint8_t rank, std::string rank_name);

  std::string name_{};
  std::vector<std::string> notice_lines_{};
  std::vector<GuildRankGroup> ranks_{};
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

 private:
  std::vector<Guild> guilds_{};
};

}  // namespace mir2
