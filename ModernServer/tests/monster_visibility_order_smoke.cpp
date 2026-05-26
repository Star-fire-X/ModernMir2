#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

#undef assert
#define assert(expr)                                                           \
  do {                                                                         \
    if (!(expr)) {                                                             \
      std::cerr << "Assertion failed: " #expr ", file " << __FILE__           \
                << ", line " << __LINE__ << '\n';                             \
      std::exit(1);                                                            \
    }                                                                          \
  } while (false)

namespace {

std::vector<mir2::LegacyRuntimeTrace> spawn_traces(const mir2::RuntimeDispatch& dispatch) {
  std::vector<mir2::LegacyRuntimeTrace> traces;
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == "MonsterSpawn" && trace.action == "spawned") {
      traces.push_back(trace);
    }
  }
  return traces;
}

std::optional<std::size_t> packet_index(const mir2::RuntimeDispatch& dispatch,
                                        std::uint64_t session_id,
                                        std::uint16_t ident,
                                        std::optional<std::int32_t> recog = std::nullopt) {
  for (std::size_t index = 0; index < dispatch.session_events.size(); ++index) {
    const auto& event = dispatch.session_events[index];
    if (event.session_id != session_id) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (!decoded.has_value() || decoded->message.ident != ident) {
      continue;
    }
    if (recog.has_value() && decoded->message.recog != *recog) {
      continue;
    }
    return index;
  }
  return std::nullopt;
}

mir2::CharacterRecord make_character(std::string name, std::int32_t x, std::int32_t y) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = x;
  character.y = y;
  character.ability.level = 1;
  character.ability.hp = 50;
  character.ability.max_hp = 50;
  character.ability.max_weight = 100;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  return character;
}

mir2::LogicCommand enter_command(std::uint64_t session_id, mir2::CharacterRecord character) {
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

mir2::RuntimeDispatch enter(mir2::LogicRuntime& runtime, std::uint64_t session_id,
                            mir2::CharacterRecord character, std::uint64_t now_ms) {
  static_cast<void>(runtime.route_logic_command(enter_command(session_id, std::move(character))));
  return runtime.tick(now_ms);
}

void advance(mir2::LogicRuntime& runtime, std::uint64_t& now_ms, int ticks = 12) {
  for (int index = 0; index < ticks; ++index) {
    now_ms += 20;
    static_cast<void>(runtime.tick(now_ms));
  }
}

mir2::LogicCommand drop_command(std::uint64_t session_id, std::int32_t make_index,
                                std::string name) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::drop_item;
  command.session_id = session_id;
  command.item_make_index = make_index;
  command.text = std::move(name);
  return command;
}

mir2::MonsterDefConfig monster_def(std::string name) {
  mir2::MonsterDefConfig monster;
  monster.name = std::move(name);
  monster.hp = 20;
  monster.dc = 1;
  monster.dc_max = 1;
  monster.walk_speed_ms = 200;
  monster.attack_speed_ms = 200;
  monster.ai_profile = mir2::MonsterAiProfile::basic;
  return monster;
}

mir2::SpawnConfig monster_spawn(std::string name, std::int32_t x, std::int32_t y) {
  mir2::SpawnConfig spawn;
  spawn.map_id = "0";
  spawn.actor_type = "monster";
  spawn.name = std::move(name);
  spawn.x = x;
  spawn.y = y;
  spawn.area = 0;
  spawn.count = 1;
  spawn.zen_time_ms = 200;
  spawn.legacy_group = true;
  return spawn;
}

}  // namespace

int main() {
  {
    mir2::HostConfig config;
    config.runtime.legacy_random_seed = 17;
    config.budgets.tick_ms = 20;
    config.maps.push_back(mir2::MapConfig{"0", "MonsterShowOrder", {}, 0, 0, 40, 40});
    config.monsters.push_back(monster_def("RightMob"));
    config.monsters.push_back(monster_def("LeftMob"));
    config.spawns.push_back(monster_spawn("RightMob", 12, 10));
    config.spawns.push_back(monster_spawn("LeftMob", 9, 10));

    mir2::LogicRuntime runtime(config);
    runtime.initialize();

    const auto first = runtime.tick(1000);
    assert(spawn_traces(first).empty());
    const auto right_spawn = spawn_traces(runtime.tick(1201));
    assert(right_spawn.size() == 1);
    const auto left_spawn = spawn_traces(runtime.tick(1402));
    assert(left_spawn.size() == 1);
    const auto right_id = static_cast<std::int32_t>(right_spawn.front().actor_id);
    const auto left_id = static_cast<std::int32_t>(left_spawn.front().actor_id);

    const auto login = enter(runtime, 7, make_character("Watcher", 10, 10), 1422);
    const auto left_index = packet_index(login, 7, mir2::kSmTurn, left_id);
    const auto right_index = packet_index(login, 7, mir2::kSmTurn, right_id);
    assert(left_index.has_value());
    assert(right_index.has_value());
    assert(*left_index < *right_index);
  }

  {
    mir2::HostConfig config;
    config.budgets.tick_ms = 20;
    config.maps.push_back(mir2::MapConfig{"0", "ActorBeforeItemShow", {}, 0, 0, 40, 40});
    config.items.push_back(mir2::ItemConfig{1, "Wooden Sword", 3, 100, 5, 1, 1, 1000, 1, 0, 0});

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    auto dropper = make_character("Dropper", 10, 10);
    dropper.bag_items[0] = mir2::LegacyUserItem{1001, 1, 600, 1000};
    std::uint64_t now_ms = 1000;
    static_cast<void>(enter(runtime, 11, dropper, now_ms));
    advance(runtime, now_ms);

    static_cast<void>(runtime.route_logic_command(drop_command(11, 1001, "Wooden Sword")));
    now_ms += 20;
    const auto drop = runtime.tick(now_ms);
    assert(packet_index(drop, 11, mir2::kSmItemShow).has_value());

    now_ms += 20;
    const auto login = enter(runtime, 12, make_character("Watcher", 11, 10), now_ms);
    const auto actor_show = packet_index(login, 12, mir2::kSmTurn);
    const auto item_show = packet_index(login, 12, mir2::kSmItemShow);
    assert(actor_show.has_value());
    assert(item_show.has_value());
    assert(*actor_show < *item_show);
  }

  return 0;
}
