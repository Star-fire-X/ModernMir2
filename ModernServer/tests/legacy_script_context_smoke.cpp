#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "world/logic_runtime.hpp"

namespace {

mir2::LegacyUserItem make_item(std::uint16_t index, std::int32_t make_index) {
  mir2::LegacyUserItem item;
  item.index = index;
  item.make_index = make_index;
  item.dura = 1000;
  item.dura_max = 1000;
  return item;
}

mir2::HostConfig make_config(std::vector<mir2::NpcDialogSectionConfig> sections) {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 1;
  config.maps.push_back(mir2::MapConfig{
      .id = "0",
      .title = "ScriptMap",
      .width = 30,
      .height = 30,
      .home_x = 10,
      .home_y = 10,
  });
  config.items.push_back(mir2::ItemConfig{
      .id = 1,
      .name = "Apple",
      .weight = 1,
      .dura_max = 1000,
      .equip_slot = -1,
  });
  config.items.push_back(mir2::ItemConfig{
      .id = 2,
      .name = "Banana",
      .weight = 1,
      .dura_max = 1000,
      .equip_slot = -1,
  });
  config.items.push_back(mir2::ItemConfig{
      .id = 3,
      .name = "Sword",
      .weight = 1,
      .std_mode = 5,
      .dura_max = 1000,
      .equip_slot = static_cast<std::int32_t>(mir2::kEquipWeapon),
  });
  mir2::NpcConfig npc;
  npc.id = "context";
  npc.map_id = "0";
  npc.name = "ContextNpc";
  npc.x = 11;
  npc.y = 10;
  npc.service = "none";
  npc.dialog_sections = std::move(sections);
  config.npcs.push_back(std::move(npc));
  return config;
}

mir2::CharacterRecord make_character() {
  mir2::CharacterRecord character;
  character.account_id = "acct";
  character.character_name = "Hero";
  character.map_id = "0";
  character.x = 10;
  character.y = 10;
  character.gold = 100;
  character.ability.level = 10;
  character.ability.hp = 30;
  character.ability.max_hp = 30;
  character.ability.max_weight = 1000;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  character.bag_items[0] = make_item(1, 101);
  character.bag_items[1] = make_item(2, 202);
  character.equipped_items[mir2::kEquipWeapon] = make_item(3, 303);
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

mir2::LogicCommand click_npc() {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::click_npc;
  command.session_id = 12;
  command.target_actor_id = 1;
  return command;
}

bool has_trace(const mir2::RuntimeDispatch& dispatch, std::string_view action,
               bool success = true, std::int32_t value = -1) {
  return std::any_of(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                     [&](const mir2::LegacyRuntimeTrace& trace) {
                       return trace.stage == "LegacyScript" && trace.action == action &&
                              trace.success == success &&
                              (value < 0 || trace.value == value);
                     });
}

std::int32_t count_item(const mir2::CharacterRecord& character, std::uint16_t index) {
  return static_cast<std::int32_t>(std::count_if(
      character.bag_items.begin(), character.bag_items.end(),
      [&](const mir2::LegacyUserItem& item) { return !mir2::is_empty(item) && item.index == index; }));
}

mir2::RuntimeDispatch run_script(mir2::LogicRuntime& runtime) {
  static_cast<void>(runtime.route_logic_command(enter_world()));
  static_cast<void>(runtime.tick(1000));
  static_cast<void>(runtime.route_logic_command(click_npc()));
  return runtime.tick(1502);
}

}  // namespace

int main() {
  {
    mir2::LogicRuntime runtime(make_config({
        {"@main", "#IF\nCHECKLEVEL 1\n#ACT\nTAKE Apple 1\nGOTO @verify"},
        {"@verify", "#IF\nISTAKEITEM Apple 1\n#ACT\nGIVE Gold 1\n#SAY Took\n#ELSESAY Failed"},
    }));
    runtime.initialize();
    const auto dispatch = run_script(runtime);
    assert(has_trace(dispatch, "take_item", true, 1));
    assert(has_trace(dispatch, "condition", true, 1));
    const auto snapshot = runtime.snapshot_character_actor("Hero");
    assert(snapshot.has_value());
    assert(snapshot->gold == 101);
    assert(count_item(*snapshot, 1) == 0);
  }

  {
    mir2::LogicRuntime runtime(make_config({
        {"@main", "#IF\nCHECKITEM Apple 1\n#ACT\nTAKECHECKITEM Banana 1\n#SAY Took"},
    }));
    runtime.initialize();
    const auto dispatch = run_script(runtime);
    assert(has_trace(dispatch, "takecheckitem", true, 1));
    const auto snapshot = runtime.snapshot_character_actor("Hero");
    assert(snapshot.has_value());
    assert(count_item(*snapshot, 1) == 0);
    assert(count_item(*snapshot, 2) == 1);
  }

  {
    mir2::LogicRuntime runtime(make_config({
        {"@main", "#IF\nCHECKITEMW [WEAPON] 1\n#ACT\nTAKEW [WEAPON] 1\n#SAY Took"},
    }));
    runtime.initialize();
    const auto dispatch = run_script(runtime);
    assert(has_trace(dispatch, "condition", true, 1));
    assert(has_trace(dispatch, "takew", true, 1));
    const auto snapshot = runtime.snapshot_character_actor("Hero");
    assert(snapshot.has_value());
    assert(mir2::is_empty(snapshot->equipped_items[mir2::kEquipWeapon]));
  }

  {
    mir2::LogicRuntime runtime(make_config({
        {"@main", "#IF\nCHECKLEVEL 1\n#ACT\nENDQUEST\nSAY Hidden"},
    }));
    runtime.initialize();
    const auto dispatch = run_script(runtime);
    assert(has_trace(dispatch, "endquest"));
    assert(!has_trace(dispatch, "say"));
  }

  return 0;
}
