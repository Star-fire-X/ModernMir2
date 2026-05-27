#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "protocol/legacy_edcode.hpp"
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

std::vector<std::uint16_t> packet_idents_for_session(
    const mir2::RuntimeDispatch& dispatch, std::uint64_t session_id) {
  std::vector<std::uint16_t> idents;
  for (const auto& event : dispatch.session_events) {
    if (event.session_id != session_id) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value()) {
      idents.push_back(decoded->message.ident);
    }
  }
  return idents;
}

std::size_t first_ident_index(const std::vector<std::uint16_t>& idents,
                              const std::uint16_t ident) {
  for (std::size_t index = 0; index < idents.size(); ++index) {
    if (idents[index] == ident) {
      return index;
    }
  }
  return idents.size();
}

mir2::CharacterRecord make_character(std::string name = "Hero", int x = 10, int y = 10) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = x;
  character.y = y;
  character.ability.hp = 15;
  character.ability.max_hp = 15;
  return character;
}

void advance(mir2::LogicRuntime& runtime, int ticks = 12) {
  for (int index = 0; index < ticks; ++index) {
    static_cast<void>(runtime.tick());
  }
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.budgets.tick_ms = 20;
  mir2::MapConfig source;
  source.id = "0";
  source.title = "Source";
  source.width = 20;
  source.height = 20;
  source.home_x = 10;
  source.home_y = 10;
  source.gates.push_back(mir2::MapGateConfig{11, 10, "1", 5, 5, false});
  config.maps.push_back(source);
  config.maps.push_back(mir2::MapConfig{"1", "Target", {}, 20, 20, 5, 5});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  mir2::LogicCommand enter;
  enter.kind = mir2::LogicCommandKind::enter_world;
  enter.session_id = 1;
  enter.account_id = "acct";
  enter.character_name = "Hero";
  enter.map_id = "0";
  enter.x = 10;
  enter.y = 10;
  enter.character = make_character();
  static_cast<void>(runtime.route_logic_command(enter));
  static_cast<void>(runtime.tick());

  mir2::LogicCommand watcher_enter;
  watcher_enter.kind = mir2::LogicCommandKind::enter_world;
  watcher_enter.session_id = 2;
  watcher_enter.account_id = "acct_Watcher";
  watcher_enter.character_name = "Watcher";
  watcher_enter.map_id = "0";
  watcher_enter.x = 10;
  watcher_enter.y = 11;
  watcher_enter.character = make_character("Watcher", 10, 11);
  static_cast<void>(runtime.route_logic_command(watcher_enter));
  static_cast<void>(runtime.tick());
  advance(runtime);

  mir2::LogicCommand walk;
  walk.kind = mir2::LogicCommandKind::walk;
  walk.session_id = 1;
  walk.x = 11;
  walk.y = 10;
  static_cast<void>(runtime.route_logic_command(walk));
  const auto transfer_dispatch = runtime.tick();
  assert(find_packet(transfer_dispatch, 1, mir2::kSmClearObjects).has_value());
  assert(find_packet(transfer_dispatch, 1, mir2::kSmChangeMap).has_value());
  assert(!find_packet(transfer_dispatch, 1, mir2::kSmNewMap).has_value());
  assert(!find_packet(transfer_dispatch, 1, mir2::kSmLogon).has_value());
  const auto change_map = find_packet(transfer_dispatch, 1, mir2::kSmChangeMap);
  assert(change_map.has_value());
  assert(mir2::legacy_decode_string(change_map->body) == "1");
  assert(change_map->message.param == 5);
  assert(change_map->message.tag == 5);
  const auto hero_idents = packet_idents_for_session(transfer_dispatch, 1);
  const auto clear_index = first_ident_index(hero_idents, mir2::kSmClearObjects);
  const auto change_index = first_ident_index(hero_idents, mir2::kSmChangeMap);
  assert(clear_index < change_index);
  assert(find_packet(transfer_dispatch, 2, mir2::kSmDisappear).has_value());
  assert(!find_packet(transfer_dispatch, 2, mir2::kSmNewMap).has_value());

  const auto snapshot = runtime.snapshot_character_actor("Hero");
  assert(snapshot.has_value());
  assert(snapshot->map_id == "1");
  assert(snapshot->x == 5 && snapshot->y == 5);
  return 0;
}
