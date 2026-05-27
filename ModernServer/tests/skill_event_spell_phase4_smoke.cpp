#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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

mir2::MagicConfig make_magic(std::int32_t id, std::string name) {
  mir2::MagicConfig magic;
  magic.id = id;
  magic.name = std::move(name);
  magic.affect_players = true;
  magic.affect_monsters = true;
  magic.legacy.legacy_present = true;
  magic.legacy.effect_type = id;
  magic.legacy.effect = id;
  magic.legacy.spell = 4;
  magic.legacy.min_power = 10;
  magic.legacy.max_power = 10;
  magic.legacy.job = 99;
  magic.legacy.need_level = {1, 1, 1, 1};
  magic.legacy.max_train = {500, 1500, 3000, 3000};
  magic.legacy.max_train_level = 3;
  magic.legacy.delay_time = 0;
  magic.legacy.def_spell = 1;
  magic.legacy.def_min_power = 1;
  magic.legacy.def_max_power = 2;
  return magic;
}

mir2::ItemConfig make_bujuk_item() {
  mir2::ItemConfig item;
  item.id = 501;
  item.name = "Bujuk";
  item.std_mode = 25;
  item.shape = 5;
  item.dura_max = 1000;
  item.equip_slot = mir2::kEquipBujuk;
  return item;
}

mir2::MapConfig make_map(std::string id, std::string title) {
  mir2::MapConfig map;
  map.id = std::move(id);
  map.title = std::move(title);
  map.width = 30;
  map.height = 30;
  map.home_x = 10;
  map.home_y = 10;
  map.allow_pk = true;
  map.fight_zone = true;
  return map;
}

mir2::HostConfig base_config(std::uint32_t seed = 1, bool two_maps = false) {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = seed;
  config.budgets.tick_ms = 20;
  config.maps.push_back(make_map("0", "HomeMap"));
  if (two_maps) {
    config.maps.push_back(make_map("1", "FieldMap"));
  }
  config.items.push_back(make_bujuk_item());
  config.magics.push_back(make_magic(16, "HolyCurtain"));
  config.magics.push_back(make_magic(21, "SpaceMove"));
  config.magics.push_back(make_magic(22, "FireWall"));
  return config;
}

mir2::SpawnConfig make_spawn(std::string name, std::int32_t x, std::int32_t y,
                             std::int32_t level = 1, std::int32_t hp = 100) {
  mir2::SpawnConfig spawn;
  spawn.map_id = "0";
  spawn.actor_type = "monster";
  spawn.name = std::move(name);
  spawn.x = x;
  spawn.y = y;
  spawn.respawn_ms = 30000;
  spawn.level = level;
  spawn.max_hp = hp;
  spawn.attack_power = 0;
  spawn.defense = 0;
  spawn.magic_defense = 0;
  spawn.exp_reward = 10;
  return spawn;
}

mir2::CharacterRecord make_character(std::string name, std::string map_id,
                                     std::int32_t x, std::int32_t y,
                                     std::vector<std::int32_t> magic_ids,
                                     std::uint8_t magic_level = 1,
                                     std::uint8_t character_level = 40,
                                     bool equip_bujuk = true) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = std::move(map_id);
  character.x = x;
  character.y = y;
  character.ability.level = character_level;
  character.ability.hp = 80;
  character.ability.max_hp = 80;
  character.ability.mp = 200;
  character.ability.max_mp = 200;
  character.ability.mc = mir2::make_word(2, 4);
  character.ability.sc = mir2::make_word(2, 4);
  character.ability.max_exp = 1000;
  character.ability.max_weight = 30;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  character.attack_mode = 0;
  if (equip_bujuk) {
    character.equipped_items[mir2::kEquipBujuk].index = 501;
    character.equipped_items[mir2::kEquipBujuk].make_index = 9001;
    character.equipped_items[mir2::kEquipBujuk].dura = 1000;
    character.equipped_items[mir2::kEquipBujuk].dura_max = 1000;
  }
  for (std::size_t index = 0; index < magic_ids.size() && index < character.magics.size();
       ++index) {
    character.magics[index].magic_id = static_cast<std::uint16_t>(magic_ids[index]);
    character.magics[index].level = magic_level;
    character.magics[index].key = static_cast<char>('1' + index);
  }
  return character;
}

mir2::LogicCommand make_enter(std::uint64_t session_id, mir2::CharacterRecord character) {
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

mir2::LogicCommand make_spell(std::uint64_t session_id, std::int32_t magic_id,
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

mir2::LogicCommand make_walk(std::uint64_t session_id, std::int32_t x, std::int32_t y) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::walk;
  command.session_id = session_id;
  command.x = x;
  command.y = y;
  command.dir = 2;
  command.game_message.ident = mir2::kCmWalk;
  return command;
}

void append(mir2::RuntimeDispatch& target, mir2::RuntimeDispatch source) {
  target.session_events.insert(target.session_events.end(),
                               std::make_move_iterator(source.session_events.begin()),
                               std::make_move_iterator(source.session_events.end()));
  target.audit_events.insert(target.audit_events.end(),
                             std::make_move_iterator(source.audit_events.begin()),
                             std::make_move_iterator(source.audit_events.end()));
  target.persist_requests.insert(target.persist_requests.end(),
                                 std::make_move_iterator(source.persist_requests.begin()),
                                 std::make_move_iterator(source.persist_requests.end()));
  target.cross_map_mails.insert(target.cross_map_mails.end(),
                                std::make_move_iterator(source.cross_map_mails.begin()),
                                std::make_move_iterator(source.cross_map_mails.end()));
  target.legacy_traces.insert(target.legacy_traces.end(),
                              std::make_move_iterator(source.legacy_traces.begin()),
                              std::make_move_iterator(source.legacy_traces.end()));
}

std::vector<mir2::DecodedLegacyGamePacket> decoded_packets(
    const mir2::RuntimeDispatch& dispatch) {
  std::vector<mir2::DecodedLegacyGamePacket> packets;
  for (const auto& event : dispatch.session_events) {
    if (const auto decoded = mir2::decode_legacy_game_packet(event.packet);
        decoded.has_value()) {
      packets.push_back(*decoded);
    }
  }
  return packets;
}

std::optional<std::size_t> first_packet_index(const mir2::RuntimeDispatch& dispatch,
                                              std::uint16_t ident) {
  const auto packets = decoded_packets(dispatch);
  for (std::size_t index = 0; index < packets.size(); ++index) {
    if (packets[index].message.ident == ident) {
      return index;
    }
  }
  return std::nullopt;
}

std::optional<std::size_t> last_packet_index(const mir2::RuntimeDispatch& dispatch,
                                             std::uint16_t ident) {
  const auto packets = decoded_packets(dispatch);
  std::optional<std::size_t> result;
  for (std::size_t index = 0; index < packets.size(); ++index) {
    if (packets[index].message.ident == ident) {
      result = index;
    }
  }
  return result;
}

std::optional<std::size_t> first_death_packet_index(const mir2::RuntimeDispatch& dispatch) {
  const auto death = first_packet_index(dispatch, mir2::kSmDeath);
  const auto now_death = first_packet_index(dispatch, mir2::kSmNowDeath);
  if (!death.has_value()) {
    return now_death;
  }
  if (!now_death.has_value()) {
    return death;
  }
  return std::min(*death, *now_death);
}

bool has_packet(const mir2::RuntimeDispatch& dispatch, std::uint16_t ident) {
  return first_packet_index(dispatch, ident).has_value();
}

bool has_trace(const mir2::RuntimeDispatch& dispatch, const std::string& action) {
  return std::any_of(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                     [&](const mir2::LegacyRuntimeTrace& trace) {
                       return trace.action == action;
                     });
}

bool has_trace_success(const mir2::RuntimeDispatch& dispatch, const std::string& action,
                       bool success) {
  return std::any_of(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                     [&](const mir2::LegacyRuntimeTrace& trace) {
                       return trace.action == action && trace.success == success;
                     });
}

bool has_event_trace(const mir2::RuntimeDispatch& dispatch, const std::string& action,
                     mir2::LegacyEventType type) {
  std::string type_name;
  switch (type) {
    case mir2::LegacyEventType::stone_mine:
      type_name = "stone_mine";
      break;
    case mir2::LegacyEventType::digout_zombi:
      type_name = "digout_zombi";
      break;
    case mir2::LegacyEventType::pile_stones:
      type_name = "pile_stones";
      break;
    case mir2::LegacyEventType::holy_curtain:
      type_name = "holy_curtain";
      break;
    case mir2::LegacyEventType::fire_burn:
      type_name = "fire_burn";
      break;
    case mir2::LegacyEventType::sculp_piece:
      type_name = "sculp_piece";
      break;
  }
  return std::any_of(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                     [&](const mir2::LegacyRuntimeTrace& trace) {
                       return trace.stage == "LegacyEventManager" &&
                              trace.action == action && trace.object_name == type_name;
                     });
}

mir2::CharacterRecord snapshot(mir2::LogicRuntime& runtime, std::string_view name) {
  const auto record = runtime.snapshot_character_actor(name);
  assert(record.has_value());
  return *record;
}

mir2::ItemConfig death_item(std::int32_t id, std::string name, std::int32_t std_mode) {
  mir2::ItemConfig item;
  item.id = id;
  item.name = std::move(name);
  item.std_mode = std_mode;
  item.dura_max = 1000;
  item.weight = 1;
  return item;
}

mir2::LegacyUserItem death_user_item(std::int32_t index, std::int32_t make_index,
                                     std::uint16_t dura) {
  mir2::LegacyUserItem item;
  item.index = static_cast<std::uint16_t>(index);
  item.make_index = make_index;
  item.dura = dura;
  item.dura_max = dura;
  return item;
}

void assert_before_death(const mir2::RuntimeDispatch& dispatch, std::uint16_t ident) {
  const auto before = last_packet_index(dispatch, ident);
  const auto death = first_death_packet_index(dispatch);
  assert(before.has_value());
  assert(death.has_value());
  assert(*before < *death);
}

}  // namespace

int main() {
  {
    auto config = base_config();
    config.spawns.push_back(make_spawn("FireTarget", 11, 10, 1, 100));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(1601, make_character("FireMage", "0", 10, 10, {22}))));
    static_cast<void>(runtime.tick());

    auto dispatch = runtime.route_logic_command(make_spell(1601, 22, 10, 10));
    append(dispatch, runtime.tick());
    assert(has_packet(dispatch, mir2::kSmMagicFire));
    assert(has_trace(dispatch, "fire_wall"));
    assert(has_trace(dispatch, "train_skill"));
    assert(runtime.legacy_active_event_count() == 5);

    const std::array<std::pair<std::int32_t, std::int32_t>, 5> expected{
        std::pair{10, 9}, {9, 10}, {10, 10}, {11, 10}, {10, 11}};
    for (const auto& [x, y] : expected) {
      assert(runtime.find_legacy_event("0", x, y,
                                       mir2::LegacyEventType::fire_burn).has_value());
    }

    const auto first_tick = runtime.run_legacy_event_manager(1000);
    assert(has_event_trace(first_tick, "fire_tick", mir2::LegacyEventType::fire_burn));
    assert(has_trace(first_tick, "fire_burn_struck"));
    auto target = runtime.legacy_monster_snapshot("0", 1);
    assert(target.has_value() && target->hp < 100);

    const auto too_soon = runtime.run_legacy_event_manager(3000);
    assert(!has_event_trace(too_soon, "fire_tick", mir2::LegacyEventType::fire_burn));
    const auto second_tick = runtime.run_legacy_event_manager(4001);
    assert(has_event_trace(second_tick, "fire_tick", mir2::LegacyEventType::fire_burn));
    static_cast<void>(runtime.run_legacy_event_manager(1000000));
    assert(runtime.legacy_active_event_count() == 0);
  }

  {
    auto config = base_config();
    config.spawns.push_back(make_spawn("Seized", 12, 12, 1, 100));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(1611, make_character("Curtain", "0", 10, 10, {16}))));
    static_cast<void>(runtime.tick());

    auto dispatch = runtime.route_logic_command(make_spell(1611, 16, 12, 12));
    append(dispatch, runtime.tick());
    assert(has_packet(dispatch, mir2::kSmDuraChange));
    assert(has_packet(dispatch, mir2::kSmMagicFire));
    assert(has_trace(dispatch, "bujuk_used"));
    assert(has_trace(dispatch, "holy_curtain"));
    assert(has_trace(dispatch, "train_skill"));
    assert(runtime.legacy_active_event_count() == 8);

    const std::array<std::pair<std::int32_t, std::int32_t>, 8> offsets{
        std::pair{-1, -2}, {1, -2}, {-2, -1}, {2, -1},
        {-2, 1},          {2, 1},   {-1, 2},  {1, 2}};
    for (const auto& [dx, dy] : offsets) {
      assert(runtime.find_legacy_event("0", 12 + dx, 12 + dy,
                                       mir2::LegacyEventType::holy_curtain).has_value());
    }

    static_cast<void>(runtime.route_logic_command(make_walk(1611, 11, 10)));
    static_cast<void>(runtime.tick());
    const auto blocked = snapshot(runtime, "Curtain");
    assert(blocked.x == 10 && blocked.y == 10);

    const auto closed = runtime.run_legacy_event_manager(300000);
    assert(has_event_trace(closed, "close", mir2::LegacyEventType::holy_curtain));
    assert(runtime.legacy_active_event_count() == 0);
  }

  {
    auto config = base_config();
    config.spawns.push_back(make_spawn("TooStrong", 12, 12, 50, 100));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(1621, make_character("WeakCurtain", "0", 10, 10, {16}, 1, 1))));
    static_cast<void>(runtime.tick());

    auto dispatch = runtime.route_logic_command(make_spell(1621, 16, 12, 12));
    append(dispatch, runtime.tick());
    assert(has_packet(dispatch, mir2::kSmDuraChange));
    assert(has_trace_success(dispatch, "holy_curtain_gate", false));
    assert(has_trace_success(dispatch, "holy_curtain", false));
    assert(!has_trace(dispatch, "train_skill"));
    assert(runtime.legacy_active_event_count() == 0);
  }

  {
    auto config = base_config();
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(1631, make_character("Blinker", "0", 10, 10, {21}))));
    static_cast<void>(runtime.tick());

    auto dispatch = runtime.route_logic_command(make_spell(1631, 21));
    append(dispatch, runtime.tick());
    assert(has_trace_success(dispatch, "space_move_gate", true));
    assert(has_trace(dispatch, "train_skill"));
    const auto magic_fire = first_packet_index(dispatch, mir2::kSmMagicFire);
    const auto hide2 = first_packet_index(dispatch, mir2::kSmSpaceMoveHide2);
    const auto show2 = first_packet_index(dispatch, mir2::kSmSpaceMoveShow2);
    assert(magic_fire.has_value());
    assert(hide2.has_value());
    assert(show2.has_value());
    assert(*magic_fire < *hide2);
    assert(*hide2 < *show2);
  }

  {
    auto config = base_config(12);
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(1641, make_character("FailedBlink", "0", 10, 10, {21}, 0))));
    static_cast<void>(runtime.tick());

    auto dispatch = runtime.route_logic_command(make_spell(1641, 21));
    append(dispatch, runtime.tick());
    assert(has_packet(dispatch, mir2::kSmMagicFire));
    assert(has_trace_success(dispatch, "space_move_gate", false));
    assert(!has_packet(dispatch, mir2::kSmSpaceMoveHide2));
    assert(!has_packet(dispatch, mir2::kSmSpaceMoveShow2));
    assert(!has_trace(dispatch, "train_skill"));
    const auto failed = snapshot(runtime, "FailedBlink");
    assert(failed.x == 10 && failed.y == 10 && failed.map_id == "0");
  }

  {
    auto config = base_config(1, true);
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(1651, make_character("Traveler", "1", 10, 10, {21}))));
    static_cast<void>(runtime.tick());

    auto dispatch = runtime.route_logic_command(make_spell(1651, 21));
    append(dispatch, runtime.tick());
    const auto located = runtime.locate_character_actor("Traveler");
    assert(located.has_value());
    assert(located->first == "0");
    assert(has_packet(dispatch, mir2::kSmSpaceMoveHide2));
    assert(has_packet(dispatch, mir2::kSmSpaceMoveShow2));
    assert(!has_packet(dispatch, mir2::kSmTurn));
  }

  {
    auto config = base_config(7);
    config.maps[0].fight_zone = false;
    config.items.push_back(death_item(901, "Burn Meat", 40));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(1661, make_character("FireMage", "0", 10, 10, {22}))));
    static_cast<void>(runtime.tick());
    auto victim = make_character("BurnVictim", "0", 11, 10, {}, 0, 40, false);
    victim.ability.hp = 5;
    victim.ability.max_hp = 5;
    victim.pk_point = 250;
    victim.bag_items[0] = death_user_item(901, 9901, 3500);
    static_cast<void>(runtime.route_logic_command(make_enter(1662, victim)));
    static_cast<void>(runtime.tick());

    auto dispatch = runtime.route_logic_command(make_spell(1661, 22, 10, 10));
    append(dispatch, runtime.tick());
    assert(has_packet(dispatch, mir2::kSmMagicFire));
    const auto burn = runtime.run_legacy_event_manager(5000);
    assert(has_trace(burn, "fire_burn_death"));
    assert_before_death(burn, mir2::kSmItemShow);
    assert_before_death(burn, mir2::kSmDelItem);
    assert_before_death(burn, mir2::kSmSendUseItems);
    assert_before_death(burn, mir2::kSmWeightChanged);
    assert_before_death(burn, mir2::kSmAbility);
    assert_before_death(burn, mir2::kSmSubAbility);
    const auto dead = snapshot(runtime, "BurnVictim");
    assert(mir2::is_empty(dead.bag_items[0]));
    assert(dead.gold == 0);
  }

  return 0;
}
