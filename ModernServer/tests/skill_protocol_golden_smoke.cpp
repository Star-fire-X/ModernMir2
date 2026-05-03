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

mir2::LegacyClientMagic decode_first_client_magic(const std::string& body) {
  const auto slash = body.find('/');
  assert(slash != std::string::npos);
  mir2::LegacyClientMagic magic{};
  assert(mir2::legacy_decode_buffer(body.substr(0, slash), &magic, sizeof(magic)));
  return magic;
}

}  // namespace

int main() {
  assert(mir2::kSmDelMagic == 212);
  assert(mir2::kSmMagicFire == 638);
  assert(mir2::kSmMagicFireFail == 639);
  assert(mir2::kSmMagicLvExp == 640);
  assert(mir2::kSmCharStatusChanged == 657);
  assert(mir2::kSmAreaState == 708);

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

  return 0;
}
