#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "storage/repository.hpp"
#include "world/logic_runtime.hpp"

#undef assert
#define assert(expression)                                                       \
  do {                                                                           \
    if (!(expression)) {                                                         \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);     \
      std::abort();                                                              \
    }                                                                            \
  } while (false)

namespace {

constexpr std::int32_t kMagicBubbleStatusBit = 0x00100000;

mir2::MagicConfig legacy_magic(std::int32_t id) {
  mir2::MagicConfig magic;
  magic.id = id;
  magic.name = "PersistMagic" + std::to_string(id);
  magic.affect_players = true;
  magic.affect_monsters = true;
  magic.legacy.legacy_present = true;
  magic.legacy.effect_type = id;
  magic.legacy.effect = id;
  magic.legacy.spell = id == 1 ? 4 : 0;
  magic.legacy.min_power = 8;
  magic.legacy.max_power = 8;
  magic.legacy.job = 99;
  magic.legacy.need_level = {1, 1, 1, 1};
  magic.legacy.max_train = {500, 1500, 3000, 3000};
  magic.legacy.max_train_level = 3;
  magic.legacy.delay_time = 0;
  magic.legacy.def_spell = id == 31 ? 1 : 0;
  magic.legacy.def_min_power = 1;
  magic.legacy.def_max_power = 2;
  return magic;
}

mir2::SpawnConfig spawn_target() {
  mir2::SpawnConfig spawn;
  spawn.map_id = "0";
  spawn.actor_type = "monster";
  spawn.name = "TrainingDummy";
  spawn.x = 10;
  spawn.y = 8;
  spawn.respawn_ms = 30000;
  spawn.level = 1;
  spawn.max_hp = 100;
  spawn.attack_power = 0;
  spawn.defense = 0;
  spawn.magic_defense = 0;
  return spawn;
}

mir2::HostConfig base_config() {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 1;
  config.budgets.tick_ms = 20;
  config.maps.push_back(mir2::MapConfig{"0", "PersistMap", {}, 24, 24, 10, 10});
  config.spawns.push_back(spawn_target());
  config.magics.push_back(legacy_magic(1));
  config.magics.push_back(legacy_magic(31));
  return config;
}

mir2::CharacterRecord character() {
  mir2::CharacterRecord record;
  record.account_id = "persistacct";
  record.character_name = "PersistHero";
  record.map_id = "0";
  record.x = 10;
  record.y = 10;
  record.ability.level = 40;
  record.ability.hp = 50;
  record.ability.max_hp = 50;
  record.ability.mp = 200;
  record.ability.max_mp = 200;
  record.ability.mc = mir2::make_word(8, 8);
  record.ability.max_exp = 1000;
  record.ability.max_weight = 30;
  record.ability.max_wear_weight = 100;
  record.ability.max_hand_weight = 100;
  record.magics[0].magic_id = 1;
  record.magics[0].level = 0;
  record.magics[0].key = '1';
  record.magics[0].cur_train = 0;
  record.magics[1].magic_id = 31;
  record.magics[1].level = 1;
  record.magics[1].key = 'S';
  record.magics[1].cur_train = 77;
  return record;
}

mir2::LogicCommand enter(std::uint64_t session_id, mir2::CharacterRecord record) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = session_id;
  command.account_id = record.account_id;
  command.character_name = record.character_name;
  command.map_id = record.map_id;
  command.x = record.x;
  command.y = record.y;
  command.character = std::move(record);
  return command;
}

mir2::LogicCommand spell(std::uint64_t session_id, std::int32_t magic_id,
                         std::uint64_t target_actor_id = 0) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::spell;
  command.session_id = session_id;
  command.x = 10;
  command.y = 8;
  command.target_actor_id = target_actor_id;
  command.game_message.ident = mir2::kCmSpell;
  command.game_message.tag = static_cast<std::uint16_t>(magic_id);
  return command;
}

bool has_trace(const mir2::RuntimeDispatch& dispatch, const std::string& action) {
  return std::any_of(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                     [&](const mir2::LegacyRuntimeTrace& trace) {
                       return trace.action == action;
                     });
}

const mir2::LegacyUseMagicInfo* find_magic(const mir2::CharacterRecord& record,
                                           std::uint16_t magic_id) {
  const auto it = std::find_if(record.magics.begin(), record.magics.end(),
                               [&](const mir2::LegacyUseMagicInfo& magic) {
                                 return magic.magic_id == magic_id;
                               });
  return it == record.magics.end() ? nullptr : &*it;
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto temp_root =
      std::filesystem::temp_directory_path() / "mir2_skill_training_persistence_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root, ignored);

  mir2::Repository repository(temp_root / "mir2.sqlite");
  repository.ensure_schema(source_root / "schema" / "mir2.sql");

  const auto config = base_config();
  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  static_cast<void>(runtime.route_logic_command(enter(1501, character())));
  static_cast<void>(runtime.tick());

  auto trained = runtime.route_logic_command(spell(1501, 1, 1));
  const auto trained_tick = runtime.tick();
  trained.legacy_traces.insert(trained.legacy_traces.end(), trained_tick.legacy_traces.begin(),
                               trained_tick.legacy_traces.end());
  assert(has_trace(trained, "train_skill"));

  auto snapshot = runtime.snapshot_character_actor("PersistHero");
  assert(snapshot.has_value());
  const auto* fireball = find_magic(*snapshot, 1);
  assert(fireball != nullptr && fireball->cur_train > 0 && fireball->key == '1');
  const auto trained_value = fireball->cur_train;
  repository.save_character(*snapshot);

  const auto loaded = repository.load_character("persistacct", "PersistHero");
  assert(loaded.has_value());
  const auto* loaded_fireball = find_magic(*loaded, 1);
  const auto* loaded_shield = find_magic(*loaded, 31);
  assert(loaded_fireball != nullptr && loaded_fireball->cur_train == trained_value &&
         loaded_fireball->key == '1');
  assert(loaded_shield != nullptr && loaded_shield->level == 1 && loaded_shield->key == 'S' &&
         loaded_shield->cur_train == 77);

  mir2::LogicRuntime relog_runtime(config);
  relog_runtime.initialize();
  static_cast<void>(relog_runtime.route_logic_command(enter(1502, *loaded)));
  static_cast<void>(relog_runtime.tick());
  const auto relog_snapshot = relog_runtime.snapshot_character_actor("PersistHero");
  assert(relog_snapshot.has_value());
  const auto* relog_fireball = find_magic(*relog_snapshot, 1);
  assert(relog_fireball != nullptr && relog_fireball->cur_train == trained_value &&
         relog_fireball->key == '1');

  auto shield = relog_runtime.route_logic_command(spell(1502, 31));
  const auto shield_tick = relog_runtime.tick();
  shield.legacy_traces.insert(shield.legacy_traces.end(), shield_tick.legacy_traces.begin(),
                              shield_tick.legacy_traces.end());
  assert(has_trace(shield, "magic_bubble"));
  const auto active_shield = relog_runtime.snapshot_character_actor("PersistHero");
  assert(active_shield.has_value() && (active_shield->status & kMagicBubbleStatusBit) != 0);
  repository.save_character(*active_shield);

  const auto stale_status = repository.load_character("persistacct", "PersistHero");
  assert(stale_status.has_value() && (stale_status->status & kMagicBubbleStatusBit) != 0);

  mir2::LogicRuntime stale_relog_runtime(config);
  stale_relog_runtime.initialize();
  static_cast<void>(stale_relog_runtime.route_logic_command(enter(1503, *stale_status)));
  static_cast<void>(stale_relog_runtime.tick());
  const auto cleaned = stale_relog_runtime.snapshot_character_actor("PersistHero");
  assert(cleaned.has_value() && (cleaned->status & kMagicBubbleStatusBit) == 0);
  const auto* cleaned_fireball = find_magic(*cleaned, 1);
  assert(cleaned_fireball != nullptr && cleaned_fireball->cur_train == trained_value &&
         cleaned_fireball->key == '1');

  std::filesystem::remove_all(temp_root, ignored);
  return 0;
}
