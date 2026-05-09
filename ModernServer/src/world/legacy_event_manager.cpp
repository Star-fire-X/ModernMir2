#include "world/legacy_event_manager.hpp"

#include <algorithm>
#include <utility>

namespace mir2 {

std::uint64_t LegacyEventManager::enqueue(LegacyEventRecord record, std::uint64_t now_ms) {
  if (record.skip_if_occupied && has_active_event(record.map_id, record.x, record.y)) {
    return 0;
  }
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

std::uint64_t LegacyEventManager::enqueue_holy_curtain_group(
    LegacyHolyCurtainGroup group, std::uint64_t now_ms) {
  if (group.id == 0) {
    group.id = next_holy_group_id_++;
  } else {
    next_holy_group_id_ = std::max(next_holy_group_id_, group.id + 1);
  }
  if (group.open_start_ms == 0) {
    group.open_start_ms = now_ms;
  }
  holy_groups_.push_back(std::move(group));
  return holy_groups_.back().id;
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

bool LegacyEventManager::has_active_event(const std::string& map_id,
                                          std::int32_t x,
                                          std::int32_t y) const {
  for (const auto& event : active_events_) {
    if (event.active && !event.closed && event.map_id == map_id && event.x == x &&
        event.y == y) {
      return true;
    }
  }
  return false;
}

std::vector<LegacyHolyCurtainGroup> LegacyEventManager::active_holy_groups() const {
  return holy_groups_;
}

LegacyEventManagerRun LegacyEventManager::update_holy_group_seized(
    std::uint64_t group_id, std::vector<std::uint64_t> seized_actor_ids,
    std::uint64_t now_ms, std::uint64_t current_tick) {
  LegacyEventManagerRun result;
  for (auto group_it = holy_groups_.begin(); group_it != holy_groups_.end(); ++group_it) {
    if (group_it->id != group_id) {
      continue;
    }
    group_it->seized_actor_ids = std::move(seized_actor_ids);
    if (group_it->seized_actor_ids.empty()) {
      add_group_trace(result.dispatch, *group_it, "holy_group_empty", now_ms, current_tick);
      close_holy_group_events(*group_it, result, now_ms, current_tick);
      holy_groups_.erase(group_it);
    }
    break;
  }
  return result;
}

LegacyEventManagerRun LegacyEventManager::run(std::uint64_t now_ms,
                                              std::uint64_t current_tick) {
  LegacyEventManagerRun result;
  for (auto group_it = holy_groups_.begin(); group_it != holy_groups_.end();) {
    const auto expired =
        (group_it->seize_ms > 0 && now_ms > group_it->open_start_ms + group_it->seize_ms) ||
        now_ms > group_it->open_start_ms + 3ULL * 60ULL * 1000ULL;
    if (expired) {
      add_group_trace(result.dispatch, *group_it, "holy_group_close", now_ms, current_tick);
      close_holy_group_events(*group_it, result, now_ms, current_tick);
      group_it = holy_groups_.erase(group_it);
      continue;
    }
    ++group_it;
  }

  std::size_t index = 0;
  while (index < active_events_.size()) {
    auto& event = active_events_[index];
    if (event.active && !event.closed && now_ms > event.run_start_ms + event.run_tick_ms) {
      event.run_start_ms = now_ms;
      add_trace(result.dispatch, event, "run", now_ms, current_tick);
      if (event.type == LegacyEventType::fire_burn &&
          (event.last_damage_ms == 0 || now_ms > event.last_damage_ms + 3000ULL)) {
        event.last_damage_ms = now_ms;
        add_trace(result.dispatch, event, "fire_tick", now_ms, current_tick);
        result.fire_burn_events.push_back(event);
      }
      if (event.continue_ms > 0 && now_ms > event.open_start_ms + event.continue_ms) {
        close_event_at(index, result, now_ms, current_tick);
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

void LegacyEventManager::add_group_trace(RuntimeDispatch& dispatch,
                                         const LegacyHolyCurtainGroup& group,
                                         std::string action, std::uint64_t now_ms,
                                         std::uint64_t current_tick, bool success) const {
  dispatch.legacy_traces.push_back(LegacyRuntimeTrace{
      "LegacyEventManager",
      std::move(action),
      group.map_id,
      "holy_curtain_group",
      group.id,
      now_ms,
      current_tick,
      0,
      0,
      0,
      0,
      {},
      std::to_string(group.seized_actor_ids.size()),
      0,
      0,
      static_cast<std::int32_t>(group.event_ids.size()),
      0,
      success});
}

void LegacyEventManager::close_event_at(std::size_t index, LegacyEventManagerRun& result,
                                        std::uint64_t now_ms,
                                        std::uint64_t current_tick) {
  auto event = active_events_[index];
  event.active = false;
  event.closed = true;
  event.close_time_ms = now_ms;
  add_trace(result.dispatch, event, "close", now_ms, current_tick);
  result.closed_events.push_back(event);
  closed_events_.push_back(event);
  active_events_.erase(active_events_.begin() + static_cast<std::ptrdiff_t>(index));
}

void LegacyEventManager::close_holy_group_events(LegacyHolyCurtainGroup& group,
                                                 LegacyEventManagerRun& result,
                                                 std::uint64_t now_ms,
                                                 std::uint64_t current_tick) {
  for (const auto event_id : group.event_ids) {
    for (std::size_t index = 0; index < active_events_.size(); ++index) {
      if (active_events_[index].id == event_id) {
        close_event_at(index, result, now_ms, current_tick);
        break;
      }
    }
  }
}

}  // namespace mir2
