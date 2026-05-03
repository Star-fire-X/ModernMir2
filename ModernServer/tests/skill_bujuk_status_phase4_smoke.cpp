#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

#undef assert
#define assert(expression)                                                        \
  do {                                                                            \
    if (!(expression)) {                                                          \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);      \
      std::abort();                                                               \
    }                                                                             \
  } while (false)

namespace {

constexpr std::int32_t kTransparentStatusBit = 0x00800000;
constexpr std::int32_t kDefenceStatusBit = 0x00400000;
constexpr std::int32_t kMagDefenceStatusBit = 0x00200000;

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
  magic.legacy.min_power = 8;
  magic.legacy.max_power = 8;
  magic.legacy.job = 99;
  magic.legacy.need_level = {1, 1, 1, 1};
  magic.legacy.max_train = {500, 1500, 3000, 3000};
  magic.legacy.max_train_level = 3;
  magic.legacy.delay_time = 0;
  magic.legacy.def_spell = 1;
  magic.legacy.def_min_power = 2;
  magic.legacy.def_max_power = 4;
  return magic;
}

mir2::HostConfig base_config() {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 1;
  config.budgets.tick_ms = 20;
  config.maps.push_back(mir2::MapConfig{"0", "Phase4Map", {}, 24, 24, 10, 10});
  config.items.push_back(
      mir2::ItemConfig{501, "Bujuk", 1, 10, 25, 5, 1, 1000, 9, 0, 0});
  config.items.push_back(
      mir2::ItemConfig{502, "Green Poison", 1, 10, 25, 1, 1, 1000, 9, 0, 0});
  config.items.push_back(
      mir2::ItemConfig{503, "Yellow Poison", 1, 10, 25, 2, 1, 1000, 9, 0, 0});
  for (const auto id : {6, 13, 14, 15, 18, 19}) {
    config.magics.push_back(make_magic(id, "Phase4Magic" + std::to_string(id)));
  }
  return config;
}

mir2::SpawnConfig make_spawn(std::string name, std::int32_t x, std::int32_t y,
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

mir2::CharacterRecord make_character(std::string name, std::int32_t x, std::int32_t y,
                                     std::vector<std::int32_t> magic_ids,
                                     std::int32_t item_index = 501,
                                     std::int32_t item_dura = 500) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = x;
  character.y = y;
  character.ability.level = 30;
  character.ability.hp = 40;
  character.ability.max_hp = 40;
  character.ability.mp = 100;
  character.ability.max_mp = 100;
  character.ability.sc = mir2::make_word(2, 4);
  character.ability.mc = mir2::make_word(2, 4);
  character.ability.max_exp = 100;
  character.ability.max_weight = 30;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  if (item_index > 0) {
    character.equipped_items[mir2::kEquipBujuk].index =
        static_cast<std::uint16_t>(item_index);
    character.equipped_items[mir2::kEquipBujuk].make_index = 7000 + item_index;
    character.equipped_items[mir2::kEquipBujuk].dura =
        static_cast<std::uint16_t>(item_dura);
    character.equipped_items[mir2::kEquipBujuk].dura_max = 1000;
  }
  for (std::size_t index = 0; index < magic_ids.size() && index < character.magics.size();
       ++index) {
    character.magics[index].magic_id = static_cast<std::uint16_t>(magic_ids[index]);
    character.magics[index].level = 1;
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

void append_dispatch(mir2::RuntimeDispatch& target, mir2::RuntimeDispatch source) {
  target.session_events.insert(target.session_events.end(),
                               std::make_move_iterator(source.session_events.begin()),
                               std::make_move_iterator(source.session_events.end()));
  target.legacy_traces.insert(target.legacy_traces.end(),
                              std::make_move_iterator(source.legacy_traces.begin()),
                              std::make_move_iterator(source.legacy_traces.end()));
}

mir2::RuntimeDispatch advance_ticks(mir2::LogicRuntime& runtime, std::int32_t ticks) {
  mir2::RuntimeDispatch combined;
  for (std::int32_t tick = 0; tick < ticks; ++tick) {
    append_dispatch(combined, runtime.tick());
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

std::int32_t count_trace(const mir2::RuntimeDispatch& dispatch, const std::string& action) {
  return static_cast<std::int32_t>(std::count_if(
      dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
      [&](const mir2::LegacyRuntimeTrace& trace) { return trace.action == action; }));
}

std::optional<mir2::DecodedLegacyGamePacket> find_packet(
    const mir2::RuntimeDispatch& dispatch, std::uint16_t ident) {
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return decoded;
    }
  }
  return std::nullopt;
}

bool has_packet(const mir2::RuntimeDispatch& dispatch, std::uint16_t ident) {
  return find_packet(dispatch, ident).has_value();
}

}  // namespace

int main() {
  {
    auto config = base_config();
    config.spawns.push_back(make_spawn("CharmTarget", 10, 8));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(1101, make_character("Charm", 10, 10, {13}))));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_spell(1101, 13, 10, 8, 1)));
    const auto immediate = runtime.tick();

    assert(has_packet(immediate, mir2::kSmDuraChange));
    assert(has_packet(immediate, mir2::kSmMagicFire));
    assert(has_trace(immediate, "bujuk_used"));
    assert(has_trace(immediate, "delay_magic_queued"));
    assert(has_trace(immediate, "train_skill"));
    assert(!has_packet(immediate, mir2::kSmStruck));
    const auto delayed = advance_ticks(runtime, 60);
    assert(has_trace(delayed, "mag_struck"));
    assert(has_packet(delayed, mir2::kSmStruck));
  }

  {
    auto config = base_config();
    config.spawns.push_back(make_spawn("NoBujukTarget", 10, 8));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(1111, make_character("NoBujuk", 10, 10, {13}, 0, 0))));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_spell(1111, 13, 10, 8, 1)));
    const auto dispatch = runtime.tick();
    const auto snapshot = runtime.snapshot_character_actor("NoBujuk");

    assert(snapshot.has_value());
    assert(snapshot->ability.mp < 100);
    assert(has_packet(dispatch, mir2::kSmMagicFireFail));
    assert(!has_packet(dispatch, mir2::kSmMagicFire));
    assert(!has_trace(dispatch, "train_skill"));
  }

  {
    auto config = base_config();
    config.spawns.push_back(make_spawn("PoisonTarget", 10, 8, 100));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(1121, make_character("Poisoner", 10, 10, {6}, 502, 500))));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_spell(1121, 6, 10, 8, 1)));
    const auto immediate = runtime.tick();

    assert(has_trace(immediate, "poison_powder_used", 1));
    assert(has_trace(immediate, "poison_queued", 0));
    assert(has_packet(immediate, mir2::kSmDuraChange));
    const auto applied = advance_ticks(runtime, 50);
    assert(has_trace(applied, "poison_apply", 0));
    const auto ticked = advance_ticks(runtime, 125);
    assert(has_packet(ticked, mir2::kSmStruck));
  }

  {
    auto config = base_config();
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(1131, make_character("Buffer", 10, 10, {14, 15}))));
    static_cast<void>(runtime.route_logic_command(
        make_enter(1132, make_character("Friend", 11, 10, {}))));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_spell(1131, 14, 10, 10)));
    const auto mag_def = runtime.tick();
    assert(has_trace(mag_def, "defence_area"));
    assert(has_trace(mag_def, "train_skill"));
    auto buffer = runtime.snapshot_character_actor("Buffer");
    auto friend_record = runtime.snapshot_character_actor("Friend");
    assert(buffer.has_value() && friend_record.has_value());
    assert((buffer->status & kMagDefenceStatusBit) != 0);
    assert((friend_record->status & kMagDefenceStatusBit) != 0);

    static_cast<void>(advance_ticks(runtime, 41));
    static_cast<void>(runtime.route_logic_command(make_spell(1131, 15, 10, 10)));
    const auto defence = runtime.tick();
    assert(has_trace(defence, "defence_area"));
    buffer = runtime.snapshot_character_actor("Buffer");
    assert(buffer.has_value());
    assert((buffer->status & kDefenceStatusBit) != 0);
  }

  {
    auto config = base_config();
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(1141, make_character("Hider", 10, 10, {18}))));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_spell(1141, 18)));
    const auto hide = runtime.tick();
    auto hider = runtime.snapshot_character_actor("Hider");
    assert(has_trace(hide, "transparent_apply"));
    assert(has_trace(hide, "train_skill"));
    assert(hider.has_value() && (hider->status & kTransparentStatusBit) != 0);

    static_cast<void>(runtime.route_logic_command(make_walk(1141, 11, 10)));
    static_cast<void>(runtime.tick());
    hider = runtime.snapshot_character_actor("Hider");
    assert(hider.has_value() && (hider->status & kTransparentStatusBit) == 0);
  }

  {
    auto config = base_config();
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(1151, make_character("GroupHide", 10, 10, {19}))));
    static_cast<void>(runtime.route_logic_command(
        make_enter(1152, make_character("GroupFriend", 11, 10, {}))));
    static_cast<void>(runtime.route_logic_command(
        make_enter(1153, make_character("FarFriend", 18, 18, {}))));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_spell(1151, 19, 10, 10)));
    const auto queued = runtime.tick();

    assert(count_trace(queued, "transparent_queued") == 2);
    assert(has_trace(queued, "train_skill"));
    const auto applied = advance_ticks(runtime, 40);
    assert(count_trace(applied, "transparent_apply") == 2);
    const auto group_hide = runtime.snapshot_character_actor("GroupHide");
    const auto group_friend = runtime.snapshot_character_actor("GroupFriend");
    const auto far_friend = runtime.snapshot_character_actor("FarFriend");
    assert(group_hide.has_value() && group_friend.has_value() && far_friend.has_value());
    assert((group_hide->status & kTransparentStatusBit) != 0);
    assert((group_friend->status & kTransparentStatusBit) != 0);
    assert((far_friend->status & kTransparentStatusBit) == 0);
  }

  return 0;
}
