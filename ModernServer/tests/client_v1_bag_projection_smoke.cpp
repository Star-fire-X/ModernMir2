#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "services/client_v1_admission_registry.hpp"
#include "services/client_v1_game_gateway_service.hpp"
#include "shared/protocol/client_v1/protocol.hpp"

namespace {

constexpr std::uint64_t kSessionId = 77;

int fail(const char* stage) {
  std::cerr << "client_v1_bag_projection_smoke failed at " << stage << '\n';
  return 1;
}

template <typename T>
std::optional<T> find_message(const std::vector<mir2::client_v1::Message>& messages) {
  for (const auto& message : messages) {
    if (const auto* value = std::get_if<T>(&message); value != nullptr) {
      return *value;
    }
  }
  return std::nullopt;
}

mir2::LegacyClientItem client_item(std::int32_t make_index, std::string name,
                                   std::uint8_t std_mode = 1) {
  mir2::LegacyClientItem item;
  mir2::set_short_string(item.item.name, name);
  item.item.std_mode = std_mode;
  item.item.looks = static_cast<std::uint16_t>(make_index & 0xffff);
  item.make_index = make_index;
  item.dura = 1;
  item.dura_max = 1;
  return item;
}

mir2::LegacyPacket item_packet(std::uint16_t ident, const mir2::LegacyClientItem& item) {
  return mir2::make_legacy_game_packet(
      kSessionId, 0, 0,
      mir2::make_default_message(ident, 1, 0, 0, 1),
      mir2::legacy_encode_buffer(&item, sizeof(item)));
}

mir2::LegacyPacket bag_packet(const std::vector<mir2::LegacyClientItem>& items) {
  std::string body;
  for (const auto& item : items) {
    body += mir2::legacy_encode_buffer(&item, sizeof(item));
    body.push_back('/');
  }
  return mir2::make_legacy_game_packet(
      kSessionId, 0, 0,
      mir2::make_default_message(mir2::kSmBagItems, 1, 0, 0,
                                 static_cast<std::uint16_t>(items.size())),
      body);
}

}  // namespace

int main() {
  auto admissions = std::make_shared<mir2::ClientV1AdmissionRegistry>();
  mir2::ClientV1GameGatewayService service(admissions);
  service.seed_session_for_test(kSessionId);

  const auto first = client_item(1001, "First");
  const auto second = client_item(1002, "Second");
  const auto third = client_item(1003, "Third");

  std::vector<mir2::client_v1::Message> messages;
  service.translate_legacy_packet_for_test(kSessionId, bag_packet({first, second}), messages);
  auto snapshot = find_message<mir2::client_v1::BagSnapshot>(messages);
  if (!snapshot.has_value() || snapshot->items.size() != 2 ||
      snapshot->items[0].slot != 6 || snapshot->items[0].item.make_index != 1001 ||
      snapshot->items[1].slot != 7 || snapshot->items[1].item.make_index != 1002) {
    return fail("initial bag snapshot");
  }

  messages.clear();
  service.translate_legacy_packet_for_test(kSessionId, item_packet(mir2::kSmDelItem, first),
                                           messages);
  const auto remove = find_message<mir2::client_v1::InventoryRemove>(messages);
  snapshot = find_message<mir2::client_v1::BagSnapshot>(messages);
  if (!remove.has_value() || remove->slot != 6 || !snapshot.has_value() ||
      snapshot->items.size() != 1 || snapshot->items[0].slot != 6 ||
      snapshot->items[0].item.make_index != 1002) {
    return fail("delete compacts bag projection");
  }

  messages.clear();
  service.translate_legacy_packet_for_test(kSessionId, item_packet(mir2::kSmAddItem, third),
                                           messages);
  const auto add = find_message<mir2::client_v1::InventoryAdd>(messages);
  if (!add.has_value() || add->entry.slot != 7 || add->entry.item.make_index != 1003) {
    return fail("add uses compacted tail slot");
  }

  messages.clear();
  auto updated_second = second;
  updated_second.dura = 9;
  service.translate_legacy_packet_for_test(kSessionId, item_packet(mir2::kSmUpdateItem, updated_second),
                                           messages);
  const auto update = find_message<mir2::client_v1::InventoryUpdate>(messages);
  if (!update.has_value() || update->entry.slot != 6 || update->entry.item.make_index != 1002 ||
      update->entry.item.dura != 9) {
    return fail("update follows compacted slot");
  }

  return 0;
}
