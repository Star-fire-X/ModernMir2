#include <cassert>
#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

#include "config/models.hpp"
#include "core/local_bus.hpp"
#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "services/client_v1_admission_registry.hpp"
#include "shared/protocol/client_v1/protocol.hpp"
#include "storage/repository.hpp"
#include "services/client_v1_game_gateway_service.hpp"

namespace {

mir2::LegacyClientMagic make_client_magic(std::uint16_t magic_id, std::uint8_t level,
                                          std::int32_t train) {
  mir2::LegacyClientMagic magic;
  magic.key = '1';
  magic.level = level;
  magic.cur_train = train;
  magic.def.magic_id = magic_id;
  magic.def.delay_time = 600;
  magic.def.effect_type = 7;
  magic.def.effect = 32;
  mir2::set_short_string(magic.def.magic_name, "Fireball");
  return magic;
}

mir2::LegacyPacket make_add_magic_packet(std::uint64_t session_id,
                                         const mir2::LegacyClientMagic& magic) {
  return mir2::make_legacy_game_packet(
      session_id, 0, 0, mir2::make_default_message(mir2::kSmAddMagic, 0, 0, 0, 1),
      mir2::legacy_encode_buffer(&magic, sizeof(magic)));
}

mir2::LegacyPacket make_lvexp_packet(std::uint64_t session_id, std::int32_t magic_id,
                                     std::int32_t level, std::int32_t train) {
  return mir2::make_legacy_game_packet(
      session_id, 0, 0,
      mir2::make_default_message(mir2::kSmMagicLvExp, magic_id,
                                 static_cast<std::uint16_t>(level),
                                 mir2::low_word(train), mir2::high_word(train)));
}

mir2::LegacyPacket make_del_magic_packet(std::uint64_t session_id, std::int32_t magic_id) {
  return mir2::make_legacy_game_packet(
      session_id, 0, 0,
      mir2::make_default_message(mir2::kSmDelMagic, magic_id, 0, 0, 1));
}

mir2::LegacyPacket make_magic_fire_packet(std::uint64_t session_id) {
  std::int32_t target = 77;
  return mir2::make_legacy_game_packet(
      session_id, 0, 0,
      mir2::make_default_message(mir2::kSmMagicFire, 42, 12, 13, mir2::make_word(7, 32)),
      mir2::legacy_encode_buffer(&target, sizeof(target)));
}

const mir2::client_v1::MagicList& only_magic_list(
    const std::vector<mir2::client_v1::Message>& messages) {
  assert(messages.size() == 1);
  const auto* list = std::get_if<mir2::client_v1::MagicList>(&messages.front());
  assert(list != nullptr);
  return *list;
}

}  // namespace

int main() {
  constexpr std::uint64_t kSessionId = 901;
  auto admissions = std::make_shared<mir2::ClientV1AdmissionRegistry>();
  mir2::ClientV1GameGatewayService service(admissions);
  service.seed_session_for_test(kSessionId);

  std::vector<mir2::client_v1::Message> messages;
  const auto add_packet = make_add_magic_packet(kSessionId, make_client_magic(1, 0, 0));
  service.translate_legacy_packet_for_test(kSessionId, add_packet, messages);
  const auto& add_list = only_magic_list(messages);
  assert(add_list.magics.size() == 1);
  assert(add_list.magics.front().magic_id == 1);
  assert(add_list.magics.front().level == 0);
  assert(add_list.magics.front().train == 0);
  assert(add_list.magics.front().effect_type == 7);
  assert(add_list.magics.front().effect == 32);
  auto character = service.session_character_for_test(kSessionId);
  assert(character.has_value());
  if (character->magics[0].magic_id != 1) {
    return 1;
  }

  messages.clear();
  service.translate_legacy_packet_for_test(kSessionId,
                                           make_lvexp_packet(kSessionId, 1, 2, 345), messages);
  const auto& lvexp_list = only_magic_list(messages);
  assert(lvexp_list.magics.size() == 1);
  assert(lvexp_list.magics.front().level == 2);
  assert(lvexp_list.magics.front().train == 345);
  character = service.session_character_for_test(kSessionId);
  assert(character.has_value());
  if (character->magics[0].level != 2 || character->magics[0].cur_train != 345) {
    return 1;
  }

  messages.clear();
  service.translate_legacy_packet_for_test(kSessionId, make_del_magic_packet(kSessionId, 1),
                                           messages);
  const auto& del_list = only_magic_list(messages);
  assert(del_list.magics.empty());
  character = service.session_character_for_test(kSessionId);
  assert(character.has_value());
  if (!mir2::is_empty(character->magics[0])) {
    return 1;
  }

  messages.clear();
  service.translate_legacy_packet_for_test(kSessionId, make_magic_fire_packet(kSessionId),
                                           messages);
  assert(messages.size() == 1);
  const auto* magic_fire = std::get_if<mir2::client_v1::ActorMagicFire>(&messages.front());
  assert(magic_fire != nullptr);
  assert(magic_fire->actor_id == 42);
  assert(magic_fire->target_actor_id == 77);
  assert(magic_fire->x == 12);
  assert(magic_fire->y == 13);
  assert(magic_fire->effect_type == 7);
  assert(magic_fire->effect == 32);
  return 0;
}
