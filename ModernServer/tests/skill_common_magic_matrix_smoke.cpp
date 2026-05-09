#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
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

mir2::MagicConfig legacy_magic(std::int32_t id) {
  mir2::MagicConfig magic;
  magic.id = id;
  magic.name = "Magic" + std::to_string(id);
  magic.affect_players = true;
  magic.affect_monsters = true;
  magic.legacy.legacy_present = true;
  magic.legacy.effect_type = id;
  magic.legacy.effect = id;
  magic.legacy.spell = 0;
  magic.legacy.min_power = 10;
  magic.legacy.max_power = 10;
  magic.legacy.job = 99;
  magic.legacy.need_level = {1, 1, 1, 1};
  magic.legacy.max_train = {500, 1500, 3000, 3000};
  magic.legacy.max_train_level = 3;
  magic.legacy.delay_time = 0;
  magic.legacy.def_spell = 0;
  magic.legacy.def_min_power = 10;
  magic.legacy.def_max_power = 10;
  return magic;
}

mir2::HostConfig base_config() {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 1;
  config.budgets.tick_ms = 20;
  mir2::MapConfig map{"0", "MagicMatrix", {}, 24, 24, 10, 10};
  map.allow_pk = true;
  map.fight_zone = true;
  config.maps.push_back(map);
  for (const auto id : {1, 2, 5, 8, 9, 10, 11, 23, 24, 29, 31, 33, 35, 36}) {
    config.magics.push_back(legacy_magic(id));
  }
  config.items.push_back(mir2::ItemConfig{501, "Bujuk", 1, 10, 25, 5, 1, 1000, 9});
  return config;
}

mir2::SpawnConfig spawn(std::string name, std::int32_t x, std::int32_t y,
                        std::int32_t hp = 100, std::int32_t life_attrib = 0) {
  mir2::SpawnConfig spawn;
  spawn.map_id = "0";
  spawn.actor_type = "monster";
  spawn.name = std::move(name);
  spawn.x = x;
  spawn.y = y;
  spawn.respawn_ms = 30000;
  spawn.level = 1;
  spawn.max_hp = hp;
  spawn.life_attrib = life_attrib;
  return spawn;
}

mir2::CharacterRecord character(std::string name, std::vector<std::int32_t> magics,
                                std::int32_t x = 10, std::int32_t y = 10,
                                std::int32_t hp = 50) {
  mir2::CharacterRecord record;
  record.account_id = "acct_" + name;
  record.character_name = std::move(name);
  record.map_id = "0";
  record.x = x;
  record.y = y;
  record.ability.level = 40;
  record.ability.hp = static_cast<std::uint16_t>(hp);
  record.ability.max_hp = 50;
  record.ability.mp = 200;
  record.ability.max_mp = 200;
  record.ability.mc = mir2::make_word(8, 8);
  record.ability.sc = mir2::make_word(8, 8);
  record.ability.max_exp = 1000;
  record.ability.max_weight = 30;
  record.ability.max_wear_weight = 100;
  record.ability.max_hand_weight = 100;
  record.attack_mode = 0;
  record.equipped_items[mir2::kEquipBujuk].index = 501;
  record.equipped_items[mir2::kEquipBujuk].make_index = 9001;
  record.equipped_items[mir2::kEquipBujuk].dura = 1000;
  record.equipped_items[mir2::kEquipBujuk].dura_max = 1000;
  for (std::size_t index = 0; index < magics.size() && index < record.magics.size(); ++index) {
    record.magics[index].magic_id = static_cast<std::uint16_t>(magics[index]);
    record.magics[index].level = 1;
  }
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
                         std::int32_t x = 0, std::int32_t y = 0,
                         std::uint64_t target_actor_id = 0) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::spell;
  command.session_id = session_id;
  command.x = x;
  command.y = y;
  command.target_actor_id = target_actor_id;
  command.game_message.ident = mir2::kCmSpell;
  command.game_message.tag = static_cast<std::uint16_t>(magic_id);
  return command;
}

void append(mir2::RuntimeDispatch& target, mir2::RuntimeDispatch source) {
  target.session_events.insert(target.session_events.end(),
                               std::make_move_iterator(source.session_events.begin()),
                               std::make_move_iterator(source.session_events.end()));
  target.legacy_traces.insert(target.legacy_traces.end(),
                              std::make_move_iterator(source.legacy_traces.begin()),
                              std::make_move_iterator(source.legacy_traces.end()));
}

mir2::RuntimeDispatch advance(mir2::LogicRuntime& runtime, int ticks) {
  mir2::RuntimeDispatch combined;
  for (int index = 0; index < ticks; ++index) {
    append(combined, runtime.tick());
  }
  return combined;
}

bool has_trace(const mir2::RuntimeDispatch& dispatch, const std::string& action,
               std::int32_t value = -1) {
  return std::any_of(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                     [&](const mir2::LegacyRuntimeTrace& trace) {
                       return trace.action == action && (value < 0 || trace.value == value);
                     });
}

std::int32_t count_trace(const mir2::RuntimeDispatch& dispatch, const std::string& action,
                         std::int32_t value) {
  return static_cast<std::int32_t>(std::count_if(
      dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
      [&](const mir2::LegacyRuntimeTrace& trace) {
        return trace.action == action && trace.value == value;
      }));
}

}  // namespace

int main() {
  {
    auto config = base_config();
    config.spawns.push_back(spawn("ColdTarget", 10, 8, 100, 1));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1301, character("Cold", {35}))));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(spell(1301, 35, 10, 8, 1));
    append(dispatch, runtime.tick());
    assert(has_trace(dispatch, "mag_struck_queued", 35));
    append(dispatch, advance(runtime, 40));
    assert(has_trace(dispatch, "mag_struck", 35));
    assert(has_trace(dispatch, "train_skill"));
  }

  {
    auto config = base_config();
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1311, character("Armor", {36}))));
    static_cast<void>(runtime.route_logic_command(enter(1312, character("Friend", {}, 11, 10))));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(spell(1311, 36, 10, 10));
    append(dispatch, runtime.tick());
    assert(has_trace(dispatch, "dc_up"));
    assert(has_trace(dispatch, "train_skill"));
    const auto armor = runtime.snapshot_character_actor("Armor");
    const auto friend_record = runtime.snapshot_character_actor("Friend");
    assert(armor.has_value() && friend_record.has_value());
    assert((armor->status & 0x00400000) == 0);
    assert((friend_record->status & 0x00400000) == 0);
  }

  {
    auto config = base_config();
    config.spawns.push_back(spawn("LineA", 10, 7));
    config.spawns.push_back(spawn("AreaA", 10, 8));
    config.spawns.push_back(spawn("AreaB", 11, 8));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        enter(1321, character("Matrix", {1, 2, 5, 8, 9, 10, 11, 23, 24, 29, 31, 33}))));
    static_cast<void>(runtime.route_logic_command(enter(1322, character("Wounded", {}, 11, 10, 10))));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(spell(1321, 1, 10, 8, 2));
    append(dispatch, runtime.tick());
    append(dispatch, advance(runtime, 40));
    assert(has_trace(dispatch, "mag_struck", 1));

    append(dispatch, runtime.route_logic_command(spell(1321, 29, 10, 10)));
    append(dispatch, runtime.tick());
    assert(count_trace(dispatch, "healing_queued", 29) >= 1);

    append(dispatch, runtime.route_logic_command(spell(1321, 23, 10, 8)));
    append(dispatch, runtime.tick());
    assert(count_trace(dispatch, "mag_struck", 23) >= 1);
  }

  return 0;
}
