#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mir2 {

struct CastleWarRegistration {
  std::string guild_name{};
  std::int32_t attack_day{0};
};

struct CastleCoreOccupant {
  std::string guild_name{};
  bool alive{true};
};

enum class CastleWarEventType {
  start,
  timeout_warning,
  owner_changed,
  finish
};

struct CastleWarEvent {
  CastleWarEventType type{};
  std::string guild_name{};
};

enum class CastleWarOpResult {
  ok,
  requester_not_lord,
  missing_zuma_piece,
  already_owner,
  already_registered,
  no_due_war,
  already_under_attack,
  not_under_attack,
  empty_guild,
  guild_not_registered,
  occupation_too_early,
  core_not_controlled
};

class CastleManager {
 public:
  static constexpr std::uint64_t kOccupationDelayMs = 10ULL * 60ULL * 1000ULL;
  static constexpr std::uint64_t kWarDurationMs = 3ULL * 60ULL * 60ULL * 1000ULL;
  static constexpr std::uint64_t kTimeoutWarningLeadMs = 10ULL * 60ULL * 1000ULL;

  explicit CastleManager(std::string castle_name = "Sabuk");

  [[nodiscard]] const std::string& castle_name() const { return castle_name_; }
  [[nodiscard]] const std::string& owner_guild() const { return owner_guild_; }
  [[nodiscard]] const std::string& owner_lord() const { return owner_lord_; }
  [[nodiscard]] const std::vector<CastleWarRegistration>& registrations() const {
    return registrations_;
  }
  [[nodiscard]] const std::vector<std::string>& rush_guilds() const { return rush_guilds_; }
  [[nodiscard]] bool under_attack() const { return under_attack_; }
  [[nodiscard]] std::uint64_t castle_attack_started_ms() const {
    return castle_attack_started_ms_;
  }

  void set_owner(std::string guild_name, std::string lord_name);

  CastleWarOpResult propose_castle_war(std::string guild_name, bool requester_is_lord,
                                       bool has_zuma_piece, std::int32_t current_day,
                                       std::int32_t delay_days = 4);
  CastleWarOpResult start_due_war(std::int32_t current_day, std::uint64_t now_ms,
                                  std::vector<CastleWarEvent>& events);
  CastleWarOpResult try_occupy(std::string guild_name,
                               const std::vector<CastleCoreOccupant>& core_occupants,
                               std::uint64_t now_ms,
                               std::vector<CastleWarEvent>& events);
  void run(std::uint64_t now_ms, std::vector<CastleWarEvent>& events);
  void finish_war(std::vector<CastleWarEvent>& events);

 private:
  [[nodiscard]] bool is_registered(std::string_view guild_name) const;
  [[nodiscard]] bool is_rush_guild(std::string_view guild_name) const;

  std::string castle_name_{};
  std::string owner_guild_{};
  std::string owner_lord_{};
  std::vector<CastleWarRegistration> registrations_{};
  std::vector<std::string> rush_guilds_{};
  bool under_attack_{false};
  bool timeout_warning_sent_{false};
  std::uint64_t latest_war_start_ms_{0};
  std::uint64_t castle_attack_started_ms_{0};
};

}  // namespace mir2
