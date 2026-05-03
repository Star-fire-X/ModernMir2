#include "world/legacy_event_manager.hpp"

#include <algorithm>
#include <utility>

namespace mir2 {

std::uint64_t LegacyEventManager::enqueue(LegacyEventRecord record, std::uint64_t now_ms) {
  if (record.id == 0) {
    record.id = next_event_id_++;
  } else {
    next_event_id_ = std::max(next_event_id_, record.id + 1);
  }
  if (record.open_start_ms == 0) {
    record.open_start_ms = now_ms;
  }
  if (record.run_start_ms == 0) {
    record.run_start_ms = now_ms;
  }
  if (record.run_tick_ms == 0) {
    record.run_tick_ms = 500;
  }
  record.active = true;
  record.closed = false;
  active_events_.push_back(std::move(record));
  return active_events_.back().id;
}

std::optional<LegacyEventRecord> LegacyEventManager::find(const std::string& map_id,
                                                          std::int32_t x,
                                                          std::int32_t y,
                                                          LegacyEventType type) const {
  for (const auto& event : active_events_) {
    if (event.active && !event.closed && event.map_id == map_id && event.x == x &&
        event.y == y && event.type == type) {
      return event;
    }
  }
  return std::nullopt;
}

LegacyEventManagerRun LegacyEventManager::run(std::uint64_t now_ms,
                                              std::uint64_t current_tick) {
  LegacyEventManagerRun result;
  std::size_t index = 0;
  while (index < active_events_.size()) {
    auto& event = active_events_[index];
    if (event.active && !event.closed && now_ms > event.run_start_ms + event.run_tick_ms) {
      event.run_start_ms = now_ms;
      add_trace(result.dispatch, event, "run", now_ms, current_tick);
      if (event.continue_ms > 0 && now_ms > event.open_start_ms + event.continue_ms) {
        event.active = false;
        event.closed = true;
        event.close_time_ms = now_ms;
        add_trace(result.dispatch, event, "close", now_ms, current_tick);
        result.closed_events.push_back(event);
        closed_events_.push_back(event);
        active_events_.erase(active_events_.begin() + static_cast<std::ptrdiff_t>(index));
        continue;
      }
    }
    ++index;
  }

  constexpr std::uint64_t kClosedTtlMs = 5ULL * 60ULL * 1000ULL;
  for (auto it = closed_events_.begin(); it != closed_events_.end(); ++it) {
    if (now_ms > it->close_time_ms + kClosedTtlMs) {
      add_trace(result.dispatch, *it, "cleanup_closed", now_ms, current_tick);
      result.cleaned_events.push_back(*it);
      closed_events_.erase(it);
      break;
    }
  }
  return result;
}

std::string LegacyEventManager::type_name(LegacyEventType type) const {
  switch (type) {
    case LegacyEventType::stone_mine:
      return "stone_mine";
    case LegacyEventType::pile_stones:
      return "pile_stones";
    case LegacyEventType::holy_curtain:
      return "holy_curtain";
    case LegacyEventType::fire_burn:
      return "fire_burn";
  }
  return "unknown";
}

void LegacyEventManager::add_trace(RuntimeDispatch& dispatch, const LegacyEventRecord& event,
                                   std::string action, std::uint64_t now_ms,
                                   std::uint64_t current_tick, bool success) const {
  dispatch.legacy_traces.push_back(LegacyRuntimeTrace{
      "LegacyEventManager",
      std::move(action),
      event.map_id,
      type_name(event.type),
      event.id,
      now_ms,
      current_tick,
      0,
      0,
      0,
      0,
      {},
      std::to_string(event.x) + "," + std::to_string(event.y),
      0,
      0,
      0,
      0,
      success});
}

}  // namespace mir2
