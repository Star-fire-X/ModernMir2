#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>

#include "config/models.hpp"
#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "util/string_utils.hpp"
#include "world/make_index_allocator.hpp"
#include "world/logic_runtime.hpp"

namespace {

std::optional<mir2::DecodedLegacyGamePacket> find_packet(const mir2::RuntimeDispatch& dispatch,
                                                         std::uint16_t ident) {
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return decoded;
    }
  }
  return std::nullopt;
}

}  // namespace

int main() {
  static_assert(sizeof(mir2::LegacyUserItem) == 40);
  static_assert(offsetof(mir2::LegacyUserItem, make_index) == 0);
  static_assert(offsetof(mir2::LegacyUserItem, index) == 4);
  static_assert(offsetof(mir2::LegacyUserItem, dura) == 6);
  static_assert(offsetof(mir2::LegacyUserItem, dura_max) == 8);
  static_assert(offsetof(mir2::LegacyUserItem, desc) == 10);
  static_assert(offsetof(mir2::LegacyUserItem, color_r) == 24);
  static_assert(offsetof(mir2::LegacyUserItem, prefix) == 27);
  static_assert(sizeof(mir2::LegacyStdItem) == 69);
  static_assert(sizeof(mir2::LegacyClientItem) == 77);

  mir2::HostConfig host_config;
  host_config.maps.push_back(mir2::MapConfig{"0", "Compat", {}, 0, 0, 10, 10});
  mir2::ItemConfig config;
  config.id = 7;
  config.name = "Animated";
  config.weight = 2;
  config.price = 90;
  config.std_mode = 5;
  config.shape = 1;
  config.looks = 77;
  config.dura_max = 1200;
  config.ani_count = 6;
  host_config.items.push_back(config);

  mir2::LegacyUserItem item;
  item.index = 7;
  item.make_index = 300001;
  item.dura = 1000;
  item.dura_max = 1200;

  mir2::CharacterRecord hero;
  hero.account_id = "acct";
  hero.character_name = "Hero";
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;
  hero.bag_items[0] = item;

  mir2::LogicRuntime runtime(host_config);
  runtime.initialize();
  mir2::LogicCommand enter;
  enter.kind = mir2::LogicCommandKind::enter_world;
  enter.session_id = 42;
  enter.account_id = hero.account_id;
  enter.character_name = hero.character_name;
  enter.map_id = hero.map_id;
  enter.x = hero.x;
  enter.y = hero.y;
  enter.character = hero;
  static_cast<void>(runtime.route_logic_command(enter));
  static_cast<void>(runtime.tick(20));
  mir2::LogicCommand query;
  query.kind = mir2::LogicCommandKind::query_bag_items;
  query.session_id = 42;
  static_cast<void>(runtime.route_logic_command(query));
  const auto dispatch = runtime.tick(300);
  const auto bag_packet = find_packet(dispatch, mir2::kSmBagItems);
  if (!bag_packet.has_value()) {
    return 1;
  }
  mir2::LegacyClientItem client_item;
  const auto parts = mir2::util::split(bag_packet->body, '/');
  if (parts.empty() ||
      !mir2::legacy_decode_buffer(parts.front(), &client_item,
                                  sizeof(client_item)) ||
      client_item.item.ani_count != 6 || client_item.item.std_mode != 5 ||
      client_item.make_index != 300001 || client_item.dura_max != 1200) {
    return 1;
  }

  mir2::CharacterRecord character;
  character.equipped_items[0].index = 1;
  character.equipped_items[0].make_index = 250000;
  character.bag_items[0].index = 2;
  character.bag_items[0].make_index = 260000;
  character.storage_items[0].index = 3;
  character.storage_items[0].make_index = 270000;

  mir2::MerchantStateRecord merchant;
  merchant.goods.push_back(mir2::LegacyUserItem{});
  merchant.goods.back().index = 4;
  merchant.goods.back().make_index = 280000;

  mir2::MakeIndexAllocator allocator;
  allocator.reset();
  allocator.observe(character);
  allocator.observe(merchant);
  if (allocator.allocate() != 280001 || allocator.allocate() != 280002) {
    return 1;
  }

  allocator.reset();
  if (allocator.allocate() != mir2::MakeIndexAllocator::kLegacyRuntimeFloor) {
    return 1;
  }

  return 0;
}
