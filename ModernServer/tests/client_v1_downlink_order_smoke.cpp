#include <cassert>
#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "services/client_v1_admission_registry.hpp"
#include "services/client_v1_game_gateway_service.hpp"
#include "shared/protocol/client_v1/protocol.hpp"

namespace {

constexpr std::uint64_t kSessionId = 902;

mir2::LegacyPacket make_actor_walk_packet() {
  return mir2::make_legacy_game_packet(
      kSessionId, 0, 0, mir2::make_default_message(mir2::kSmWalk, 42, 12, 13, 2));
}

mir2::LegacyPacket make_item_show_packet() {
  return mir2::make_legacy_game_packet(
      kSessionId, 0, 0, mir2::make_default_message(mir2::kSmItemShow, 901, 11, 10, 7),
      mir2::legacy_encode_string("Gold"));
}

mir2::LegacyPacket make_item_hide_packet() {
  return mir2::make_legacy_game_packet(
      kSessionId, 0, 0, mir2::make_default_message(mir2::kSmItemHide, 901, 11, 10, 0));
}

mir2::LegacyPacket make_sys_message_packet() {
  return mir2::make_legacy_game_packet(
      kSessionId, 0, 0,
      mir2::make_default_message(mir2::kSmSysMessage, 0, mir2::make_word(5, 1), 0, 0),
      mir2::legacy_encode_string("server notice"));
}

template <typename T>
const T& require_message(const std::vector<mir2::client_v1::Message>& messages,
                         std::size_t index) {
  assert(index < messages.size());
  const auto* value = std::get_if<T>(&messages[index]);
  assert(value != nullptr);
  return *value;
}

}  // namespace

int main() {
  auto admissions = std::make_shared<mir2::ClientV1AdmissionRegistry>();
  mir2::ClientV1GameGatewayService service(admissions);
  service.seed_session_for_test(kSessionId);

  std::vector<mir2::client_v1::Message> messages;
  service.translate_legacy_packet_for_test(kSessionId, make_actor_walk_packet(), messages);
  assert(messages.size() == 2);

  const auto& upsert = require_message<mir2::client_v1::ActorUpsert>(messages, 0);
  assert(upsert.actor.actor_id == 42);
  assert(upsert.actor.x == 12);
  assert(upsert.actor.y == 13);
  assert(upsert.actor.dir == 2);

  const auto& walk = require_message<mir2::client_v1::ActorAction>(messages, 1);
  assert(walk.actor_id == 42);
  assert(walk.kind == mir2::client_v1::ActorActionKind::walk);
  assert(walk.legacy_ident == mir2::kSmWalk);
  assert(walk.x == 12);
  assert(walk.y == 13);
  assert(walk.dir == 2);

  messages.clear();
  service.translate_legacy_packet_for_test(
      kSessionId, mir2::make_legacy_raw_packet(kSessionId, "+GOOD/123"), messages);
  service.translate_legacy_packet_for_test(kSessionId, make_item_show_packet(), messages);
  service.translate_legacy_packet_for_test(kSessionId, make_item_hide_packet(), messages);
  service.translate_legacy_packet_for_test(kSessionId, make_sys_message_packet(), messages);
  assert(messages.size() == 4);

  const auto& ack = require_message<mir2::client_v1::ActionAck>(messages, 0);
  assert(ack.ok);
  assert(ack.server_time_ms == 123);

  const auto& add = require_message<mir2::client_v1::GroundItemAdd>(messages, 1);
  assert(add.item.object_id == 901);
  assert(add.item.x == 11);
  assert(add.item.y == 10);
  assert(add.item.looks == 7);
  assert(add.item.name == "Gold");

  const auto& remove = require_message<mir2::client_v1::GroundItemRemove>(messages, 2);
  assert(remove.object_id == 901);
  assert(remove.x == 11);
  assert(remove.y == 10);

  const auto& chat = require_message<mir2::client_v1::ChatLine>(messages, 3);
  assert(chat.text == "server notice");
  assert(chat.fore_color == 5);
  assert(chat.back_color == 1);

  return 0;
}
