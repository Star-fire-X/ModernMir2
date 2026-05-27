#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>

#include "world/logic_runtime.hpp"

namespace {

mir2::MapConfig make_map(std::string id) {
  mir2::MapConfig map;
  map.id = std::move(id);
  map.title = map.id;
  map.width = 30;
  map.height = 30;
  map.home_x = 10;
  map.home_y = 10;
  return map;
}

mir2::HostConfig make_config(std::string script) {
  mir2::HostConfig config;
  config.budgets.tick_ms = 500;
  config.runtime.legacy_random_seed = 1;
  config.maps = {make_map("0"), make_map("1")};
  mir2::NpcConfig npc;
  npc.id = "deferred";
  npc.map_id = "0";
  npc.name = "DeferredNpc";
  npc.x = 11;
  npc.y = 10;
  npc.service = "none";
  npc.dialog_sections.push_back({"@main", std::move(script)});
  config.npcs.push_back(std::move(npc));
  return config;
}

mir2::CharacterRecord make_character(std::string name, std::string map_id) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = std::move(map_id);
  character.x = 10;
  character.y = 10;
  character.ability.level = 10;
  character.ability.hp = 30;
  character.ability.max_hp = 30;
  return character;
}

mir2::LogicCommand enter_world(std::uint64_t session_id, mir2::CharacterRecord character) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = session_id;
  command.account_id = character.account_id;
  command.character_name = character.character_name;
  command.map_id = character.map_id;
  command.x = character.x;
  command.y = character.y;
  command.character = std::move(character);
  return command;
}

mir2::LogicCommand click_npc() {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::click_npc;
  command.session_id = 12;
  command.target_actor_id = 1;
  return command;
}

bool has_trace(const mir2::RuntimeDispatch& dispatch, std::string_view stage,
               std::string_view action, bool success, std::int32_t value = -1,
               std::string_view label = {}) {
  return std::any_of(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                     [&](const mir2::LegacyRuntimeTrace& trace) {
                       return trace.stage == stage && trace.action == action &&
                              trace.success == success &&
                              (value < 0 || trace.value == value) &&
                              (label.empty() || trace.label == label);
                     });
}

std::int32_t trace_count(const mir2::RuntimeDispatch& dispatch, std::string_view action) {
  return static_cast<std::int32_t>(std::count_if(
      dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
      [&](const mir2::LegacyRuntimeTrace& trace) {
        return trace.stage == "LegacyScript" && trace.action == action;
      }));
}

void enter_hero(mir2::LogicRuntime& runtime, std::string map_id = "0") {
  static_cast<void>(
      runtime.route_logic_command(enter_world(12, make_character("Hero", std::move(map_id)))));
  static_cast<void>(runtime.tick(1000));
}

}  // namespace

int main() {
  {
    mir2::LogicRuntime runtime(make_config(R"(#IF
CHECKLEVEL 1
#ACT
BATCHDELAY 1
ADDBATCH 1
BATCHMOVE
#SAY Scheduled)"));
    runtime.initialize();
    enter_hero(runtime);
    static_cast<void>(runtime.route_logic_command(click_npc()));
    const auto script_dispatch = runtime.tick(1500);
    assert(has_trace(script_dispatch, "LegacyScript", "deferred_action", true, 1,
                     "BATCHDELAY 1"));
    assert(has_trace(script_dispatch, "LegacyScript", "deferred_action", true, 1,
                     "ADDBATCH 1"));
    assert(has_trace(script_dispatch, "LegacyBatchMove", "schedule", true, 2, "1"));
    assert(runtime.snapshot_character_actor("Hero")->map_id == "0");
    static_cast<void>(runtime.tick(2000));
    const auto due_dispatch = runtime.tick(2500);
    assert(has_trace(due_dispatch, "LegacyBatchMove", "batch_move", true, 1, "1"));
    assert(runtime.snapshot_character_actor("Hero")->map_id == "1");
  }

  {
    mir2::LogicRuntime runtime(make_config(R"(#IF
CHECKLEVEL 1
#ACT
RECALLMAP 1
#SAY Recalled)"));
    runtime.initialize();
    enter_hero(runtime);
    static_cast<void>(
        runtime.route_logic_command(enter_world(13, make_character("Remote", "1"))));
    static_cast<void>(runtime.tick(1500));
    static_cast<void>(runtime.route_logic_command(click_npc()));
    const auto dispatch = runtime.tick(2000);
    assert(has_trace(dispatch, "LegacyBatchMove", "recall_map", true, 1, "1"));
    assert(runtime.snapshot_character_actor("Remote")->map_id == "0");
  }

  {
    mir2::LogicRuntime runtime(make_config(R"(#IF
CHECKLEVEL 1
#ACT
EXCHANGEMAP 1
#SAY Exchanged)"));
    runtime.initialize();
    enter_hero(runtime);
    static_cast<void>(runtime.route_logic_command(click_npc()));
    const auto dispatch = runtime.tick(1500);
    assert(has_trace(dispatch, "LegacyBatchMove", "exchange_map", true, 1, "1"));
    assert(runtime.snapshot_character_actor("Hero")->map_id == "1");
  }

  {
    mir2::LogicRuntime runtime(make_config(R"(#IF
CHECKLEVEL 1
#ACT
EXCHANGEMAP
RECALLMAP
ADDBATCH
BATCHMOVE
#SAY Rejected)"));
    runtime.initialize();
    enter_hero(runtime);
    static_cast<void>(runtime.route_logic_command(click_npc()));
    const auto dispatch = runtime.tick(1500);
    assert(trace_count(dispatch, "deferred_action_reject") == 4);
    assert(!has_trace(dispatch, "LegacyScript", "unsupported_action", false));
  }

  return 0;
}
