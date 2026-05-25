#include <cassert>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "world/logic_runtime.hpp"

namespace {

mir2::CharacterRecord make_character() {
  mir2::CharacterRecord character;
  character.account_id = "acct";
  character.character_name = "Hero";
  character.map_id = "0";
  character.x = 10;
  character.y = 10;
  character.ability.level = 12;
  character.ability.hp = 30;
  character.ability.max_hp = 30;
  return character;
}

mir2::LogicCommand enter_world() {
  auto character = make_character();
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = 12;
  command.account_id = character.account_id;
  command.character_name = character.character_name;
  command.map_id = character.map_id;
  command.x = character.x;
  command.y = character.y;
  command.character = std::move(character);
  return command;
}

mir2::LogicCommand select_npc(std::string action) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::merchant_select;
  command.session_id = 12;
  command.target_actor_id = 1;
  command.text = std::move(action);
  return command;
}

bool has_trace(const mir2::RuntimeDispatch& dispatch, std::string_view stage,
               std::string_view action) {
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == stage && trace.action == action) {
      return true;
    }
  }
  return false;
}

void assert_position(mir2::LogicRuntime& runtime, std::string_view map_id, std::int32_t x,
                     std::int32_t y) {
  const auto snapshot = runtime.snapshot_character_actor("Hero");
  assert(snapshot.has_value());
  assert(snapshot->map_id == map_id);
  assert(snapshot->x == x);
  assert(snapshot->y == y);
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.budgets.tick_ms = 60'000;
  config.maps.push_back(mir2::MapConfig{"0", "Town", {}, 0, 0, 20, 20});
  config.maps.push_back(mir2::MapConfig{"Q001", "Trial", {}, 0, 0, 20, 20});

  mir2::NpcConfig npc;
  npc.id = "recaller";
  npc.map_id = "0";
  npc.name = "Recaller";
  npc.x = 11;
  npc.y = 10;
  npc.service = "none";
  npc.dialog_sections.push_back(
      {"@recall", "#IF\\CHECKLEVEL 1\\#ACT\\SET [1] 1\\TIMERECALL 1\\MAPMOVE Q001 5 5"});
  npc.dialog_sections.push_back(
      {"@cancel", "#IF\\CHECKLEVEL 1\\#ACT\\TIMERECALL 1\\BREAKTIMERECALL\\MAPMOVE Q001 6 6"});
  config.npcs.push_back(std::move(npc));

  mir2::MapQuestConfig enter_quest;
  enter_quest.map_id = "0";
  enter_quest.set_number = 1;
  enter_quest.value = 1;
  enter_quest.qfile = "enter_recall.txt";
  enter_quest.dialog_sections.push_back(
      {"@main", "#IF\\CHECKLEVEL 1\\#ACT\\TIMERECALL 0\\SET [1] 0\\MAPMOVE Q001 7 7"});
  config.map_quests.push_back(std::move(enter_quest));

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  static_cast<void>(runtime.route_logic_command(enter_world()));
  static_cast<void>(runtime.tick(60'000));
  assert_position(runtime, "0", 10, 10);

  static_cast<void>(runtime.route_logic_command(select_npc("@recall")));
  const auto schedule_dispatch = runtime.tick(120'000);
  assert(has_trace(schedule_dispatch, "LegacyScript", "time_recall"));
  assert(has_trace(schedule_dispatch, "LegacyTimeRecall", "schedule"));
  assert_position(runtime, "Q001", 5, 5);

  const auto recall_dispatch = runtime.tick(180'000);
  assert(has_trace(recall_dispatch, "LegacyTimeRecall", "recall"));
  assert(has_trace(recall_dispatch, "LegacyScript", "mapquest_trigger"));
  assert(has_trace(recall_dispatch, "LegacyScript", "time_recall"));
  assert(has_trace(recall_dispatch, "LegacyTimeRecall", "schedule"));
  assert_position(runtime, "Q001", 7, 7);

  const auto chained_recall_dispatch = runtime.tick(240'000);
  assert(has_trace(chained_recall_dispatch, "LegacyTimeRecall", "recall"));
  assert_position(runtime, "0", 10, 10);

  static_cast<void>(runtime.route_logic_command(select_npc("@cancel")));
  const auto cancel_dispatch = runtime.tick(300'000);
  assert(has_trace(cancel_dispatch, "LegacyScript", "time_recall"));
  assert(has_trace(cancel_dispatch, "LegacyScript", "time_recall_cancel"));
  assert(has_trace(cancel_dispatch, "LegacyTimeRecall", "schedule"));
  assert(has_trace(cancel_dispatch, "LegacyTimeRecall", "cancel"));
  assert_position(runtime, "Q001", 6, 6);

  const auto canceled_due_dispatch = runtime.tick(360'000);
  assert(!has_trace(canceled_due_dispatch, "LegacyTimeRecall", "recall"));
  assert_position(runtime, "Q001", 6, 6);

  return 0;
}
