#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
#define assert(expression)                                                        \
  do {                                                                            \
    if (!(expression)) {                                                          \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);      \
      std::abort();                                                               \
    }                                                                             \
  } while (false)

namespace {

void write_u16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value & 0xffU);
  bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
}

std::filesystem::path write_test_map(
    int width, int height, const std::vector<std::pair<int, int>>& blocked) {
  const auto path = std::filesystem::temp_directory_path() / "mir2_skill_line_block.map";
  std::vector<std::uint8_t> bytes(52U + static_cast<std::size_t>(width) *
                                            static_cast<std::size_t>(height) * 12U);
  write_u16(bytes, 0, static_cast<std::uint16_t>(width));
  write_u16(bytes, 2, static_cast<std::uint16_t>(height));
  for (const auto& [x, y] : blocked) {
    const auto offset = 52U +
        (static_cast<std::size_t>(x) * static_cast<std::size_t>(height) +
         static_cast<std::size_t>(y)) *
            12U;
    write_u16(bytes, offset + 4U, 0x8000U);
  }
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return path;
}

mir2::MagicConfig make_phase3_magic(std::int32_t id, std::string name) {
  mir2::MagicConfig magic;
  magic.id = id;
  magic.name = std::move(name);
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
  mir2::MapConfig map{"0", "Phase3Map", {}, 24, 24, 10, 10};
  map.allow_pk = true;
  map.fight_zone = true;
  config.maps.push_back(map);
  config.magics.push_back(make_phase3_magic(2, "Healing"));
  config.magics.push_back(make_phase3_magic(8, "Repulsion"));
  config.magics.push_back(make_phase3_magic(9, "HellFire"));
  config.magics.push_back(make_phase3_magic(10, "Laser"));
  config.magics.push_back(make_phase3_magic(11, "Thunderbolt"));
  config.magics.push_back(make_phase3_magic(23, "Explosion"));
  config.magics.push_back(make_phase3_magic(24, "ThunderStorm"));
  config.magics.push_back(make_phase3_magic(28, "OpenHealth"));
  config.magics.push_back(make_phase3_magic(29, "GroupHealing"));
  config.magics.push_back(make_phase3_magic(31, "MagicShield"));
  config.magics.push_back(make_phase3_magic(32, "TurnUndead"));
  config.magics.push_back(make_phase3_magic(33, "IceStorm"));
  config.magics.push_back(make_phase3_magic(35, "ColdPalm"));
  return config;
}

mir2::SpawnConfig make_spawn(std::string name, std::int32_t x, std::int32_t y,
                             std::int32_t level = 1, std::int32_t hp = 100,
                             std::int32_t life_attrib = 0,
                             std::int32_t magic_defense = 0) {
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
  spawn.magic_defense = magic_defense;
  spawn.exp_reward = 10;
  spawn.life_attrib = life_attrib;
  return spawn;
}

mir2::CharacterRecord make_character(std::string name, std::int32_t x = 10,
                                     std::int32_t y = 10, std::uint8_t level = 20,
                                     std::int32_t hp = 30, std::int32_t max_hp = 30,
                                     std::int32_t mp = 100,
                                     std::vector<std::int32_t> magic_ids = {}) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = x;
  character.y = y;
  character.ability.level = level;
  character.ability.hp = static_cast<std::uint16_t>(hp);
  character.ability.max_hp = static_cast<std::uint16_t>(max_hp);
  character.ability.mp = static_cast<std::uint16_t>(mp);
  character.ability.max_mp = static_cast<std::uint16_t>(std::max(mp, 100));
  character.ability.max_exp = 100;
  character.ability.max_weight = 30;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  character.attack_mode = 0;
  for (std::size_t index = 0; index < magic_ids.size() && index < character.magics.size(); ++index) {
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

void append_dispatch(mir2::RuntimeDispatch& target, mir2::RuntimeDispatch source) {
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

mir2::RuntimeDispatch advance_ticks(mir2::LogicRuntime& runtime, std::int32_t ticks) {
  mir2::RuntimeDispatch combined;
  for (std::int32_t tick = 0; tick < ticks; ++tick) {
    append_dispatch(combined, runtime.tick());
  }
  return combined;
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

std::int32_t count_trace(const mir2::RuntimeDispatch& dispatch, const std::string& action,
                         std::int32_t magic_id = 0) {
  return static_cast<std::int32_t>(std::count_if(
      dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
      [&](const mir2::LegacyRuntimeTrace& trace) {
        return trace.action == action && (magic_id == 0 || trace.value == magic_id);
      }));
}

std::vector<std::int32_t> trace_damages(const mir2::RuntimeDispatch& dispatch,
                                        const std::string& action, std::int32_t magic_id) {
  std::vector<std::int32_t> damages;
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.action == action && trace.value == magic_id) {
      damages.push_back(trace.damage);
    }
  }
  return damages;
}

std::vector<mir2::DecodedLegacyGamePacket> decoded_packets(const mir2::RuntimeDispatch& dispatch) {
  std::vector<mir2::DecodedLegacyGamePacket> packets;
  for (const auto& event : dispatch.session_events) {
    if (const auto decoded = mir2::decode_legacy_game_packet(event.packet); decoded.has_value()) {
      packets.push_back(*decoded);
    }
  }
  return packets;
}

bool has_packet(const mir2::RuntimeDispatch& dispatch, std::uint16_t ident) {
  const auto packets = decoded_packets(dispatch);
  return std::any_of(packets.begin(), packets.end(),
                     [&](const auto& packet) { return packet.message.ident == ident; });
}

std::uint64_t actor_id(mir2::LogicRuntime& runtime, std::string_view name) {
  auto located = runtime.locate_character_actor(name);
  assert(located.has_value());
  return located->second;
}

}  // namespace

int main() {
  {
    auto config = base_config();
    config.spawns.push_back(make_spawn("PushTarget", 11, 10, 1, 100));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(901, make_character("Pusher", 10, 10, 20, 30, 30, 100, {8}))));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_spell(901, 8)));
    const auto dispatch = runtime.tick();

    assert(has_trace(dispatch, "push"));
    assert(has_trace(dispatch, "train_skill"));
    assert(has_packet(dispatch, mir2::kSmMagicFire));
    assert(has_packet(dispatch, mir2::kSmBackStep));
  }

  {
    auto config = base_config();
    config.spawns.push_back(make_spawn("Line9Target", 10, 7, 1, 100));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(911, make_character("Line9", 10, 10, 20, 30, 30, 100, {9}))));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_spell(911, 9, 10, 5)));
    const auto immediate = runtime.tick();

    assert(has_trace(immediate, "mag_struck_queued"));
    assert(has_trace(immediate, "train_skill"));
    assert(!has_packet(immediate, mir2::kSmStruck));
    const auto delayed = advance_ticks(runtime, 30);
    assert(has_trace(delayed, "mag_struck"));
    assert(has_packet(delayed, mir2::kSmStruck));
  }

  {
    auto config = base_config();
    config.maps[0].source_map = write_test_map(24, 24, {{10, 8}});
    config.spawns.push_back(make_spawn("BlockedLineTarget", 10, 7, 1, 100));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(916, make_character("LineBlock", 10, 10, 20, 30, 30, 100, {9}))));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_spell(916, 9, 10, 5)));
    const auto dispatch = runtime.tick();

    assert(has_trace(dispatch, "line_blocked"));
    assert(!has_trace(dispatch, "mag_struck_queued"));
    assert(!has_trace(dispatch, "train_skill"));
  }

  {
    auto config = base_config();
    config.spawns.push_back(make_spawn("Line10Normal", 10, 3, 1, 100, 0));
    config.spawns.push_back(make_spawn("Line10Undead", 10, 2, 1, 100, 1));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(921, make_character("Line10", 10, 10, 20, 30, 30, 100, {10}))));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_spell(921, 10, 10, 2)));
    const auto immediate = runtime.tick();

    assert(has_trace(immediate, "mag_struck_queued"));
    assert(has_trace(immediate, "train_skill"));
    const auto delayed = advance_ticks(runtime, 30);
    assert(has_trace(delayed, "mag_struck"));
    const auto damages = trace_damages(delayed, "mag_struck", 10);
    assert(damages.size() == 2);
    const auto [min_damage, max_damage] = std::minmax_element(damages.begin(), damages.end());
    assert(*min_damage < *max_damage);
  }

  {
    auto config = base_config();
    config.spawns.push_back(make_spawn("ThunderTarget", 10, 2, 1, 100, 1));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(931, make_character("Thunder", 10, 10, 20, 30, 30, 100, {11}))));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_spell(931, 11, 10, 2, 1)));
    const auto immediate = runtime.tick();

    assert(has_trace(immediate, "mag_struck_queued"));
    assert(has_trace(immediate, "train_skill"));
    assert(!has_packet(immediate, mir2::kSmStruck));
    const auto delayed = advance_ticks(runtime, 30);
    assert(has_trace(delayed, "mag_struck"));
    assert(has_packet(delayed, mir2::kSmStruck));
  }

  {
    auto config = base_config();
    config.spawns.push_back(make_spawn("AntiMagicTarget", 10, 2, 1, 100, 0, 10));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(936, make_character("AntiMagicCaster", 10, 10, 20, 30, 30, 100, {11}))));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_spell(936, 11, 10, 2, 1)));
    const auto immediate = runtime.tick();

    assert(has_trace(immediate, "anti_magic"));
    assert(!has_trace(immediate, "mag_struck_queued"));
    assert(!has_trace(immediate, "train_skill"));
  }

  {
    auto cast_cold = [](std::int32_t life_attrib) {
      auto config = base_config();
      config.spawns.push_back(make_spawn("ColdTarget", 10, 2, 1, 100, life_attrib));
      mir2::LogicRuntime runtime(config);
      runtime.initialize();
      static_cast<void>(runtime.route_logic_command(
          make_enter(937, make_character("ColdCaster", 10, 10, 20, 30, 30, 100, {35}))));
      static_cast<void>(runtime.tick());
      static_cast<void>(runtime.route_logic_command(make_spell(937, 35, 10, 2, 1)));
      const auto immediate = runtime.tick();

      assert(has_trace(immediate, "mag_struck_queued"));
      const auto damages = trace_damages(immediate, "mag_struck_queued", 35);
      assert(damages.size() == 1);
      return damages.front();
    };

    const auto normal_damage = cast_cold(0);
    const auto undead_damage = cast_cold(1);
    assert(normal_damage > undead_damage);
  }

  {
    auto config = base_config();
    config.spawns.push_back(make_spawn("AreaA", 10, 8, 1, 100));
    config.spawns.push_back(make_spawn("AreaB", 11, 9, 1, 100));
    config.spawns.push_back(make_spawn("AreaOut", 13, 8, 1, 100));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(941, make_character("Explosion", 10, 10, 20, 30, 30, 100, {23}))));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_spell(941, 23, 10, 8)));
    const auto dispatch = runtime.tick();

    assert(count_trace(dispatch, "mag_struck", 23) == 2);
    assert(has_trace(dispatch, "train_skill"));
  }

  {
    auto config = base_config();
    config.spawns.push_back(make_spawn("StormNormal", 11, 10, 1, 100, 0));
    config.spawns.push_back(make_spawn("StormUndead", 10, 11, 1, 100, 1));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(951, make_character("Storm", 10, 10, 20, 30, 30, 100, {24}))));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_spell(951, 24)));
    const auto dispatch = runtime.tick();
    const auto damages = trace_damages(dispatch, "mag_struck", 24);

    assert(damages.size() == 2);
    const auto [min_damage, max_damage] = std::minmax_element(damages.begin(), damages.end());
    assert(*min_damage <= 3);
    assert(*max_damage >= 10);
    assert(*min_damage < *max_damage);
    assert(has_trace(dispatch, "train_skill"));
  }

  {
    auto config = base_config();
    config.spawns.push_back(make_spawn("OpenHealthTarget", 10, 8, 1, 100));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(961, make_character("Seer", 10, 10, 20, 30, 30, 100, {28}))));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_spell(961, 28, 10, 8, 1)));
    const auto immediate = runtime.tick();

    assert(has_trace(immediate, "open_health_queued"));
    assert(has_trace(immediate, "train_skill"));
    const auto delayed = advance_ticks(runtime, 75);
    assert(has_trace(delayed, "open_health_apply"));
  }

  {
    auto config = base_config();
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(966, make_character("SoloHealer", 10, 10, 20, 5, 30, 100, {2}))));
    static_cast<void>(runtime.tick());
    const auto healer_id = actor_id(runtime, "SoloHealer");
    static_cast<void>(runtime.route_logic_command(make_spell(966, 2, 10, 10, healer_id)));
    const auto immediate = runtime.tick();

    assert(has_trace(immediate, "healing_queued"));
    assert(has_trace(immediate, "train_skill"));
    auto healer = runtime.snapshot_character_actor("SoloHealer");
    assert(healer.has_value() && healer->ability.hp == 5);
    const auto before_delay = advance_ticks(runtime, 39);
    assert(!has_trace(before_delay, "healing_apply"));
    healer = runtime.snapshot_character_actor("SoloHealer");
    assert(healer.has_value() && healer->ability.hp == 5);
    const auto first_heal = advance_ticks(runtime, 2);
    assert(has_trace(first_heal, "healing_apply"));
    assert(has_packet(first_heal, mir2::kSmHealthSpellChanged));
    healer = runtime.snapshot_character_actor("SoloHealer");
    assert(healer.has_value() && healer->ability.hp > 5);
    assert(healer->ability.hp < healer->ability.max_hp);
  }

  {
    auto config = base_config();
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(971, make_character("Healer", 10, 10, 20, 5, 30, 100, {29}))));
    static_cast<void>(runtime.route_logic_command(
        make_enter(972, make_character("Friend", 11, 10, 20, 8, 30, 100, {}))));
    static_cast<void>(runtime.route_logic_command(
        make_enter(973, make_character("FarFriend", 18, 18, 20, 6, 30, 100, {}))));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_spell(971, 29, 10, 10)));
    const auto immediate = runtime.tick();

    assert(count_trace(immediate, "healing_queued", 29) == 2);
    assert(has_trace(immediate, "train_skill"));
    const auto before_delay = advance_ticks(runtime, 39);
    assert(!has_trace(before_delay, "healing_apply"));
    auto healer = runtime.snapshot_character_actor("Healer");
    auto friend_record = runtime.snapshot_character_actor("Friend");
    auto far_friend = runtime.snapshot_character_actor("FarFriend");
    assert(healer.has_value() && healer->ability.hp == 5);
    assert(friend_record.has_value() && friend_record->ability.hp == 8);
    assert(far_friend.has_value() && far_friend->ability.hp == 6);
    const auto delayed = advance_ticks(runtime, 2);
    assert(has_trace(delayed, "healing_apply"));
    assert(has_packet(delayed, mir2::kSmHealthSpellChanged));
    healer = runtime.snapshot_character_actor("Healer");
    friend_record = runtime.snapshot_character_actor("Friend");
    far_friend = runtime.snapshot_character_actor("FarFriend");
    assert(healer.has_value() && healer->ability.hp > 5);
    assert(friend_record.has_value() && friend_record->ability.hp > 8);
    assert(far_friend.has_value() && far_friend->ability.hp == 6);
    assert(healer->ability.hp < healer->ability.max_hp);
    assert(friend_record->ability.hp < friend_record->ability.max_hp);
  }

  {
    auto config = base_config();
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(981, make_character("Shielded", 10, 10, 20, 30, 30, 100, {31}))));
    static_cast<void>(runtime.route_logic_command(
        make_enter(982, make_character("Rival", 10, 8, 20, 30, 30, 100, {11}))));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_spell(981, 31)));
    const auto shield_dispatch = runtime.tick();
    assert(has_trace(shield_dispatch, "magic_bubble"));
    assert(has_trace(shield_dispatch, "train_skill"));
    static_cast<void>(advance_ticks(runtime, 40));
    static_cast<void>(runtime.route_logic_command(make_spell(981, 31)));
    const auto second_shield = runtime.tick();
    assert(has_trace_success(second_shield, "magic_bubble", false));
    assert(!has_trace(second_shield, "train_skill"));

    const auto shielded_id = actor_id(runtime, "Shielded");
    static_cast<void>(runtime.route_logic_command(make_spell(982, 11, 10, 10, shielded_id)));
    static_cast<void>(runtime.tick());
    const auto delayed = advance_ticks(runtime, 30);
    const auto damages = trace_damages(delayed, "mag_struck", 11);
    assert(!damages.empty());
    assert(damages.front() > 0 && damages.front() < 10);
  }

  {
    auto config = base_config();
    config.spawns.push_back(make_spawn("UndeadLow", 10, 8, 1, 20, 1));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(991, make_character("Saint", 10, 10, 80, 30, 30, 100, {32}))));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_spell(991, 32, 10, 8, 1)));
    const auto dispatch = runtime.tick();

    assert(has_trace(dispatch, "turn_undead_gate"));
    assert(has_trace(dispatch, "death"));
    assert(has_trace(dispatch, "train_skill"));
    assert(has_packet(dispatch, mir2::kSmDeath));
  }

  {
    auto config = base_config();
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(1001, make_character("BadLine", 10, 10, 20, 30, 30, 100, {9}))));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_spell(1001, 9, 10, 10)));
    const auto dispatch = runtime.tick();

    assert(has_trace(dispatch, "magic_fire"));
    assert(has_packet(dispatch, mir2::kSmMagicFire));
    assert(!has_trace(dispatch, "train_skill"));
  }

  return 0;
}
