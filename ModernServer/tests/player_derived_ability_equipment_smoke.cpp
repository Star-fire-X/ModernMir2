#include <optional>
#include <string>
#include <vector>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

std::optional<mir2::DecodedLegacyGamePacket> find_packet(const mir2::RuntimeDispatch& dispatch,
                                                         std::uint16_t ident,
                                                         std::uint64_t session_id) {
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

template <typename T>
std::optional<T> decode_body(std::string_view body) {
  T value{};
  if (!mir2::legacy_decode_buffer(body, &value, sizeof(value))) {
    return std::nullopt;
  }
  return value;
}

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

}  // namespace

int main() {
  auto fail = [](int stage) { return stage; };

  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "DerivedMap", {}, 20, 20, 10, 10});
  mir2::ItemConfig sword{1, "Derived Sword", 3, 100, 5, 1, 1, 1000, 1, 5, 7};
  sword.ac = mir2::make_word(1, 2);
  sword.mac = mir2::make_word(1, 1);
  sword.dc = mir2::make_word(2, 5);
  sword.mc = mir2::make_word(1, 3);
  sword.sc = mir2::make_word(4, 4);
  sword.accurate = 2;
  sword.agility = 3;
  config.items.push_back(sword);

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  mir2::CharacterRecord hero;
  hero.account_id = "guest";
  hero.character_name = "Hero";
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;
  hero.ability.level = 20;
  hero.ability.ac = mir2::make_word(1, 1);
  hero.ability.dc = mir2::make_word(1, 1);
  hero.ability.hp = 10;
  hero.ability.max_hp = 20;
  hero.ability.mp = 5;
  hero.ability.max_mp = 10;
  hero.ability.max_weight = 50;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;
  hero.bag_items[0].index = 1;
  hero.bag_items[0].make_index = 1001;
  hero.bag_items[0].dura = 1000;
  hero.bag_items[0].dura_max = 1000;

  static_cast<void>(runtime.route_logic_command(make_enter(201, hero)));
  const auto login = runtime.tick();
  if (!find_packet(login, mir2::kSmNewMap, 201).has_value()) {
    return fail(1);
  }

  static_cast<void>(runtime.route_logic_command(make_item_command(
      mir2::LogicCommandKind::take_on_item, 201, 1001, "Derived Sword", 1)));
  const auto take_on = runtime.tick();
  const auto ability_packet = find_packet(take_on, mir2::kSmAbility, 201);
  const auto sub_ability = find_packet(take_on, mir2::kSmSubAbility, 201);
  if (!ability_packet.has_value() || !sub_ability.has_value()) {
    return fail(2);
  }
  const auto ability = decode_body<mir2::LegacyAbility>(ability_packet->body);
  if (!ability.has_value()) {
    return fail(3);
  }
  if (ability->ac != mir2::make_word(2, 3) || ability->mac != mir2::make_word(1, 1) ||
      ability->dc != mir2::make_word(3, 6) || ability->mc != mir2::make_word(1, 3) ||
      ability->sc != mir2::make_word(4, 4) || ability->hp != 10 || ability->max_hp != 25 ||
      ability->mp != 5 || ability->max_mp != 17 || ability->weight != 0 ||
      ability->wear_weight != 0 || ability->hand_weight != 3) {
    return fail(4);
  }
  if (sub_ability->message.param != mir2::make_word(12, 13)) {
    return fail(5);
  }

  const auto equipped_snapshot = runtime.snapshot_character_actor("Hero");
  if (!equipped_snapshot.has_value() ||
      equipped_snapshot->equipped_items[1].make_index != 1001 ||
      equipped_snapshot->ability.dc != mir2::make_word(3, 6)) {
    return fail(6);
  }

  static_cast<void>(runtime.route_logic_command(make_item_command(
      mir2::LogicCommandKind::take_off_item, 201, 1001, "Derived Sword", 1)));
  const auto take_off = runtime.tick();
  const auto base_ability_packet = find_packet(take_off, mir2::kSmAbility, 201);
  const auto base_sub_ability = find_packet(take_off, mir2::kSmSubAbility, 201);
  if (!base_ability_packet.has_value() || !base_sub_ability.has_value()) {
    return fail(7);
  }
  const auto base_ability = decode_body<mir2::LegacyAbility>(base_ability_packet->body);
  if (!base_ability.has_value()) {
    return fail(8);
  }
  if (base_ability->ac != mir2::make_word(1, 1) || base_ability->dc != mir2::make_word(1, 1) ||
      base_ability->max_hp != 20 || base_ability->max_mp != 10 ||
      base_ability->weight != 3 || base_ability->wear_weight != 0 ||
      base_ability->hand_weight != 0) {
    return fail(9);
  }
  if (base_sub_ability->message.param != mir2::make_word(10, 10)) {
    return fail(10);
  }

  const auto stored_snapshot = runtime.snapshot_character_actor("Hero");
  if (!stored_snapshot.has_value() || stored_snapshot->bag_items[0].make_index != 1001 ||
      stored_snapshot->equipped_items[1].make_index != 0 ||
      stored_snapshot->ability.dc != mir2::make_word(1, 1)) {
    return fail(11);
  }

  return 0;
}
