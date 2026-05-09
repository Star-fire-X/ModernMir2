#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "config/config_loader.hpp"
#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

#ifndef MIR2_CONFIG_DIR
#error "MIR2_CONFIG_DIR must be defined by CMake"
#endif

#undef assert
#define assert(expression)       \
  do {                           \
    if (!(expression)) {         \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
      std::abort();              \
    }                            \
  } while (false)

namespace {

const mir2::MagicConfig& find_magic(const mir2::HostConfig& config, std::int32_t id) {
  const auto it = std::find_if(config.magics.begin(), config.magics.end(),
                               [id](const mir2::MagicConfig& magic) {
                                 return magic.id == id;
                               });
  assert(it != config.magics.end());
  return *it;
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

mir2::CharacterRecord make_character() {
  mir2::CharacterRecord character;
  character.account_id = "acct_protocol";
  character.character_name = "ProtocolCaster";
  character.map_id = "0";
  character.x = 10;
  character.y = 10;
  character.ability.level = 1;
  character.ability.hp = 15;
  character.ability.max_hp = 15;
  character.ability.mp = 20;
  character.ability.max_mp = 20;
  character.ability.max_exp = 100;
  character.ability.max_weight = 30;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  character.magics[0].magic_id = 1;
  character.magics[0].level = 1;
  character.magics[0].key = '1';
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

mir2::LogicCommand make_spell(std::uint64_t session_id, std::uint64_t target_actor_id,
                              std::int32_t x, std::int32_t y, std::int32_t magic_id) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::spell;
  command.session_id = session_id;
  command.target_actor_id = target_actor_id;
  command.x = x;
  command.y = y;
  command.game_message.ident = mir2::kCmSpell;
  command.game_message.tag = static_cast<std::uint16_t>(magic_id);
  return command;
}

mir2::LegacyClientMagic decode_first_client_magic(const std::string& body) {
  const auto slash = body.find('/');
  assert(slash != std::string::npos);
  mir2::LegacyClientMagic magic{};
  assert(mir2::legacy_decode_buffer(body.substr(0, slash), &magic, sizeof(magic)));
  return magic;
}

void append_dispatch(mir2::RuntimeDispatch& target, const mir2::RuntimeDispatch& source) {
  target.session_events.insert(target.session_events.end(), source.session_events.begin(),
                               source.session_events.end());
  target.legacy_traces.insert(target.legacy_traces.end(), source.legacy_traces.begin(),
                              source.legacy_traces.end());
}

mir2::RuntimeDispatch advance_ticks(mir2::LogicRuntime& runtime, std::int32_t ticks) {
  mir2::RuntimeDispatch combined;
  for (std::int32_t tick = 0; tick < ticks; ++tick) {
    append_dispatch(combined, runtime.tick());
  }
  return combined;
}

const mir2::LegacyRuntimeTrace* find_trace(const mir2::RuntimeDispatch& dispatch,
                                           const std::string& action) {
  const auto it = std::find_if(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                               [&](const mir2::LegacyRuntimeTrace& trace) {
                                 return trace.stage == "LegacySpell" && trace.action == action;
                               });
  return it != dispatch.legacy_traces.end() ? &*it : nullptr;
}

}  // namespace

int main() {
  assert(mir2::kSmAddMagic == 210);
  assert(mir2::kSmSendMyMagic == 211);
  assert(mir2::kSmDelMagic == 212);
  assert(mir2::kSmMagicFire == 638);
  assert(mir2::kSmMagicFireFail == 639);
  assert(mir2::kSmMagicLvExp == 640);
  assert(mir2::kSmCharStatusChanged == 657);
  assert(mir2::kSmAreaState == 708);
  assert(mir2::kSmDelMagic != mir2::kSmAreaState);

  const auto loaded = mir2::ConfigLoader{}.load(std::filesystem::path(MIR2_CONFIG_DIR));
  const auto& fireball = find_magic(loaded, 1);
  assert(fireball.legacy.legacy_present);

  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "ProtocolMap", {}, 20, 20, 10, 10});
  config.magics.push_back(fireball);

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  static_cast<void>(runtime.route_logic_command(make_enter(501, make_character())));
  const auto login = runtime.tick();

  const auto packet = find_packet(login, mir2::kSmSendMyMagic);
  assert(packet.has_value());
  assert(packet->message.series == 1);
  assert(packet->message.recog == ((fireball.legacy.delay_time ^ 0x773F1A34) ^ 0x4BBC2255));

  const auto client_magic = decode_first_client_magic(packet->body);
  assert(client_magic.key == '1');
  assert(client_magic.level == 1);
  assert(client_magic.def.magic_id == 1);
  assert(mir2::to_string(client_magic.def.magic_name) == fireball.name);
  assert(client_magic.def.effect_type == 1);
  assert(client_magic.def.effect == 1);
  assert(client_magic.def.spell == 4);
  assert(client_magic.def.min_power == 8);
  assert(client_magic.def.max_power == 8);
  const std::array<std::uint8_t, 4> expected_need_level{7, 11, 16, 16};
  const std::array<std::int32_t, 4> expected_max_train{500, 1500, 3000, 3000};
  assert(client_magic.def.need_level == expected_need_level);
  assert(client_magic.def.max_train == expected_max_train);
  assert(client_magic.def.max_train_level == 3);
  assert(client_magic.def.delay_time == 600);
  assert(client_magic.def.def_spell == 1);
  assert(client_magic.def.def_min_power == 2);
  assert(client_magic.def.def_max_power == 2);

  {
    mir2::HostConfig spell_config;
    spell_config.runtime.legacy_random_seed = 1;
    spell_config.maps.push_back(mir2::MapConfig{"0", "UndeadMagicMap", {}, 20, 20, 10, 10});
    spell_config.spawns.push_back(
        mir2::SpawnConfig{"0", "monster", "UndeadTarget", 10, 9, 30000, 1, 100,
                          0, 0, 0, 20, 1});
    auto slayer = mir2::ItemConfig{};
    slayer.id = 900;
    slayer.name = "Undead Spell Focus";
    slayer.std_mode = 5;
    slayer.dura_max = 1000;
    slayer.equip_slot = mir2::kEquipWeapon;
    slayer.undead = 4;
    spell_config.items.push_back(slayer);
    spell_config.magics.push_back(fireball);

    mir2::LogicRuntime spell_runtime(spell_config);
    spell_runtime.initialize();
    auto caster = make_character();
    caster.character_name = "UndeadPowerCaster";
    caster.ability.level = 20;
    caster.ability.mp = 50;
    caster.ability.max_mp = 50;
    caster.ability.mc = mir2::make_word(2, 2);
    caster.equipped_items[mir2::kEquipWeapon] =
        mir2::LegacyUserItem{9001, static_cast<std::uint16_t>(slayer.id), 1000, 1000};
    static_cast<void>(spell_runtime.route_logic_command(make_enter(601, caster)));
    static_cast<void>(spell_runtime.tick());
    static_cast<void>(spell_runtime.route_logic_command(make_spell(601, 1, 10, 9, 1)));
    const auto immediate = spell_runtime.tick();
    const auto queued = find_trace(immediate, "delay_magic_queued");
    assert(queued != nullptr);
    const auto delayed = advance_ticks(spell_runtime, 30);
    const auto struck = find_trace(delayed, "mag_struck");
    assert(struck != nullptr);
    assert(struck->damage == 14);
  }

  return 0;
}
