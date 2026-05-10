#include <cassert>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "config/config_loader.hpp"
#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"

#ifndef MIR2_CONFIG_DIR
#error "MIR2_CONFIG_DIR must be defined by CMake"
#endif

#undef assert
#define assert(expression)       \
  do {                           \
    if (!(expression)) {         \
      std::abort();              \
    }                            \
  } while (false)

namespace {

const mir2::MagicConfig& find_magic(const mir2::HostConfig& config, const std::int32_t id) {
  const auto it = std::find_if(config.magics.begin(), config.magics.end(),
                               [id](const mir2::MagicConfig& magic) {
                                 return magic.id == id;
                               });
  assert(it != config.magics.end());
  return *it;
}

mir2::DecodedLegacyGamePacket decode(const mir2::LegacyPacket& packet) {
  const auto decoded = mir2::decode_legacy_game_packet(packet);
  assert(decoded.has_value());
  return *decoded;
}

mir2::LegacyPacket make_spell_packet_for_test(const std::int32_t magic_id,
                                              const std::int32_t effect) {
  return mir2::make_legacy_game_packet(
      701, 0, 0,
      mir2::make_default_message(mir2::kSmSpell, 42, 12, 13,
                                 static_cast<std::uint16_t>(effect)),
      std::to_string(magic_id));
}

mir2::LegacyPacket make_magic_fire_packet_for_test(const std::int32_t effect_type,
                                                   const std::int32_t effect) {
  std::int32_t target = 77;
  return mir2::make_legacy_game_packet(
      701, 0, 0,
      mir2::make_default_message(
          mir2::kSmMagicFire, 42, 12, 13,
          mir2::make_word(static_cast<std::uint8_t>(effect_type),
                          static_cast<std::uint8_t>(effect))),
      mir2::legacy_encode_buffer(&target, sizeof(target)));
}

}  // namespace

int main() {
  const auto config = mir2::ConfigLoader{}.load(std::filesystem::path(MIR2_CONFIG_DIR));
  const std::vector<std::int32_t> active_magic_ids{
      1,  2,  5,  6,  8,  9,  10, 11, 13, 14, 15, 16, 17, 18,
      19, 20, 21, 22, 23, 24, 28, 29, 30, 31, 32, 33, 35, 36};

  for (const auto magic_id : active_magic_ids) {
    const auto& magic = find_magic(config, magic_id);
    assert(magic.legacy.legacy_present);
    assert(!magic.legacy.is_sword_skill);

    const auto spell = decode(make_spell_packet_for_test(magic_id, magic.legacy.effect));
    assert(spell.message.ident == mir2::kSmSpell);
    assert(spell.message.recog == 42);
    assert(spell.message.param == 12);
    assert(spell.message.tag == 13);
    assert(spell.message.series == magic.legacy.effect);
    assert(spell.body == std::to_string(magic_id));

    const auto fire = decode(
        make_magic_fire_packet_for_test(magic.legacy.effect_type, magic.legacy.effect));
    assert(fire.message.ident == mir2::kSmMagicFire);
    assert((fire.message.series & 0xFFU) == static_cast<std::uint16_t>(magic.legacy.effect_type));
    assert(((fire.message.series >> 8U) & 0xFFU) ==
           static_cast<std::uint16_t>(magic.legacy.effect));
    std::int32_t target = 0;
    assert(mir2::legacy_decode_buffer(fire.body, &target, sizeof(target)));
    assert(target == 77);
  }

  const auto fail = decode(mir2::make_legacy_game_packet(
      701, 0, 0, mir2::make_default_message(mir2::kSmMagicFireFail, 42, 0, 0, 0)));
  assert(fail.message.ident == mir2::kSmMagicFireFail);
  assert(fail.message.recog == 42);
  assert(fail.message.param == 0);
  assert(fail.message.tag == 0);
  assert(fail.message.series == 0);
  assert(fail.body.empty());

  return 0;
}
