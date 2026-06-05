#include <cassert>
#include <cstdint>
#include <optional>
#include <string>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

std::optional<mir2::DecodedLegacyGamePacket> find_packet(
    const mir2::RuntimeDispatch& dispatch, std::uint64_t session_id, std::uint16_t ident) {
  for (const auto& event : dispatch.session_events) {
    if (event.session_id != session_id) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return decoded;
    }
  }
  return std::nullopt;
}

mir2::CharacterRecord make_character(std::string name, int x, int y, bool with_item = false) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = x;
  character.y = y;
  character.ability.hp = 20;
  character.ability.max_hp = 20;
  character.ability.max_weight = 100;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  if (with_item) {
    character.bag_items[0].index = 1;
    character.bag_items[0].make_index = 1001;
    character.bag_items[0].dura = 1000;
    character.bag_items[0].dura_max = 1000;
  }
  return character;
}

void enter(mir2::LogicRuntime& runtime, std::uint64_t session_id,
           const mir2::CharacterRecord& character) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = session_id;
  command.account_id = character.account_id;
  command.character_name = character.character_name;
  command.map_id = character.map_id;
  command.x = character.x;
  command.y = character.y;
  command.character = character;
  static_cast<void>(runtime.route_logic_command(command));
  static_cast<void>(runtime.tick(1000));
}

void advance(mir2::LogicRuntime& runtime, int ticks = 12, std::uint64_t start_ms = 1020) {
  for (int index = 0; index < ticks; ++index) {
    static_cast<void>(runtime.tick(start_ms + static_cast<std::uint64_t>(index) * 20ULL));
  }
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.budgets.tick_ms = 20;
  config.maps.push_back(mir2::MapConfig{"0", "ItemLifetimeMap", {}, 20, 20, 5, 5});
  config.items.push_back(mir2::ItemConfig{1, "Wooden Sword", 3, 100, 5, 1, 1, 1000, 1, 0, 0});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  enter(runtime, 1, make_character("Dropper", 5, 5, true));
  enter(runtime, 2, make_character("Watcher", 6, 5));
  advance(runtime);

  mir2::LogicCommand drop;
  drop.kind = mir2::LogicCommandKind::drop_item;
  drop.session_id = 1;
  drop.item_make_index = 1001;
  drop.text = "Wooden Sword";
  static_cast<void>(runtime.route_logic_command(drop));
  const auto drop_dispatch = runtime.tick(2000);
  assert(find_packet(drop_dispatch, 1, mir2::kSmItemShow).has_value());
  assert(find_packet(drop_dispatch, 2, mir2::kSmItemShow).has_value());

  const auto before_expire = runtime.tick(2000 + 59ULL * 60ULL * 1000ULL);
  assert(!find_packet(before_expire, 2, mir2::kSmItemHide).has_value());

  const auto at_boundary = runtime.tick(2000 + 60ULL * 60ULL * 1000ULL);
  assert(!find_packet(at_boundary, 2, mir2::kSmItemHide).has_value());

  const auto expired = runtime.tick(2000 + 60ULL * 60ULL * 1000ULL + 1ULL);
  assert(find_packet(expired, 2, mir2::kSmItemHide).has_value());
  return 0;
}
