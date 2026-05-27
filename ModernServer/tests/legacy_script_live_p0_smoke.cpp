#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config/config_loader.hpp"
#include "world/logic_runtime.hpp"

#ifndef MIR2_CONFIG_DIR
#error "MIR2_CONFIG_DIR must be defined by CMake"
#endif

namespace {

constexpr std::uint64_t kHeroSessionId = 12;

int fail(std::string_view stage, std::string_view case_name) {
  std::cerr << "legacy_script_live_p0_smoke failed at " << stage
            << " case=" << case_name << '\n';
  return 1;
}

const mir2::NpcConfig* find_npc_by_script(const mir2::HostConfig& config,
                                          std::string_view script) {
  for (const auto& npc : config.npcs) {
    if (npc.script == script) {
      return &npc;
    }
  }
  return nullptr;
}

mir2::MapConfig make_map(std::string id) {
  mir2::MapConfig map;
  map.id = std::move(id);
  map.title = map.id;
  map.width = 40;
  map.height = 40;
  map.home_x = 10;
  map.home_y = 10;
  return map;
}

mir2::HostConfig make_config(const mir2::HostConfig& loaded, std::string_view script,
                             std::vector<mir2::SpawnConfig> spawns = {},
                             std::string_view entry_action = {}) {
  const auto* source_npc = find_npc_by_script(loaded, script);
  if (source_npc == nullptr || source_npc->dialog_sections.empty()) {
    return {};
  }

  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 1;
  config.items = loaded.items;
  if (std::none_of(config.items.begin(), config.items.end(), [](const mir2::ItemConfig& item) {
        return item.name == "\xBD\xF0\xCC\xF5";
      })) {
    auto max_id = 0;
    for (const auto& item : config.items) {
      max_id = std::max(max_id, item.id);
    }
    mir2::ItemConfig gold_bar;
    gold_bar.id = max_id + 1;
    gold_bar.name = "\xBD\xF0\xCC\xF5";
    gold_bar.weight = 1;
    gold_bar.dura_max = 1000;
    gold_bar.equip_slot = -1;
    config.items.push_back(std::move(gold_bar));
  }
  if (std::none_of(config.items.begin(), config.items.end(), [](const mir2::ItemConfig& item) {
        return item.name == "\xE9\x87\x91\xE6\x9D\xA1";
      })) {
    auto max_id = 0;
    for (const auto& item : config.items) {
      max_id = std::max(max_id, item.id);
    }
    mir2::ItemConfig gold_bar;
    gold_bar.id = max_id + 1;
    gold_bar.name = "\xE9\x87\x91\xE6\x9D\xA1";
    gold_bar.weight = 1;
    gold_bar.dura_max = 1000;
    gold_bar.equip_slot = -1;
    config.items.push_back(std::move(gold_bar));
  }
  config.monsters = loaded.monsters;
  config.maps = {make_map("0"), make_map("1"), make_map("5"), make_map("0114"),
                 make_map("01132"), make_map("01141"), make_map("Q001"),
                 make_map("Q002"), make_map("Q003"), make_map("H001")};
  config.spawns = std::move(spawns);

  mir2::NpcConfig npc;
  npc.id = std::string(script);
  npc.map_id = "0";
  npc.name = "LiveP0Npc";
  npc.x = 11;
  npc.y = 10;
  npc.service = "none";
  if (entry_action.empty()) {
    npc.dialog_sections = source_npc->dialog_sections;
  } else {
    const auto section_it =
        std::find_if(source_npc->dialog_sections.begin(), source_npc->dialog_sections.end(),
                     [&](const mir2::NpcDialogSectionConfig& section) {
                       return section.action == entry_action;
                     });
    if (section_it != source_npc->dialog_sections.end()) {
      npc.dialog_sections.push_back({"@main", section_it->text});
    }
  }
  config.npcs.push_back(std::move(npc));
  return config;
}

mir2::SpawnConfig remote_spawn(std::string map_id) {
  mir2::SpawnConfig spawn;
  spawn.map_id = std::move(map_id);
  spawn.name = "Rat";
  spawn.x = 12;
  spawn.y = 12;
  spawn.max_hp = 12;
  spawn.count = 1;
  return spawn;
}

mir2::CharacterRecord make_character(std::int32_t gold = 100) {
  mir2::CharacterRecord character;
  character.account_id = "acct";
  character.character_name = "Hero";
  character.map_id = "0";
  character.x = 10;
  character.y = 10;
  character.gold = gold;
  character.ability.level = 40;
  character.ability.hp = 100;
  character.ability.max_hp = 100;
  character.ability.max_weight = 10000;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  return character;
}

mir2::LogicCommand enter_world(mir2::CharacterRecord character) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = kHeroSessionId;
  command.account_id = character.account_id;
  command.character_name = character.character_name;
  command.map_id = character.map_id;
  command.x = character.x;
  command.y = character.y;
  command.character = std::move(character);
  return command;
}

mir2::LogicCommand npc_command(mir2::LogicCommandKind kind, std::uint64_t npc_actor_id,
                               std::string action = {}) {
  mir2::LogicCommand command;
  command.kind = kind;
  command.session_id = kHeroSessionId;
  command.target_actor_id = npc_actor_id;
  command.text = std::move(action);
  return command;
}

mir2::RuntimeDispatch route_due(mir2::LogicRuntime& runtime, std::uint64_t& now_ms,
                                const mir2::LogicCommand& command) {
  static_cast<void>(runtime.route_logic_command(command));
  now_ms += 251;
  return runtime.tick(now_ms);
}

bool has_trace(const mir2::RuntimeDispatch& dispatch, std::string_view action, bool success,
               std::int32_t value, std::string_view label = {}) {
  return std::any_of(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                     [&](const mir2::LegacyRuntimeTrace& trace) {
                       return trace.stage == "LegacyScript" && trace.action == action &&
                              trace.success == success && trace.value == value &&
                              (label.empty() || trace.label == label);
                     });
}

bool has_trace_action(const mir2::RuntimeDispatch& dispatch, std::string_view action) {
  return std::any_of(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                     [&](const mir2::LegacyRuntimeTrace& trace) {
                       return trace.stage == "LegacyScript" && trace.action == action;
                     });
}

void print_script_traces(const mir2::RuntimeDispatch& dispatch) {
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == "LegacyScript") {
      std::cerr << "trace action=" << trace.action << " success=" << trace.success
                << " value=" << trace.value << " label=" << trace.label << '\n';
    }
  }
}

int run_checkmonmap_sample(const mir2::HostConfig& loaded) {
  auto config = make_config(loaded, "npc_scripts/market_def/9Ehw-01141.txt",
                            {remote_spawn("01141")});
  mir2::LogicRuntime runtime(std::move(config));
  runtime.initialize();
  std::uint64_t now_ms = 1000;
  static_cast<void>(runtime.route_logic_command(enter_world(make_character())));
  static_cast<void>(runtime.tick(now_ms));
  const auto dispatch =
      route_due(runtime, now_ms,
                npc_command(mir2::LogicCommandKind::click_npc, 2));
  if (!has_trace(dispatch, "condition", true, 1, "checkmonmap 01141 1")) {
    return fail("CHECKMONMAP", "9Ehw-01141.txt");
  }
  return 0;
}

int run_checkhum_monclear_sample(const mir2::HostConfig& loaded) {
  auto config = make_config(loaded, "npc_scripts/market_def/9Eht-0113.txt", {}, "@q706_1");
  mir2::LogicRuntime runtime(std::move(config));
  runtime.initialize();
  std::uint64_t now_ms = 1000;
  static_cast<void>(runtime.route_logic_command(enter_world(make_character())));
  static_cast<void>(runtime.tick(now_ms));
  const auto dispatch =
      route_due(runtime, now_ms,
                npc_command(mir2::LogicCommandKind::click_npc, 1));
  if (!has_trace(dispatch, "condition", false, 0, "checkhum 01132 1") ||
      !has_trace(dispatch, "monclear", true, 0, "Monclear 01132")) {
    return fail("CHECKHUM/MONCLEAR", "9Eht-0113.txt");
  }
  return 0;
}

int run_q001_sample(const mir2::HostConfig& loaded) {
  auto check_config = make_config(loaded, "npc_scripts/market_def/9Wqu-1.txt", {}, "@q307_6");
  mir2::LogicRuntime check_runtime(std::move(check_config));
  check_runtime.initialize();
  std::uint64_t now_ms = 1000;
  static_cast<void>(check_runtime.route_logic_command(enter_world(make_character())));
  static_cast<void>(check_runtime.tick(now_ms));
  const auto check_dispatch =
      route_due(check_runtime, now_ms,
                npc_command(mir2::LogicCommandKind::click_npc, 1));
  if (!has_trace(check_dispatch, "condition", false, 0, "checkhum Q001 1")) {
    return fail("CHECKHUM Q001", "9Wqu-1.txt");
  }

  auto clear_config =
      make_config(loaded, "npc_scripts/market_def/9Wqu-1.txt", {}, "@q307_6_1_1");
  mir2::LogicRuntime clear_runtime(std::move(clear_config));
  clear_runtime.initialize();
  now_ms = 1000;
  static_cast<void>(clear_runtime.route_logic_command(enter_world(make_character())));
  static_cast<void>(clear_runtime.tick(now_ms));
  const auto clear_dispatch =
      route_due(clear_runtime, now_ms,
                npc_command(mir2::LogicCommandKind::click_npc, 1));
  if (!has_trace(clear_dispatch, "monclear", true, 0, "Monclear Q001")) {
    print_script_traces(clear_dispatch);
    return fail("MONCLEAR Q001", "9Wqu-1.txt");
  }
  return 0;
}

int run_coin_sample(const mir2::HostConfig& loaded) {
  auto config = make_config(loaded, "npc_scripts/market_def/6Iwh-5.txt", {},
                            "@changegold_1");
  mir2::LogicRuntime runtime(std::move(config));
  runtime.initialize();
  std::uint64_t now_ms = 1000;
  static_cast<void>(runtime.route_logic_command(enter_world(make_character(1100000))));
  static_cast<void>(runtime.tick(now_ms));
  const auto dispatch =
      route_due(runtime, now_ms,
                npc_command(mir2::LogicCommandKind::click_npc, 1));
  if (!has_trace(dispatch, "take_gold", true, 1002000) ||
      has_trace_action(dispatch, "take_item_reject") ||
      !has_trace_action(dispatch, "give_item")) {
    print_script_traces(dispatch);
    return fail("legacy coin token", "6Iwh-5.txt");
  }
  const auto snapshot = runtime.snapshot_character_actor("Hero");
  if (!snapshot.has_value() || snapshot->gold != 98000) {
    return fail("gold snapshot", "6Iwh-5.txt");
  }
  return 0;
}

}  // namespace

int main() {
  const auto loaded = mir2::ConfigLoader{}.load(std::filesystem::path(MIR2_CONFIG_DIR));
  for (const auto script : {"npc_scripts/market_def/9Wqu-1.txt",
                            "npc_scripts/market_def/9Eht-0113.txt",
                            "npc_scripts/market_def/9Ehw-01141.txt",
                            "npc_scripts/market_def/6Iwh-5.txt"}) {
    if (find_npc_by_script(loaded, script) == nullptr) {
      return fail("fixture script", script);
    }
  }

  if (const auto result = run_checkmonmap_sample(loaded); result != 0) {
    return result;
  }
  if (const auto result = run_checkhum_monclear_sample(loaded); result != 0) {
    return result;
  }
  if (const auto result = run_q001_sample(loaded); result != 0) {
    return result;
  }
  if (const auto result = run_coin_sample(loaded); result != 0) {
    return result;
  }
  return 0;
}
