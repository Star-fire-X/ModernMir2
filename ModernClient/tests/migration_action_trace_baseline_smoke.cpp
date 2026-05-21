#include "game/game_state.hpp"
#include "shared/legacy/action_ids.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

std::map<std::string, std::vector<std::string>> read_trace_sections() {
  const auto path = std::filesystem::path{MIR2_CLIENT_SOURCE_DIR} / "tests" / "golden" /
                    "client_migration_pr1_expected_trace.txt";
  std::ifstream input(path);
  assert(input);

  std::map<std::string, std::vector<std::string>> sections;
  std::string current;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty() || line.front() == '#') {
      continue;
    }
    if (line.front() == '[' && line.back() == ']') {
      current = line.substr(1, line.size() - 2);
      sections[current] = {};
      continue;
    }
    assert(!current.empty());
    sections[current].push_back(line);
  }
  return sections;
}

void push(std::vector<std::string>& trace, const char* label) { trace.emplace_back(label); }

std::vector<std::string> run_action_trace() {
  using namespace mir2::client;
  using namespace mir2::client_v1;

  std::vector<std::string> trace;

  WorldViewState world;
  ActorState self;
  self.actor_id = 1;
  self.x = 12;
  self.y = 10;
  self.dir = 2;

  world.action_locked = true;
  world.action_lock_started_ms = 1000;
  assert(!server_accept_next_action(world, 11000));
  assert(!can_next_action(world, self, 11000));
  push(trace, "action_lock_blocks_until_10000ms");

  assert(server_accept_next_action(world, 11001));
  assert(!world.action_locked);
  push(trace, "server_accept_next_action_unlocks_after_timeout");

  assert(can_next_action(world, self, 11001));
  push(trace, "can_next_action_after_timeout");

  GameStateStore state;
  state.world.action_locked = true;
  state.apply(ActionAck{true, 1234});
  assert(!state.world.action_locked);
  assert(state.world.last_action_ack_ok);
  push(trace, "action_ack_success_unlocks");

  WorldSnapshot snapshot;
  snapshot.self_actor_id = 1;
  snapshot.actors.push_back(WorldActor{1, "Hero", 30, 20, 2, 0, 0, ActorType::player});
  state.apply(snapshot);
  auto& actor = state.world.actors[1];
  actor.from_x = 29;
  actor.from_y = 20;
  actor.x = 30;
  actor.y = 20;
  actor.current_action = ActorActionKind::walk;
  state.world.action_locked = true;
  state.world.last_sent_action_ident =
      static_cast<std::uint16_t>(3000U + mir2::legacy::kSmWalk);
  state.world.last_sent_action_dir = 2;
  state.apply(ActionAck{false, 1235});
  assert(!state.world.action_locked);
  assert(!state.world.last_action_ack_ok);
  assert(actor.x == 29 && actor.y == 20);
  assert(actor.current_action == ActorActionKind::turn);
  assert(state.world.action_fail_lock);
  push(trace, "action_ack_fail_unlocks_and_rewinds_self");

  assert(!is_unlock_action(state.world, state.world.fail_action_ident, 2,
                           state.world.fail_action_time_ms + 999));
  push(trace, "fail_lock_blocks_matching_action_for_1000ms");

  assert(is_unlock_action(state.world, state.world.fail_action_ident, 2,
                          state.world.fail_action_time_ms + 1000));
  assert(!state.world.action_fail_lock);
  push(trace, "fail_lock_releases_after_1000ms");

  push(trace, "known_gap_plus_pwr_not_covered");
  push(trace, "known_gap_plus_lng_not_covered");
  push(trace, "known_gap_plus_wid_not_covered");
  push(trace, "known_gap_plus_crs_not_covered");
  push(trace, "known_gap_plus_fir_not_covered");

  return trace;
}

}  // namespace

int main() {
  const auto sections = read_trace_sections();
  assert(run_action_trace() == sections.at("action.ack"));
  return 0;
}
