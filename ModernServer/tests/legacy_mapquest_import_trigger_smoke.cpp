#include <algorithm>
#include <cassert>
#include <cstdint>
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

mir2::CharacterRecord make_hero(std::string name, std::int32_t x, std::int32_t y,
                                bool alive, bool kill_gate_enabled) {
  mir2::CharacterRecord hero;
  hero.account_id = "acct_" + name;
  hero.character_name = std::move(name);
  hero.map_id = "0";
  hero.x = x;
  hero.y = y;
  hero.ability.level = 12;
  hero.ability.dc = mir2::make_word(20, 30);
  hero.ability.hp = alive ? 40 : 0;
  hero.ability.max_hp = 40;
  hero.ability.mp = 10;
  hero.ability.max_mp = 10;
  hero.ability.max_exp = 100;
  hero.ability.max_weight = 100;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;
  if (kill_gate_enabled) {
    hero.quest_marks[0] = static_cast<std::uint8_t>(hero.quest_marks[0] | 0x40U);
  }
  return hero;
}

mir2::LogicCommand enter_world(std::uint64_t session_id,
                               const mir2::CharacterRecord& character) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = session_id;
  command.account_id = character.account_id;
  command.character_name = character.character_name;
  command.map_id = character.map_id;
  command.x = character.x;
  command.y = character.y;
  command.character = character;
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
    const mir2::RuntimeDispatch& dispatch, std::uint16_t ident,
    std::string_view body) {
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

std::size_t count_mapquest_traces(const mir2::RuntimeDispatch& dispatch,
                                  std::string_view source) {
  return static_cast<std::size_t>(std::count_if(
      dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
      [&](const mir2::LegacyRuntimeTrace& trace) {
        return trace.stage == "LegacyScript" &&
               trace.action == "mapquest_trigger" && trace.label == source;
      }));
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

bool quest_mark_set(const mir2::CharacterRecord& character, std::int32_t index) {
  if (index <= 0) {
    return false;
  }
  const auto zero_based = index - 1;
  const auto byte_index = static_cast<std::size_t>(zero_based / 8);
  if (byte_index >= character.quest_marks.size()) {
    return false;
  }
  const auto bit = static_cast<std::uint8_t>(0x80U >> (zero_based % 8));
  return (character.quest_marks[byte_index] & bit) != 0U;
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
  write_text(legacy / "Envir" / "MonGen.txt", "0 11 11 TestMon 0 1 1 0\n");
  write_text(legacy / "Envir" / "MonZen.txt", "");
  write_text(legacy / "Envir" / "merchant.txt", "");
  write_text(legacy / "Envir" / "Npcs.txt", "");
  write_text(legacy / "Envir" / "MakeItem.txt", "Apple\n");
  write_text(legacy / "Envir" / "MapQuest.txt",
             "0 [1] 0 * * entry.txt\n"
             "0 [2] 2 赤月恶魔 * death.txt\n"
             "0 [3] -1 * 苹果 pickup.txt\n"
             "0 [4] -1 赤月恶魔 * group.txt group\n");
  write_text(legacy / "Envir" / "MapQuest_def" / "entry.txt",
             "[@main]\n"
             "#ACT\n"
             "SET [8] 1\n");
  write_text(legacy / "Envir" / "MapQuest_def" / "death.txt",
             "[@main]\n"
             "#ACT\n"
             "SET [9] 1\n");
  write_text(legacy / "Envir" / "MapQuest_def" / "pickup.txt",
             "[@main]\n"
             "#ACT\n"
             "SET [10] 1\n");
  write_text(legacy / "Envir" / "MapQuest_def" / "group.txt",
             "[@main]\n"
             "#ACT\n"
             "SET [11] 1\n");

  mir2::LegacyImporter importer;
  const auto report = importer.import_tree(legacy, output);
  assert(report.map_quest_count == 4);

  mir2::ConfigLoader loader;
  auto config = loader.load(output);
  assert(config.map_quests.size() == 4);
  assert(std::all_of(config.map_quests.begin(), config.map_quests.end(),
                     [](const mir2::MapQuestConfig& quest) {
                       return !quest.dialog_sections.empty();
                     }));

  auto find_quest = [&](std::int32_t set_number) -> const mir2::MapQuestConfig* {
    const auto it =
        std::find_if(config.map_quests.begin(), config.map_quests.end(),
                     [&](const mir2::MapQuestConfig& quest) {
                       return quest.set_number == set_number;
                     });
    return it == config.map_quests.end() ? nullptr : &*it;
  };
  const auto* death_quest = find_quest(2);
  const auto* pickup_quest = find_quest(3);
  const auto* group_quest = find_quest(4);
  assert(death_quest != nullptr);
  assert(pickup_quest != nullptr);
  assert(group_quest != nullptr);
  assert(death_quest->value == 1);
  assert(pickup_quest->value == 0);
  assert(group_quest->value == 0 && group_quest->enable_group);
  assert(death_quest->monster_name == "赤月恶魔");
  assert(pickup_quest->item_name == "苹果");

  for (auto& map : config.maps) {
    if (map.id == "0") {
      map.width = 40;
      map.height = 40;
      map.home_x = 10;
      map.home_y = 10;
    }
  }
  config.runtime.legacy_random_seed = 11;
  config.items.clear();
  config.items.push_back(mir2::ItemConfig{1, "苹果", 1, 1, 1, 0, 2, 1, -1, 0, 0});
  config.monsters.clear();
  mir2::MonsterDefConfig demon;
  demon.name = "赤月恶魔";
  demon.hp = 8;
  demon.dc = 1;
  demon.exp = 10;
  demon.ai_profile = mir2::MonsterAiProfile::aggressive;
  config.monsters.push_back(demon);
  config.monster_drops.clear();
  config.monster_drops.push_back(mir2::MonsterDropConfig{"赤月恶魔", 1, 1, "苹果", 1});
  config.spawns.clear();
  mir2::SpawnConfig spawn;
  spawn.map_id = "0";
  spawn.actor_type = "monster";
  spawn.name = "赤月恶魔";
  spawn.x = 11;
  spawn.y = 11;
  spawn.count = 1;
  spawn.legacy_group = true;
  config.spawns.push_back(spawn);

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  static_cast<void>(runtime.tick(1000));
  static_cast<void>(runtime.tick(1201));

  const auto attacker = make_hero("Hero", 10, 10, true, true);
  const auto near = make_hero("Near", 12, 10, true, true);
  const auto far = make_hero("Far", 30, 10, true, true);
  const auto dead = make_hero("Dead", 11, 10, false, true);

  static_cast<void>(runtime.route_logic_command(enter_world(7, attacker)));
  const auto attacker_enter = runtime.tick(1220);
  assert(find_packet(attacker_enter, mir2::kSmNewMap).has_value());
  assert(!has_mapquest_trace(attacker_enter, "enter"));

  static_cast<void>(runtime.route_logic_command(enter_world(8, near)));
  const auto near_enter = runtime.tick(1230);
  assert(find_packet(near_enter, mir2::kSmNewMap).has_value());
  assert(!has_mapquest_trace(near_enter, "enter"));

  static_cast<void>(runtime.route_logic_command(enter_world(9, far)));
  const auto far_enter = runtime.tick(1240);
  assert(find_packet(far_enter, mir2::kSmNewMap).has_value());
  assert(!has_mapquest_trace(far_enter, "enter"));

  static_cast<void>(runtime.route_logic_command(enter_world(10, dead)));
  const auto dead_enter = runtime.tick(1250);
  assert(!has_mapquest_trace(dead_enter, "enter"));

  auto hero_snapshot = runtime.snapshot_character_actor("Hero");
  assert(hero_snapshot.has_value());
  assert(!quest_mark_set(*hero_snapshot, 8));

  static_cast<void>(runtime.route_logic_command(make_attack(11, 11)));
  const auto kill_dispatch = runtime.tick(1300);
  assert(has_packet(kill_dispatch, mir2::kSmDeath));
  assert(has_mapquest_trace(kill_dispatch, "monster_die"));
  assert(count_mapquest_traces(kill_dispatch, "monster_die") == 3);
  assert(!has_unsupported_script_trace(kill_dispatch));

  auto near_snapshot = runtime.snapshot_character_actor("Near");
  auto far_snapshot = runtime.snapshot_character_actor("Far");
  auto dead_snapshot = runtime.snapshot_character_actor("Dead");
  hero_snapshot = runtime.snapshot_character_actor("Hero");
  assert(hero_snapshot.has_value() && near_snapshot.has_value() && far_snapshot.has_value() &&
         dead_snapshot.has_value());
  assert(quest_mark_set(*hero_snapshot, 9));
  assert(quest_mark_set(*hero_snapshot, 11));
  assert(!quest_mark_set(*hero_snapshot, 8));
  assert(!quest_mark_set(*near_snapshot, 9));
  assert(quest_mark_set(*near_snapshot, 11));
  assert(!quest_mark_set(*far_snapshot, 9));
  assert(!quest_mark_set(*far_snapshot, 11));
  assert(!quest_mark_set(*dead_snapshot, 9));
  assert(!quest_mark_set(*dead_snapshot, 11));

  const auto apple_show = find_packet_by_body(kill_dispatch, mir2::kSmItemShow, "苹果");
  assert(apple_show.has_value());
  mir2::LogicCommand pickup;
  pickup.kind = mir2::LogicCommandKind::pickup_item;
  pickup.session_id = 7;
  pickup.x = apple_show->message.param;
  pickup.y = apple_show->message.tag;
  static_cast<void>(runtime.route_logic_command(pickup));
  const auto pickup_dispatch = runtime.tick(2000);
  assert(has_packet(pickup_dispatch, mir2::kSmItemHide));
  assert(has_packet(pickup_dispatch, mir2::kSmAddItem));
  assert(has_mapquest_trace(pickup_dispatch, "pickup"));
  assert(!has_unsupported_script_trace(pickup_dispatch));

  hero_snapshot = runtime.snapshot_character_actor("Hero");
  assert(hero_snapshot.has_value() && quest_mark_set(*hero_snapshot, 10));

  std::filesystem::remove_all(root, ignored);
  return 0;
}
