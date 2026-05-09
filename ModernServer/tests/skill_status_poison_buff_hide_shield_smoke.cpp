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
#include "world/game_object.hpp"
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

constexpr std::int32_t kTransparentStatusBit = 0x00800000;
constexpr std::int32_t kDefenceStatusBit = 0x00400000;
constexpr std::int32_t kMagDefenceStatusBit = 0x00200000;
constexpr std::int32_t kMagicBubbleStatusBit = 0x00100000;
constexpr std::uint32_t kPoisonDecHealthStatusBit = 0x80000000u;
constexpr std::uint32_t kPoisonDamageArmorStatusBit = 0x40000000u;
constexpr std::uint32_t kPoisonStoneStatusBit = 0x04000000u;

mir2::MagicConfig magic(std::int32_t id) {
  mir2::MagicConfig magic;
  magic.id = id;
  magic.name = "Status" + std::to_string(id);
  magic.affect_players = true;
  magic.affect_monsters = true;
  magic.legacy.legacy_present = true;
  magic.legacy.effect_type = id;
  magic.legacy.effect = id;
  magic.legacy.spell = 0;
  magic.legacy.min_power = 8;
  magic.legacy.max_power = 8;
  magic.legacy.job = 99;
  magic.legacy.need_level = {1, 1, 1, 1};
  magic.legacy.max_train = {500, 1500, 3000, 3000};
  magic.legacy.max_train_level = 3;
  magic.legacy.delay_time = 0;
  magic.legacy.def_spell = 0;
  magic.legacy.def_min_power = 1;
  magic.legacy.def_max_power = 2;
  return magic;
}

mir2::HostConfig base_config() {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 1;
  config.budgets.tick_ms = 1000;
  config.maps.push_back(mir2::MapConfig{"0", "StatusMap", {}, 24, 24, 10, 10});
  for (const auto id : {6, 18, 19, 31}) {
    config.magics.push_back(magic(id));
  }
  config.items.push_back(mir2::ItemConfig{501, "Bujuk", 1, 10, 25, 5, 1, 1000, 9});
  config.items.push_back(mir2::ItemConfig{502, "Green Poison", 1, 10, 25, 1, 1, 1000, 9});
  return config;
}

mir2::SpawnConfig spawn(std::string name, std::int32_t x, std::int32_t y,
                        std::int32_t hp = 100) {
  mir2::SpawnConfig spawn;
  spawn.map_id = "0";
  spawn.actor_type = "monster";
  spawn.name = std::move(name);
  spawn.x = x;
  spawn.y = y;
  spawn.respawn_ms = 30000;
  spawn.level = 1;
  spawn.max_hp = hp;
  spawn.attack_power = 0;
  spawn.defense = 0;
  spawn.magic_defense = 0;
  spawn.exp_reward = 10;
  return spawn;
}

mir2::CharacterRecord character(std::string name, std::vector<std::int32_t> magics,
                                std::int32_t item_index = 501) {
  mir2::CharacterRecord record;
  record.account_id = "acct_" + name;
  record.character_name = std::move(name);
  record.map_id = "0";
  record.x = 10;
  record.y = 10;
  record.ability.level = 40;
  record.ability.hp = 50;
  record.ability.max_hp = 50;
  record.ability.mp = 200;
  record.ability.max_mp = 200;
  record.ability.mc = mir2::make_word(8, 8);
  record.ability.sc = mir2::make_word(8, 8);
  record.ability.max_exp = 1000;
  record.ability.max_weight = 30;
  record.ability.max_wear_weight = 100;
  record.ability.max_hand_weight = 100;
  if (item_index > 0) {
    record.equipped_items[mir2::kEquipBujuk].index = static_cast<std::uint16_t>(item_index);
    record.equipped_items[mir2::kEquipBujuk].make_index = 9100 + item_index;
    record.equipped_items[mir2::kEquipBujuk].dura = 1000;
    record.equipped_items[mir2::kEquipBujuk].dura_max = 1000;
  }
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

mir2::LogicCommand attack(std::uint64_t session_id, std::int32_t x, std::int32_t y) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::attack;
  command.session_id = session_id;
  command.x = x;
  command.y = y;
  command.dir = 0;
  command.game_message.ident = mir2::kCmHit;
  return command;
}

mir2::LogicCommand walk(std::uint64_t session_id, std::int32_t x, std::int32_t y) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::walk;
  command.session_id = session_id;
  command.x = x;
  command.y = y;
  return command;
}

mir2::LogicCommand logout(std::uint64_t session_id) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::logout;
  command.session_id = session_id;
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

bool has_packet(const mir2::RuntimeDispatch& dispatch, std::uint16_t ident) {
  return std::any_of(dispatch.session_events.begin(), dispatch.session_events.end(),
                     [&](const mir2::SessionEvent& event) {
                       const auto decoded = mir2::decode_legacy_game_packet(event.packet);
                       return decoded.has_value() && decoded->message.ident == ident;
                     });
}

std::optional<mir2::CharacterRecord> saved_character(
    const mir2::RuntimeDispatch& dispatch,
    const std::string& name) {
  for (const auto& request : dispatch.persist_requests) {
    if (request.kind == mir2::PersistRequestKind::save_character &&
        request.character.character_name == name) {
      return request.character;
    }
  }
  return std::nullopt;
}

}  // namespace

int main() {
  {
    auto config = base_config();
    config.spawns.push_back(spawn("Poisoned", 10, 8, 100));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        enter(1401, character("Poisoner", {6}, 502))));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(spell(1401, 6, 10, 8, 1));
    append(dispatch, runtime.tick());
    assert(has_trace(dispatch, "poison_powder_used", 1));
    append(dispatch, advance(runtime, 3));
    assert(has_trace(dispatch, "poison_apply", 0));
    append(dispatch, advance(runtime, 4));
    assert(has_packet(dispatch, mir2::kSmStruck) || has_packet(dispatch, mir2::kSmDeath));
  }

  {
    auto config = base_config();
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1411, character("Hidden", {18, 31}))));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(spell(1411, 18));
    append(dispatch, runtime.tick());
    auto snapshot = runtime.snapshot_character_actor("Hidden");
    assert(snapshot.has_value() && (snapshot->status & kTransparentStatusBit) != 0);
    assert(has_packet(dispatch, mir2::kSmCharStatusChanged));
    append(dispatch, runtime.route_logic_command(attack(1411, 10, 9)));
    append(dispatch, runtime.tick());
    snapshot = runtime.snapshot_character_actor("Hidden");
    assert(snapshot.has_value() && (snapshot->status & kTransparentStatusBit) == 0);
    assert(has_packet(dispatch, mir2::kSmCharStatusChanged));
  }

  {
    auto config = base_config();
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1421, character("Bubble", {31}))));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(spell(1421, 31));
    append(dispatch, runtime.tick());
    auto snapshot = runtime.snapshot_character_actor("Bubble");
    assert(snapshot.has_value() && (snapshot->status & kMagicBubbleStatusBit) != 0);
    assert(has_packet(dispatch, mir2::kSmCharStatusChanged));
    for (int index = 0; index < 60; ++index) {
      append(dispatch, runtime.tick());
      snapshot = runtime.snapshot_character_actor("Bubble");
      if (snapshot.has_value() && (snapshot->status & kMagicBubbleStatusBit) == 0) {
        break;
      }
    }
    snapshot = runtime.snapshot_character_actor("Bubble");
    assert(snapshot.has_value() && (snapshot->status & kMagicBubbleStatusBit) == 0);
    assert(has_packet(dispatch, mir2::kSmCharStatusChanged));
  }

  {
    auto record = character("StoneTarget", {}, 0);
    mir2::Player player(1, 1501, std::move(record));
    assert(player.apply_legacy_poison(5, 2, 0, 1, 99, 1));
    auto status = static_cast<std::uint32_t>(player.character().status);
    assert((status & kPoisonStoneStatusBit) != 0);
    assert((status & kPoisonDamageArmorStatusBit) == 0);

    const auto tick_result = player.tick_status_effects(4);
    status = static_cast<std::uint32_t>(player.character().status);
    assert(tick_result.legacy_status_changed);
    assert((status & kPoisonStoneStatusBit) == 0);
    assert((status & kPoisonDamageArmorStatusBit) == 0);
  }

  {
    auto record = character("BuffLifecycle", {}, 0);
    mir2::Player player(1, 1502, std::move(record));
    assert(player.apply_legacy_poison(0, 20, 1, 5, 99, 1));
    assert(player.apply_legacy_poison(1, 20, 0, 1, 99, 1));
    assert(player.apply_legacy_poison(5, 20, 0, 1, 99, 1));
    assert(player.activate_legacy_transparent(20, 1));
    assert(player.activate_legacy_defence_up(20, 1));
    assert(player.activate_legacy_magic_defence_up(20, 1));
    assert(player.activate_legacy_magic_bubble(1, 1, 21));
    auto status = static_cast<std::uint32_t>(player.character().status);
    assert((status & kPoisonDecHealthStatusBit) != 0);
    assert((status & kPoisonDamageArmorStatusBit) != 0);
    assert((status & kPoisonStoneStatusBit) != 0);
    assert((status & static_cast<std::uint32_t>(kTransparentStatusBit)) != 0);
    assert((status & static_cast<std::uint32_t>(kDefenceStatusBit)) != 0);
    assert((status & static_cast<std::uint32_t>(kMagDefenceStatusBit)) != 0);
    assert((status & static_cast<std::uint32_t>(kMagicBubbleStatusBit)) != 0);

    static_cast<void>(player.mark_dead(1000));
    status = static_cast<std::uint32_t>(player.character().status);
    assert((status & kPoisonDecHealthStatusBit) == 0);
    assert((status & kPoisonDamageArmorStatusBit) == 0);
    assert((status & kPoisonStoneStatusBit) == 0);
    assert((status & static_cast<std::uint32_t>(kTransparentStatusBit)) == 0);
    assert((status & static_cast<std::uint32_t>(kDefenceStatusBit)) == 0);
    assert((status & static_cast<std::uint32_t>(kMagDefenceStatusBit)) == 0);
    assert((status & static_cast<std::uint32_t>(kMagicBubbleStatusBit)) == 0);
    const auto tick_result = player.tick_status_effects(6);
    assert(tick_result.damage == 0);
  }

  {
    auto record = character("LeaveMapPolicy", {}, 0);
    mir2::Player player(1, 1503, std::move(record));
    assert(player.apply_legacy_poison(1, 20, 0, 1, 99, 1));
    assert(player.apply_legacy_poison(5, 20, 0, 1, 99, 1));
    assert(player.activate_legacy_transparent(20, 1));
    assert(player.activate_legacy_magic_bubble(1, 1, 21));
    const auto clear = player.clear_legacy_buffs_on_leave_map(2);
    assert(clear.legacy_status_changed);
    auto status = static_cast<std::uint32_t>(player.character().status);
    assert((status & kPoisonDamageArmorStatusBit) != 0);
    assert((status & kPoisonStoneStatusBit) == 0);
    assert((status & static_cast<std::uint32_t>(kTransparentStatusBit)) == 0);
    assert((status & static_cast<std::uint32_t>(kMagicBubbleStatusBit)) != 0);

    const auto logout_clear = player.clear_legacy_buffs_on_logout(3);
    assert(logout_clear.legacy_status_changed);
    status = static_cast<std::uint32_t>(player.character().status);
    assert((status & kPoisonDamageArmorStatusBit) == 0);
    assert((status & static_cast<std::uint32_t>(kMagicBubbleStatusBit)) == 0);
  }

  {
    auto record = character("AbilityExpire", {}, 0);
    mir2::Player player(1, 1504, std::move(record));
    assert(player.activate_legacy_defence_up(2, 1));
    assert(player.activate_legacy_magic_defence_up(2, 1));
    const auto tick_result = player.tick_status_effects(4);
    const auto status = static_cast<std::uint32_t>(player.character().status);
    assert(tick_result.legacy_status_changed);
    assert(tick_result.ability_changed);
    assert((status & static_cast<std::uint32_t>(kDefenceStatusBit)) == 0);
    assert((status & static_cast<std::uint32_t>(kMagDefenceStatusBit)) == 0);
  }

  {
    auto record = character("DcUp", {}, 0);
    record.ability.dc = mir2::make_word(2, 5);
    mir2::Player player(1, 1505, std::move(record));
    assert(player.melee_power() == 5);
    assert(player.activate_legacy_dc_up(2, 1, 3));
    assert(player.legacy_dc_up_bonus() == 3);
    assert(player.melee_power() == 8);
    const auto tick_result = player.tick_status_effects(4);
    assert(tick_result.ability_changed);
    assert(player.legacy_dc_up_bonus() == 0);
    assert(player.melee_power() == 5);

    mir2::Monster monster(2, "DcUpSlave", "0", 10, 10, 1, 20, 0, 1, 4,
                          0, 0, 0, 0, 10);
    assert(monster.dc_max() == 4);
    assert(monster.activate_legacy_dc_up(2, 1, 3));
    assert(monster.dc_max() == 7);
    const auto monster_tick = monster.tick_status_effects(4);
    assert(monster_tick.ability_changed);
    assert(monster.dc_max() == 4);
  }

  {
    mir2::Monster monster(1, "LifecycleMonster", "0", 10, 10, 1, 20, 0, 0, 0,
                          0, 0, 0, 0, 10);
    assert(monster.apply_legacy_poison(0, 20, 1, 1, 99, 1));
    assert(monster.apply_legacy_poison(5, 20, 0, 1, 99, 1));
    static_cast<void>(monster.mark_legacy_death(1000));
    const auto tick_result = monster.tick_status_effects(2);
    assert(tick_result.damage == 0);
    assert(!tick_result.legacy_status_changed);
  }

  {
    auto config = base_config();
    config.maps[0].gates.push_back(mir2::MapGateConfig{11, 10, "1", 5, 5, false});
    config.maps.push_back(mir2::MapConfig{"1", "TargetStatusMap", {}, 24, 24, 5, 5});
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        enter(1511, character("TransferStatus", {18, 31}))));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(spell(1511, 31));
    append(dispatch, runtime.tick());
    append(dispatch, runtime.route_logic_command(spell(1511, 18)));
    append(dispatch, runtime.tick());
    auto snapshot = runtime.snapshot_character_actor("TransferStatus");
    assert(snapshot.has_value());
    assert((snapshot->status & kTransparentStatusBit) != 0);
    assert((snapshot->status & kMagicBubbleStatusBit) != 0);

    append(dispatch, runtime.route_logic_command(walk(1511, 11, 10)));
    append(dispatch, runtime.tick());
    snapshot = runtime.snapshot_character_actor("TransferStatus");
    assert(snapshot.has_value());
    assert(snapshot->map_id == "1");
    assert((snapshot->status & kTransparentStatusBit) == 0);
    assert((snapshot->status & kMagicBubbleStatusBit) != 0);
    assert(has_packet(dispatch, mir2::kSmCharStatusChanged));
    assert(has_packet(dispatch, mir2::kSmChangeMap));
  }

  {
    auto config = base_config();
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        enter(1521, character("LogoutStatus", {18, 31}))));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(spell(1521, 31));
    append(dispatch, runtime.tick());
    append(dispatch, runtime.route_logic_command(spell(1521, 18)));
    append(dispatch, runtime.tick());
    auto snapshot = runtime.snapshot_character_actor("LogoutStatus");
    assert(snapshot.has_value());
    assert((snapshot->status & kTransparentStatusBit) != 0);
    assert((snapshot->status & kMagicBubbleStatusBit) != 0);

    const auto logout_dispatch = runtime.route_logic_command(logout(1521));
    const auto saved = saved_character(logout_dispatch, "LogoutStatus");
    assert(saved.has_value());
    assert((saved->status & kTransparentStatusBit) == 0);
    assert((saved->status & kMagicBubbleStatusBit) == 0);
    assert(has_packet(logout_dispatch, mir2::kSmCharStatusChanged));
  }

  return 0;
}
