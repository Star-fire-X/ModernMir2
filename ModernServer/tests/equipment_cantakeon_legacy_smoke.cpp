#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

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

mir2::LogicCommand make_item_command(mir2::LogicCommandKind kind, std::uint64_t session_id,
                                     std::int32_t make_index, std::string name,
                                     std::int32_t slot) {
  mir2::LogicCommand command;
  command.kind = kind;
  command.session_id = session_id;
  command.item_make_index = make_index;
  command.item_slot = slot;
  command.text = std::move(name);
  return command;
}

std::optional<mir2::DecodedLegacyGamePacket> find_packet(const mir2::RuntimeDispatch& dispatch,
                                                         std::uint16_t ident) {
  for (const auto& event : dispatch.session_events) {
    if (event.kind != mir2::SessionEventKind::send_packet) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return decoded;
    }
  }
  return std::nullopt;
}

bool bag_has_make_index(const mir2::CharacterRecord& character, std::int32_t make_index) {
  return std::any_of(character.bag_items.begin(), character.bag_items.end(),
                     [&](const mir2::LegacyUserItem& item) {
                       return item.make_index == make_index;
                     });
}

mir2::RuntimeDispatch tick_player_due(mir2::LogicRuntime& runtime, std::uint64_t& now_ms) {
  now_ms += 251;
  return runtime.tick(now_ms);
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "EquipMap", {}, 20, 20, 10, 10});

  mir2::ItemConfig heavy_sword{1, "Heavy Sword", 8, 100, 5, 1, 1, 1000, 1, 0, 0};
  mir2::ItemConfig level_sword{2, "Level Sword", 2, 100, 5, 1, 2, 1000, 1, 0, 0};
  level_sword.need_level = 20;
  mir2::ItemConfig wrong_job_ring{3, "Wizard Ring", 1, 100, 22, 1, 3, 1000, 7, 0, 0};
  wrong_job_ring.job = 1;
  mir2::ItemConfig cursed_ring{4, "Cursed Ring", 1, 100, 22, 1, 4, 1000, 7, 0, 0};
  mir2::ItemConfig plain_ring{5, "Plain Ring", 1, 100, 22, 1, 5, 1000, 7, 0, 0};
  mir2::ItemConfig dc_sword{6, "Dc Sword", 2, 100, 5, 1, 6, 1000, 1, 0, 0};
  dc_sword.need = 1;
  dc_sword.need_level = 3;
  config.items = {heavy_sword, level_sword, wrong_job_ring, cursed_ring, plain_ring, dc_sword};

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  mir2::CharacterRecord hero;
  hero.account_id = "acct";
  hero.character_name = "Hero";
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;
  hero.job = 0;
  hero.sex = 0;
  hero.ability.level = 10;
  hero.ability.hp = 15;
  hero.ability.max_hp = 15;
  hero.ability.max_weight = 50;
  hero.ability.max_wear_weight = 10;
  hero.ability.max_hand_weight = 5;
  hero.bag_items[0] = mir2::LegacyUserItem{1001, 1, 1000, 1000};
  hero.bag_items[1] = mir2::LegacyUserItem{1002, 2, 1000, 1000};
  hero.bag_items[2] = mir2::LegacyUserItem{1003, 3, 1000, 1000};
  hero.bag_items[3] = mir2::LegacyUserItem{1005, 5, 1000, 1000};
  hero.bag_items[4] = mir2::LegacyUserItem{1006, 6, 1000, 1000};
  hero.equipped_items[mir2::kEquipRingLeft] = mir2::LegacyUserItem{1004, 4, 1000, 1000};
  hero.equipped_items[mir2::kEquipRingLeft].desc[7] = 1;

  static_cast<void>(runtime.route_logic_command(make_enter(501, hero)));
  std::uint64_t now_ms = 20;
  static_cast<void>(runtime.tick(now_ms));

  static_cast<void>(runtime.route_logic_command(make_item_command(
      mir2::LogicCommandKind::take_on_item, 501, 1001, "Heavy Sword", mir2::kEquipWeapon)));
  auto dispatch = tick_player_due(runtime, now_ms);
  assert(find_packet(dispatch, mir2::kSmTakeOnFail).has_value());
  auto snapshot = runtime.snapshot_character_actor("Hero");
  assert(snapshot.has_value());
  assert(bag_has_make_index(*snapshot, 1001));
  assert(snapshot->equipped_items[mir2::kEquipWeapon].make_index == 0);

  static_cast<void>(runtime.route_logic_command(make_item_command(
      mir2::LogicCommandKind::take_on_item, 501, 1002, "Level Sword", mir2::kEquipWeapon)));
  dispatch = tick_player_due(runtime, now_ms);
  assert(find_packet(dispatch, mir2::kSmTakeOnFail).has_value());
  snapshot = runtime.snapshot_character_actor("Hero");
  assert(snapshot.has_value());
  assert(bag_has_make_index(*snapshot, 1002));

  static_cast<void>(runtime.route_logic_command(make_item_command(
      mir2::LogicCommandKind::take_on_item, 501, 1006, "Dc Sword", mir2::kEquipWeapon)));
  dispatch = tick_player_due(runtime, now_ms);
  assert(find_packet(dispatch, mir2::kSmTakeOnFail).has_value());
  snapshot = runtime.snapshot_character_actor("Hero");
  assert(snapshot.has_value());
  assert(bag_has_make_index(*snapshot, 1006));

  static_cast<void>(runtime.route_logic_command(make_item_command(
      mir2::LogicCommandKind::take_on_item, 501, 1003, "Wizard Ring", mir2::kEquipRingRight)));
  dispatch = tick_player_due(runtime, now_ms);
  assert(find_packet(dispatch, mir2::kSmTakeOnFail).has_value());
  snapshot = runtime.snapshot_character_actor("Hero");
  assert(snapshot.has_value());
  assert(bag_has_make_index(*snapshot, 1003));

  static_cast<void>(runtime.route_logic_command(make_item_command(
      mir2::LogicCommandKind::take_on_item, 501, 1005, "Plain Ring", mir2::kEquipRingLeft)));
  dispatch = tick_player_due(runtime, now_ms);
  assert(find_packet(dispatch, mir2::kSmTakeOnFail).has_value());
  snapshot = runtime.snapshot_character_actor("Hero");
  assert(snapshot.has_value());
  assert(snapshot->equipped_items[mir2::kEquipRingLeft].make_index == 1004);
  assert(bag_has_make_index(*snapshot, 1005));

  return 0;
}
