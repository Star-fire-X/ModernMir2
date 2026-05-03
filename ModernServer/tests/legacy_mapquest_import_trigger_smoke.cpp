#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

#include "config/config_loader.hpp"
#include "importer/legacy_importer.hpp"
#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

void write_text(const std::filesystem::path& path, const char* text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << text;
}

mir2::CharacterRecord make_hero() {
  mir2::CharacterRecord hero;
  hero.account_id = "guest";
  hero.character_name = "Hero";
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;
  hero.ability.level = 12;
  hero.ability.dc = mir2::make_word(20, 30);
  hero.ability.hp = 40;
  hero.ability.max_hp = 40;
  hero.ability.mp = 10;
  hero.ability.max_mp = 10;
  hero.ability.max_exp = 100;
  hero.ability.max_weight = 100;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;
  return hero;
}

mir2::LogicCommand enter_world() {
  auto hero = make_hero();
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = 7;
  command.account_id = hero.account_id;
  command.character_name = hero.character_name;
  command.map_id = hero.map_id;
  command.x = hero.x;
  command.y = hero.y;
  command.character = std::move(hero);
  return command;
}

mir2::LogicCommand make_attack(std::int32_t x, std::int32_t y) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::attack;
  command.session_id = 7;
  command.x = x;
  command.y = y;
  command.game_message.ident = mir2::kCmHit;
  return command;
}

std::optional<mir2::DecodedLegacyGamePacket> find_packet(const mir2::RuntimeDispatch& dispatch,
                                                         std::uint16_t ident) {
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return decoded;
    }
  }
  return std::nullopt;
}

std::optional<mir2::DecodedLegacyGamePacket> find_packet_by_body(
    const mir2::RuntimeDispatch& dispatch, std::uint16_t ident, std::string_view body) {
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident &&
        mir2::legacy_decode_string(decoded->body) == body) {
      return decoded;
    }
  }
  return std::nullopt;
}

bool has_packet(const mir2::RuntimeDispatch& dispatch, std::uint16_t ident) {
  return find_packet(dispatch, ident).has_value();
}

bool has_mapquest_trace(const mir2::RuntimeDispatch& dispatch, std::string_view source) {
  return std::any_of(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                     [&](const mir2::LegacyRuntimeTrace& trace) {
                       return trace.stage == "LegacyScript" &&
                              trace.action == "mapquest_trigger" && trace.label == source;
                     });
}

bool has_unsupported_script_trace(const mir2::RuntimeDispatch& dispatch) {
  return std::any_of(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                     [](const mir2::LegacyRuntimeTrace& trace) {
                       return trace.stage == "LegacyScript" &&
                              (trace.action == "unsupported_action" ||
                               trace.action == "unsupported_condition" ||
                               trace.action == "mapquest_missing_script");
                     });
}

}  // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() / "mir2_legacy_mapquest_import";
  const auto legacy = root / "legacy";
  const auto output = root / "output";
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);

  write_text(legacy / "!SetUp.txt",
             "[Setup]\n"
             "HomeMap=0\n"
             "HomeX=10\n"
             "HomeY=10\n");
  write_text(legacy / "Envir" / "StartPoint.txt", "0 10 10\n");
  write_text(legacy / "Envir" / "MapInfo.txt", "[0 QuestMap 0]\n");
  write_text(legacy / "Envir" / "MonGen.txt", "0 10 9 Oma 0 1 1 0\n");
  write_text(legacy / "Envir" / "MonZen.txt", "");
  write_text(legacy / "Envir" / "merchant.txt", "");
  write_text(legacy / "Envir" / "Npcs.txt", "");
  write_text(legacy / "Envir" / "MakeItem.txt", "Apple\n");
  write_text(legacy / "Envir" / "MonItems" / "Oma.txt", "1/1 Apple 1\n");
  write_text(legacy / "Envir" / "MapQuest.txt",
             "0 [0] 0 * * entry.txt\n"
             "0 [1] 1 Oma * death.txt\n"
             "0 [2] 1 * Apple pickup.txt\n");
  write_text(legacy / "Envir" / "MapQuest_def" / "entry.txt",
             "[@main]\n"
             "#ACT\n"
             "SET [1] 1\n");
  write_text(legacy / "Envir" / "MapQuest_def" / "death.txt",
             "[@main]\n"
             "#ACT\n"
             "SET [2] 1\n");
  write_text(legacy / "Envir" / "MapQuest_def" / "pickup.txt",
             "[@main]\n"
             "#ACT\n"
             "SET [3] 1\n");

  mir2::LegacyImporter importer;
  const auto report = importer.import_tree(legacy, output);
  assert(report.map_quest_count == 3);

  mir2::ConfigLoader loader;
  auto config = loader.load(output);
  assert(config.map_quests.size() == 3);
  assert(std::all_of(config.map_quests.begin(), config.map_quests.end(),
                     [](const mir2::MapQuestConfig& quest) {
                       return !quest.dialog_sections.empty();
                     }));

  for (auto& map : config.maps) {
    if (map.id == "0") {
      map.width = 20;
      map.height = 20;
      map.home_x = 10;
      map.home_y = 10;
    }
  }
  config.runtime.legacy_random_seed = 11;
  config.monsters.clear();
  mir2::MonsterDefConfig oma;
  oma.name = "Oma";
  oma.hp = 8;
  oma.dc = 1;
  oma.exp = 10;
  oma.ai_profile = mir2::MonsterAiProfile::aggressive;
  config.monsters.push_back(oma);
  config.spawns.clear();
  mir2::SpawnConfig spawn;
  spawn.map_id = "0";
  spawn.actor_type = "monster";
  spawn.name = "Oma";
  spawn.x = 10;
  spawn.y = 9;
  spawn.count = 1;
  spawn.legacy_group = true;
  config.spawns.push_back(spawn);

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  static_cast<void>(runtime.tick(1000));

  static_cast<void>(runtime.route_logic_command(enter_world()));
  const auto enter_dispatch = runtime.tick(1020);
  assert(find_packet(enter_dispatch, mir2::kSmNewMap).has_value());
  assert(has_mapquest_trace(enter_dispatch, "enter"));
  assert(!has_unsupported_script_trace(enter_dispatch));
  auto snapshot = runtime.snapshot_character_actor("Hero");
  assert(snapshot.has_value() && snapshot->quest_marks[1] == 1);

  static_cast<void>(runtime.route_logic_command(make_attack(10, 9)));
  const auto kill_dispatch = runtime.tick(1040);
  assert(has_packet(kill_dispatch, mir2::kSmDeath));
  assert(has_mapquest_trace(kill_dispatch, "monster_die"));
  assert(!has_unsupported_script_trace(kill_dispatch));
  snapshot = runtime.snapshot_character_actor("Hero");
  assert(snapshot.has_value() && snapshot->quest_marks[2] == 1);

  const auto apple_show = find_packet_by_body(kill_dispatch, mir2::kSmItemShow, "Apple");
  assert(apple_show.has_value());
  mir2::LogicCommand pickup;
  pickup.kind = mir2::LogicCommandKind::pickup_item;
  pickup.session_id = 7;
  pickup.x = apple_show->message.param;
  pickup.y = apple_show->message.tag;
  static_cast<void>(runtime.route_logic_command(pickup));
  const auto pickup_dispatch = runtime.tick(1060);
  assert(has_packet(pickup_dispatch, mir2::kSmItemHide));
  assert(has_packet(pickup_dispatch, mir2::kSmAddItem));
  assert(has_mapquest_trace(pickup_dispatch, "pickup"));
  assert(!has_unsupported_script_trace(pickup_dispatch));
  snapshot = runtime.snapshot_character_actor("Hero");
  assert(snapshot.has_value() && snapshot->quest_marks[3] == 1);

  std::filesystem::remove_all(root, ignored);
  return 0;
}
