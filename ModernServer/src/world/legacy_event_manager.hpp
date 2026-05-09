#pragma once

#include <optional>
#include <vector>

#include "world/game_object.hpp"

namespace mir2 {

struct LegacyEventManagerRun {
  RuntimeDispatch dispatch{};
  std::vector<LegacyEventRecord> fire_burn_events{};
  std::vector<LegacyEventRecord> closed_events{};
  std::vector<LegacyEventRecord> cleaned_events{};
};

class LegacyEventManager {
 public:
  [[nodiscard]] std::uint64_t enqueue(LegacyEventRecord record, std::uint64_t now_ms);
  [[nodiscard]] std::uint64_t enqueue_holy_curtain_group(
      LegacyHolyCurtainGroup group, std::uint64_t now_ms);
  [[nodiscard]] std::optional<LegacyEventRecord> find(const std::string& map_id,
                                                      std::int32_t x,
                                                      std::int32_t y,
                                                      LegacyEventType type) const;
  [[nodiscard]] bool has_active_event(const std::string& map_id,
                                      std::int32_t x,
                                      std::int32_t y) const;
  [[nodiscard]] std::vector<LegacyHolyCurtainGroup> active_holy_groups() const;
  [[nodiscard]] LegacyEventManagerRun update_holy_group_seized(
      std::uint64_t group_id, std::vector<std::uint64_t> seized_actor_ids,
      std::uint64_t now_ms, std::uint64_t current_tick);
  [[nodiscard]] LegacyEventManagerRun run(std::uint64_t now_ms, std::uint64_t current_tick);
  [[nodiscard]] std::size_t active_count() const { return active_events_.size(); }
  [[nodiscard]] std::size_t closed_count() const { return closed_events_.size(); }

 private:
  [[nodiscard]] std::string type_name(LegacyEventType type) const;
  void add_trace(RuntimeDispatch& dispatch, const LegacyEventRecord& event,
                 std::string action, std::uint64_t now_ms, std::uint64_t current_tick,
                 bool success = true) const;
  void add_group_trace(RuntimeDispatch& dispatch, const LegacyHolyCurtainGroup& group,
                       std::string action, std::uint64_t now_ms,
                       std::uint64_t current_tick, bool success = true) const;
  void close_event_at(std::size_t index, LegacyEventManagerRun& result,
                      std::uint64_t now_ms, std::uint64_t current_tick);
  void close_holy_group_events(LegacyHolyCurtainGroup& group, LegacyEventManagerRun& result,
                               std::uint64_t now_ms, std::uint64_t current_tick);

  std::vector<LegacyEventRecord> active_events_{};
  std::vector<LegacyEventRecord> closed_events_{};
  std::uint64_t next_event_id_{1};
  std::vector<LegacyHolyCurtainGroup> holy_groups_{};
  std::uint64_t next_holy_group_id_{1};
};

}  // namespace mir2
