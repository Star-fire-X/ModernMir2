#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "world/game_object.hpp"

namespace mir2 {

enum class LegacyEventType {
  stone_mine,
  pile_stones,
  holy_curtain,
  fire_burn
};

struct LegacyEventRecord {
  std::uint64_t id{0};
  std::string map_id{};
  std::int32_t x{0};
  std::int32_t y{0};
  LegacyEventType type{LegacyEventType::stone_mine};
  std::uint64_t open_start_ms{0};
  std::uint64_t continue_ms{0};
  std::uint64_t close_time_ms{0};
  std::uint64_t run_start_ms{0};
  std::uint64_t run_tick_ms{500};
  bool active{true};
  bool closed{false};
};

struct LegacyEventManagerRun {
  RuntimeDispatch dispatch{};
  std::vector<LegacyEventRecord> closed_events{};
  std::vector<LegacyEventRecord> cleaned_events{};
};

class LegacyEventManager {
 public:
  [[nodiscard]] std::uint64_t enqueue(LegacyEventRecord record, std::uint64_t now_ms);
  [[nodiscard]] std::optional<LegacyEventRecord> find(const std::string& map_id,
                                                      std::int32_t x,
                                                      std::int32_t y,
                                                      LegacyEventType type) const;
  [[nodiscard]] LegacyEventManagerRun run(std::uint64_t now_ms, std::uint64_t current_tick);
  [[nodiscard]] std::size_t active_count() const { return active_events_.size(); }
  [[nodiscard]] std::size_t closed_count() const { return closed_events_.size(); }

 private:
  [[nodiscard]] std::string type_name(LegacyEventType type) const;
  void add_trace(RuntimeDispatch& dispatch, const LegacyEventRecord& event,
                 std::string action, std::uint64_t now_ms, std::uint64_t current_tick,
                 bool success = true) const;

  std::vector<LegacyEventRecord> active_events_{};
  std::vector<LegacyEventRecord> closed_events_{};
  std::uint64_t next_event_id_{1};
};

}  // namespace mir2
