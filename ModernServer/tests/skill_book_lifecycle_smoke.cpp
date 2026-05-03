#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

#undef assert
#define assert(expression)                                                        \
  do {                                                                            \
    if (!(expression)) {                                                          \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);      \
      std::abort();                                                               \
    }                                                                             \
  } while (false)

namespace {

mir2::MagicConfig make_magic(std::int32_t id, std::string name, std::int32_t job = 99,
                             std::int32_t need_level = 1) {
  mir2::MagicConfig magic;
  magic.id = id;
  magic.name = std::move(name);
  magic.legacy.legacy_present = true;
  magic.legacy.effect_type = 1;
  magic.legacy.effect = id;
  magic.legacy.spell = 4;
  magic.legacy.min_power = 8;
  magic.legacy.max_power = 8;
  magic.legacy.job = job;
  magic.legacy.need_level = {need_level, need_level, need_level, need_level};
  magic.legacy.max_train = {500, 1500, 3000, 3000};
  magic.legacy.max_train_level = 3;
  magic.legacy.delay_time = 600;
  return magic;
}

mir2::ItemConfig make_book(std::int32_t id, std::string name) {
  mir2::ItemConfig item;
  item.id = id;
  item.name = std::move(name);
  item.std_mode = 4;
  item.weight = 1;
  item.looks = id;
  return item;
}

mir2::HostConfig base_config(std::vector<mir2::MagicConfig> magics,
                             std::vector<mir2::ItemConfig> items) {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 1;
  config.budgets.tick_ms = 20;
  config.maps.push_back(mir2::MapConfig{"0", "BookMap", {}, 20, 20, 10, 10});
  config.magics = std::move(magics);
  config.items = std::move(items);
  return config;
}

mir2::CharacterRecord make_character(std::string name, std::int32_t level = 7,
                                     std::int32_t job = 0, bool learned = false) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = 10;
  character.y = 10;
  character.job = static_cast<std::uint8_t>(job);
  character.ability.level = static_cast<std::uint8_t>(level);
  character.ability.hp = 15;
  character.ability.max_hp = 15;
  character.ability.mp = 15;
  character.ability.max_mp = 15;
  character.ability.max_exp = 100;
  character.ability.max_weight = 30;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  character.bag_items[0].index = 100;
  character.bag_items[0].make_index = 5000;
  character.bag_items[0].dura = 1;
  character.bag_items[0].dura_max = 1;
  if (learned) {
    character.magics[0].magic_id = 1;
    character.magics[0].level = 0;
    character.magics[0].key = '\0';
    character.magics[0].cur_train = 0;
  }
  return character;
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

mir2::LogicCommand make_eat(std::uint64_t session_id, std::int32_t make_index,
                            std::string item_name) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::eat_item;
  command.session_id = session_id;
  command.item_make_index = make_index;
  command.text = std::move(item_name);
  command.game_message.ident = mir2::kCmEat;
  command.game_message.recog = make_index;
  return command;
}

std::vector<std::uint16_t> packet_idents(const mir2::RuntimeDispatch& dispatch) {
  std::vector<std::uint16_t> idents;
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value()) {
      idents.push_back(decoded->message.ident);
    }
  }
  return idents;
}

bool has_packet(const mir2::RuntimeDispatch& dispatch, std::uint16_t ident) {
  const auto idents = packet_idents(dispatch);
  return std::find(idents.begin(), idents.end(), ident) != idents.end();
}

std::optional<std::size_t> first_packet_index(const mir2::RuntimeDispatch& dispatch,
                                             std::uint16_t ident) {
  std::size_t index = 0;
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (!decoded.has_value()) {
      continue;
    }
    if (decoded->message.ident == ident) {
      return index;
    }
    ++index;
  }
  return std::nullopt;
}

mir2::RuntimeDispatch run_use_book(mir2::HostConfig config, mir2::CharacterRecord character,
                                   std::string item_name, std::uint64_t session_id = 701) {
  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  static_cast<void>(runtime.route_logic_command(make_enter(session_id, std::move(character))));
  static_cast<void>(runtime.tick());
  static_cast<void>(runtime.route_logic_command(make_eat(session_id, 5000, std::move(item_name))));
  return runtime.tick();
}

}  // namespace

int main() {
  {
    auto config = base_config({make_magic(1, "Fireball")}, {make_book(100, "Fireball")});
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    auto character = make_character("Learner");
    static_cast<void>(runtime.route_logic_command(make_enter(701, character)));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_eat(701, 5000, "Fireball")));
    const auto dispatch = runtime.tick();

    const auto add_magic_index = first_packet_index(dispatch, mir2::kSmAddMagic);
    const auto eat_ok_index = first_packet_index(dispatch, mir2::kSmEatOk);
    assert(add_magic_index.has_value());
    assert(eat_ok_index.has_value());
    assert(*add_magic_index < *eat_ok_index);
    assert(!has_packet(dispatch, mir2::kSmEatFail));

    const auto snapshot = runtime.snapshot_character_actor("Learner");
    assert(snapshot.has_value());
    assert(snapshot->magics[0].magic_id == 1);
    assert(snapshot->magics[0].level == 0);
    assert(snapshot->magics[0].key == '\0');
    assert(snapshot->magics[0].cur_train == 0);
    assert(mir2::is_empty(snapshot->bag_items[0]));
  }

  {
    const auto dispatch = run_use_book(
        base_config({make_magic(1, "Fireball")}, {make_book(100, "Fireball")}),
        make_character("Duplicate", 7, 0, true), "Fireball");
    assert(has_packet(dispatch, mir2::kSmEatFail));
    assert(!has_packet(dispatch, mir2::kSmAddMagic));
  }

  {
    const auto dispatch = run_use_book(
        base_config({make_magic(1, "Fireball", 1)}, {make_book(100, "Fireball")}),
        make_character("WrongJob", 7, 0), "Fireball");
    assert(has_packet(dispatch, mir2::kSmEatFail));
    assert(!has_packet(dispatch, mir2::kSmAddMagic));
  }

  {
    const auto dispatch = run_use_book(
        base_config({make_magic(1, "Fireball", 99, 7)}, {make_book(100, "Fireball")}),
        make_character("TooLow", 6, 0), "Fireball");
    assert(has_packet(dispatch, mir2::kSmEatFail));
    assert(!has_packet(dispatch, mir2::kSmAddMagic));
  }

  {
    const auto dispatch =
        run_use_book(base_config({make_magic(1, "Fireball")}, {make_book(100, "Unknown")}),
                     make_character("Unknown"), "Unknown");
    assert(has_packet(dispatch, mir2::kSmEatFail));
    assert(!has_packet(dispatch, mir2::kSmAddMagic));
  }

  {
    const auto dispatch = run_use_book(
        base_config({make_magic(34, "SourceOnly")}, {make_book(100, "SourceOnly")}),
        make_character("SourceOnly"), "SourceOnly");
    assert(has_packet(dispatch, mir2::kSmEatFail));
    assert(!has_packet(dispatch, mir2::kSmAddMagic));
  }

  return 0;
}
