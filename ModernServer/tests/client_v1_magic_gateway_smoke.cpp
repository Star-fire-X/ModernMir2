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

mir2::LegacyPacket make_magic_fire_fail_packet(std::uint64_t session_id) {
  return mir2::make_legacy_game_packet(
      session_id, 0, 0, mir2::make_default_message(mir2::kSmMagicFireFail, 42, 0, 0, 0));
}

mir2::LegacyPacket make_actor_walk_packet(std::uint64_t session_id) {
  return mir2::make_legacy_game_packet(
      session_id, 0, 0, mir2::make_default_message(mir2::kSmWalk, 42, 12, 13, 2));
}

mir2::LegacyPacket make_actor_struck_packet(std::uint64_t session_id) {
  return mir2::make_legacy_game_packet(
      session_id, 0, 0, mir2::make_default_message(mir2::kSmStruck, 42, 5, 12, 7));
}

mir2::LegacyPacket make_actor_death_packet(std::uint64_t session_id) {
  return mir2::make_legacy_game_packet(
      session_id, 0, 0, mir2::make_default_message(mir2::kSmDeath, 42, 12, 13, 2));
}

mir2::LegacyPacket make_actor_alive_packet(std::uint64_t session_id) {
  return mir2::make_legacy_game_packet(
      session_id, 0, 0, mir2::make_default_message(mir2::kSmAlive, 42, 14, 15, 3));
}

mir2::LegacyPacket make_actor_hide_packet(std::uint64_t session_id, std::uint16_t ident) {
  return mir2::make_legacy_game_packet(
      session_id, 0, 0, mir2::make_default_message(ident, 77, 12, 13, 0));
}

mir2::LegacyPacket make_clear_objects_packet(std::uint64_t session_id) {
  return mir2::make_legacy_game_packet(
      session_id, 0, 0, mir2::make_default_message(mir2::kSmClearObjects, 0, 0, 0, 0));
}

mir2::LegacyPacket make_change_map_packet(std::uint64_t session_id) {
  return mir2::make_legacy_game_packet(
      session_id, 0, 0, mir2::make_default_message(mir2::kSmChangeMap, 0, 0, 0, 0),
      mir2::legacy_encode_string("1"));
}

mir2::LegacyPacket make_new_map_packet(std::uint64_t session_id) {
  return mir2::make_legacy_game_packet(
      session_id, 0, 0, mir2::make_default_message(mir2::kSmNewMap, 42, 14, 15, 3),
      mir2::legacy_encode_string("1"));
}

mir2::LegacyPacket make_map_description_packet(std::uint64_t session_id) {
  return mir2::make_legacy_game_packet(
      session_id, 0, 0, mir2::make_default_message(mir2::kSmMapDescription, 0, 0, 0, 0),
      mir2::legacy_encode_string("Bichon"));
}

mir2::LegacyPacket make_username_packet(std::uint64_t session_id) {
  return mir2::make_legacy_game_packet(
      session_id, 0, 0, mir2::make_default_message(mir2::kSmUsername, 42, 196, 0, 0),
      mir2::legacy_encode_string("Hero"));
}

mir2::LegacyPacket make_feature_changed_packet(std::uint64_t session_id) {
  return mir2::make_legacy_game_packet(
      session_id, 0, 0,
      mir2::make_default_message(mir2::kSmFeatureChanged, 42, 0x0304, 0x0102, 0));
}

mir2::LegacyPacket make_char_status_changed_packet(std::uint64_t session_id) {
  return mir2::make_legacy_game_packet(
      session_id, 0, 0,
      mir2::make_default_message(mir2::kSmCharStatusChanged, 42, 0x0008, 0x0000, 0));
}

mir2::LegacyPacket make_door_packet(std::uint64_t session_id, std::uint16_t ident) {
  return mir2::make_legacy_game_packet(
      session_id, 0, 0, mir2::make_default_message(ident, 0, 12, 13, 0));
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
  assert(magic_fire->legacy_ident == mir2::kSmMagicFire);

  messages.clear();
  service.translate_legacy_packet_for_test(kSessionId, make_magic_fire_fail_packet(kSessionId),
                                           messages);
  assert(messages.size() == 1);
  const auto* magic_fail = std::get_if<mir2::client_v1::ActorMagicFireFail>(&messages.front());
  assert(magic_fail != nullptr);
  assert(magic_fail->actor_id == 42);
  assert(magic_fail->legacy_ident == mir2::kSmMagicFireFail);

  messages.clear();
  service.translate_legacy_packet_for_test(kSessionId, make_actor_walk_packet(kSessionId),
                                           messages);
  assert(messages.size() == 2);
  assert(std::holds_alternative<mir2::client_v1::ActorUpsert>(messages[0]));
  const auto* walk = std::get_if<mir2::client_v1::ActorAction>(&messages[1]);
  assert(walk != nullptr);
  assert(walk->legacy_ident == mir2::kSmWalk);

  messages.clear();
  service.translate_legacy_packet_for_test(kSessionId, make_actor_struck_packet(kSessionId),
                                           messages);
  assert(messages.size() == 2);
  const auto* vitals = std::get_if<mir2::client_v1::ActorVitals>(&messages[0]);
  assert(vitals != nullptr);
  assert(vitals->legacy_ident == mir2::kSmStruck);
  const auto* struck = std::get_if<mir2::client_v1::ActorAction>(&messages[1]);
  assert(struck != nullptr);
  assert(struck->legacy_ident == mir2::kSmStruck);

  messages.clear();
  service.translate_legacy_packet_for_test(kSessionId, make_actor_death_packet(kSessionId),
                                           messages);
  assert(messages.size() == 1);
  const auto* death = std::get_if<mir2::client_v1::ActorDeath>(&messages.front());
  assert(death != nullptr);
  assert(death->legacy_ident == mir2::kSmDeath);

  messages.clear();
  service.translate_legacy_packet_for_test(kSessionId, make_actor_alive_packet(kSessionId),
                                           messages);
  assert(messages.size() == 2);
  const auto* alive_upsert = std::get_if<mir2::client_v1::ActorUpsert>(&messages[0]);
  assert(alive_upsert != nullptr);
  assert(alive_upsert->actor.x == 14 && alive_upsert->actor.y == 15 &&
         alive_upsert->actor.dir == 3);
  const auto* alive = std::get_if<mir2::client_v1::ActorAction>(&messages[1]);
  assert(alive != nullptr);
  assert(alive->legacy_ident == mir2::kSmAlive);
  assert(alive->x == 14 && alive->y == 15 && alive->dir == 3);
  character = service.session_character_for_test(kSessionId);
  assert(character.has_value());
  assert(character->x == 14 && character->y == 15 && character->dir == 3);

  messages.clear();
  service.translate_legacy_packet_for_test(
      kSessionId, make_actor_hide_packet(kSessionId, mir2::kSmDisappear), messages);
  assert(messages.size() == 1);
  const auto* remove = std::get_if<mir2::client_v1::ActorRemove>(&messages.front());
  assert(remove != nullptr);
  assert(remove->actor_id == 77);
  assert(remove->legacy_ident == mir2::kSmDisappear);

  messages.clear();
  service.translate_legacy_packet_for_test(
      kSessionId, make_actor_hide_packet(kSessionId, mir2::kSmSpaceMoveHide2), messages);
  assert(messages.size() == 1);
  remove = std::get_if<mir2::client_v1::ActorRemove>(&messages.front());
  assert(remove != nullptr);
  assert(remove->actor_id == 77);
  assert(remove->legacy_ident == mir2::kSmSpaceMoveHide2);

  messages.clear();
  service.translate_legacy_packet_for_test(kSessionId, make_clear_objects_packet(kSessionId),
                                           messages);
  assert(messages.size() == 1);
  assert(std::holds_alternative<mir2::client_v1::WorldClearObjects>(messages.front()));

  messages.clear();
  service.translate_legacy_packet_for_test(kSessionId, make_change_map_packet(kSessionId),
                                           messages);
  assert(messages.size() == 1);
  const auto* map_change = std::get_if<mir2::client_v1::MapChange>(&messages.front());
  assert(map_change != nullptr);
  assert(map_change->map_id == "1");

  messages.clear();
  service.translate_legacy_packet_for_test(kSessionId, make_new_map_packet(kSessionId),
                                           messages);
  assert(messages.size() == 1);
  const auto* map_entered = std::get_if<mir2::client_v1::MapEntered>(&messages.front());
  assert(map_entered != nullptr);
  assert(map_entered->map_id == "1");
  assert(map_entered->self_actor_id == 42);
  assert(map_entered->x == 14 && map_entered->y == 15);
  assert(map_entered->dir == 3);

  service.seed_session_for_test(kSessionId);
  messages.clear();
  service.translate_legacy_packet_for_test(kSessionId, make_new_map_packet(kSessionId),
                                           messages);
  assert(messages.empty());

  messages.clear();
  service.translate_legacy_packet_for_test(kSessionId, make_map_description_packet(kSessionId),
                                           messages);
  assert(messages.size() == 1);
  const auto* map_description =
      std::get_if<mir2::client_v1::MapDescription>(&messages.front());
  assert(map_description != nullptr);
  assert(map_description->title == "Bichon");

  messages.clear();
  service.translate_legacy_packet_for_test(kSessionId, make_username_packet(kSessionId),
                                           messages);
  assert(messages.size() == 1);
  const auto* username =
      std::get_if<mir2::client_v1::ActorIdentityUpdate>(&messages.front());
  assert(username != nullptr);
  assert(username->actor_id == 42);
  assert((username->mask & mir2::client_v1::kActorIdentityName) != 0U);
  assert((username->mask & mir2::client_v1::kActorIdentityNameColor) != 0U);
  assert(username->name == "Hero");
  assert(username->name_color == 196);

  messages.clear();
  service.translate_legacy_packet_for_test(kSessionId, make_feature_changed_packet(kSessionId),
                                           messages);
  assert(messages.size() == 1);
  const auto* feature =
      std::get_if<mir2::client_v1::ActorIdentityUpdate>(&messages.front());
  assert(feature != nullptr);
  assert(feature->mask == mir2::client_v1::kActorIdentityFeature);
  assert(feature->feature == 0x01020304);

  messages.clear();
  service.translate_legacy_packet_for_test(kSessionId,
                                           make_char_status_changed_packet(kSessionId),
                                           messages);
  assert(messages.size() == 1);
  const auto* status =
      std::get_if<mir2::client_v1::ActorIdentityUpdate>(&messages.front());
  assert(status != nullptr);
  assert(status->mask == mir2::client_v1::kActorIdentityStatus);
  assert(status->status == 8);

  messages.clear();
  service.translate_legacy_packet_for_test(
      kSessionId, make_door_packet(kSessionId, mir2::kSmOpenDoorOk), messages);
  assert(messages.size() == 1);
  const auto* open_door = std::get_if<mir2::client_v1::MapDoorState>(&messages.front());
  assert(open_door != nullptr);
  assert(open_door->x == 12 && open_door->y == 13 && open_door->open);

  messages.clear();
  service.translate_legacy_packet_for_test(
      kSessionId, make_door_packet(kSessionId, mir2::kSmCloseDoor), messages);
  assert(messages.size() == 1);
  const auto* close_door = std::get_if<mir2::client_v1::MapDoorState>(&messages.front());
  assert(close_door != nullptr);
  assert(close_door->x == 12 && close_door->y == 13 && !close_door->open);

  messages.clear();
  service.translate_legacy_packet_for_test(kSessionId, make_clear_objects_packet(kSessionId),
                                           messages);
  service.translate_legacy_packet_for_test(kSessionId, make_change_map_packet(kSessionId),
                                           messages);
  service.translate_legacy_packet_for_test(kSessionId, make_new_map_packet(kSessionId),
                                           messages);
  service.translate_legacy_packet_for_test(
      kSessionId, make_door_packet(kSessionId, mir2::kSmOpenDoorOk), messages);
  assert(messages.size() == 4);
  assert(std::holds_alternative<mir2::client_v1::WorldClearObjects>(messages[0]));
  assert(std::holds_alternative<mir2::client_v1::MapChange>(messages[1]));
  assert(std::holds_alternative<mir2::client_v1::MapEntered>(messages[2]));
  assert(std::holds_alternative<mir2::client_v1::MapDoorState>(messages[3]));
  return 0;
}
