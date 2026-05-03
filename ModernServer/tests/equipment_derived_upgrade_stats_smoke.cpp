#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "protocol/legacy_edcode.hpp"
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

template <typename T>
std::optional<T> decode_body(std::string_view body) {
  T value{};
  if (!mir2::legacy_decode_buffer(body, &value, sizeof(value))) {
    return std::nullopt;
  }
  return value;
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "UpgradeMap", {}, 20, 20, 10, 10});
  mir2::ItemConfig sword{1, "Upgrade Sword", 3, 100, 5, 1, 1, 1000, 1, 0, 0};
  sword.dc = mir2::make_word(1, 3);
  sword.mc = mir2::make_word(0, 1);
  sword.sc = mir2::make_word(0, 1);
  sword.accurate = 1;
  config.items.push_back(sword);

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  mir2::CharacterRecord hero;
  hero.account_id = "acct";
  hero.character_name = "Hero";
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;
  hero.ability.level = 20;
  hero.ability.hp = 15;
  hero.ability.max_hp = 15;
  hero.ability.max_weight = 50;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;
  hero.bag_items[0] = mir2::LegacyUserItem{1001, 1, 1000, 1000};
  hero.bag_items[0].desc[0] = 2;
  hero.bag_items[0].desc[1] = 1;
  hero.bag_items[0].desc[2] = 3;
  hero.bag_items[0].desc[5] = 4;

  static_cast<void>(runtime.route_logic_command(make_enter(601, hero)));
  static_cast<void>(runtime.tick());

  static_cast<void>(runtime.route_logic_command(make_item_command(
      mir2::LogicCommandKind::take_on_item, 601, 1001, "Upgrade Sword", mir2::kEquipWeapon)));
  const auto take_on = runtime.tick();
  const auto ability_packet = find_packet(take_on, mir2::kSmAbility);
  const auto sub_packet = find_packet(take_on, mir2::kSmSubAbility);
  assert(ability_packet.has_value());
  assert(sub_packet.has_value());
  const auto ability = decode_body<mir2::LegacyAbility>(ability_packet->body);
  assert(ability.has_value());
  assert(ability->dc == mir2::make_word(1, 5));
  assert(ability->mc == mir2::make_word(0, 2));
  assert(ability->sc == mir2::make_word(0, 4));
  assert(ability->hand_weight == 3);
  assert(sub_packet->message.param == mir2::make_word(15, 10));

  auto snapshot = runtime.snapshot_character_actor("Hero");
  assert(snapshot.has_value());
  assert(snapshot->equipped_items[mir2::kEquipWeapon].make_index == 1001);

  auto zero_dura = snapshot->equipped_items[mir2::kEquipWeapon];
  zero_dura.dura = 0;
  snapshot->equipped_items[mir2::kEquipWeapon] = zero_dura;
  mir2::LogicRuntime zero_runtime(config);
  zero_runtime.initialize();
  static_cast<void>(zero_runtime.route_logic_command(make_enter(602, *snapshot)));
  const auto relogin = zero_runtime.tick();
  const auto relogin_ability_packet = find_packet(relogin, mir2::kSmAbility);
  assert(relogin_ability_packet.has_value());
  const auto relogin_ability =
      decode_body<mir2::LegacyAbility>(relogin_ability_packet->body);
  assert(relogin_ability.has_value());
  assert(relogin_ability->dc == mir2::make_word(0, 0));
  assert(relogin_ability->hand_weight == 3);

  return 0;
}
