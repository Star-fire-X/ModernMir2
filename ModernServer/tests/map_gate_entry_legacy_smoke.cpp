#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

bool has_packet(const mir2::RuntimeDispatch& dispatch, std::uint64_t session_id,
                std::uint16_t ident) {
  for (const auto& event : dispatch.session_events) {
    if (event.session_id != session_id) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return true;
    }
  }
  return false;
}

mir2::CharacterRecord make_character(std::uint8_t level = 10) {
  mir2::CharacterRecord character;
  character.account_id = "acct";
  character.character_name = "Hero";
  character.map_id = "0";
  character.x = 10;
  character.y = 10;
  character.ability.level = level;
  character.ability.hp = 15;
  character.ability.max_hp = 15;
  return character;
}

mir2::HostConfig make_config(mir2::MapConfig target) {
  mir2::HostConfig config;
  config.budgets.tick_ms = 20;
  mir2::MapConfig source;
  source.id = "0";
  source.title = "Source";
  source.width = 20;
  source.height = 20;
  source.home_x = 10;
  source.home_y = 10;
  source.gates.push_back(mir2::MapGateConfig{11, 10, target.id, 5, 5, false});
  config.maps.push_back(source);
  config.maps.push_back(std::move(target));
  return config;
}

struct GateAttempt {
  mir2::RuntimeDispatch dispatch{};
  std::optional<mir2::CharacterRecord> snapshot{};
};

GateAttempt try_gate(mir2::HostConfig config, mir2::CharacterRecord character,
                     std::optional<mir2::LegacyEventRecord> event = std::nullopt) {
  mir2::LogicRuntime runtime(std::move(config));
  runtime.initialize();
  if (event.has_value()) {
    static_cast<void>(runtime.enqueue_legacy_event(*event));
  }

  mir2::LogicCommand enter;
  enter.kind = mir2::LogicCommandKind::enter_world;
  enter.session_id = 1;
  enter.account_id = character.account_id;
  enter.character_name = character.character_name;
  enter.map_id = character.map_id;
  enter.x = character.x;
  enter.y = character.y;
  enter.character = std::move(character);
  static_cast<void>(runtime.route_logic_command(enter));
  static_cast<void>(runtime.tick(1000));
  for (int index = 0; index < 12; ++index) {
    static_cast<void>(runtime.tick(1020 + static_cast<std::uint64_t>(index) * 20ULL));
  }

  mir2::LogicCommand walk;
  walk.kind = mir2::LogicCommandKind::walk;
  walk.session_id = 1;
  walk.x = 11;
  walk.y = 10;
  static_cast<void>(runtime.route_logic_command(walk));
  auto dispatch = runtime.tick(1300);
  return GateAttempt{std::move(dispatch), runtime.snapshot_character_actor("Hero")};
}

void assert_rejected(const GateAttempt& attempt) {
  assert(!has_packet(attempt.dispatch, 1, mir2::kSmClearObjects));
  assert(!has_packet(attempt.dispatch, 1, mir2::kSmChangeMap));
  assert(attempt.snapshot.has_value());
  assert(attempt.snapshot->map_id == "0");
  assert(attempt.snapshot->x == 11 && attempt.snapshot->y == 10);
}

void assert_transferred(const GateAttempt& attempt) {
  assert(has_packet(attempt.dispatch, 1, mir2::kSmClearObjects));
  assert(has_packet(attempt.dispatch, 1, mir2::kSmChangeMap));
  assert(attempt.snapshot.has_value());
  assert(attempt.snapshot->map_id == "1");
  assert(attempt.snapshot->x == 5 && attempt.snapshot->y == 5);
}

}  // namespace

int main() {
  {
    mir2::MapConfig target{"1", "LevelGate", {}, 20, 20, 5, 5};
    target.need_level = 10;
    const auto attempt = try_gate(make_config(target), make_character(1));
    assert_rejected(attempt);
  }

  {
    mir2::MapConfig target{"1", "QuestGate", {}, 20, 20, 5, 5};
    target.need_set_number = 1;
    target.need_set_value = 1;
    mir2::MapEntryQuestConfig quest;
    quest.qfile = "entry.txt";
    quest.dialog_sections.push_back({"@main", "#ACT\nSET [1] 1\n"});
    target.check_quest = std::move(quest);
    const auto attempt = try_gate(make_config(target), make_character());
    assert_transferred(attempt);
    assert((attempt.snapshot->quest_marks[0] & 0x80U) != 0U);
  }

  {
    mir2::MapConfig target{"1", "NeedSetGate", {}, 20, 20, 5, 5};
    target.need_set_number = 2;
    target.need_set_value = 1;
    const auto attempt = try_gate(make_config(target), make_character());
    assert_rejected(attempt);
  }

  {
    mir2::MapConfig target{"1", "NeedHoleGate", {}, 20, 20, 5, 5};
    target.need_hole = true;
    const auto attempt = try_gate(make_config(target), make_character());
    assert_rejected(attempt);
  }

  {
    mir2::MapConfig target{"1", "NeedHoleQuestGate", {}, 20, 20, 5, 5};
    target.need_hole = true;
    target.need_set_number = 1;
    target.need_set_value = 1;
    mir2::MapEntryQuestConfig quest;
    quest.qfile = "entry.txt";
    quest.dialog_sections.push_back({"@main", "#ACT\nSET [1] 1\n"});
    target.check_quest = std::move(quest);
    const auto attempt = try_gate(make_config(target), make_character());
    assert_rejected(attempt);
    assert((attempt.snapshot->quest_marks[0] & 0x80U) == 0U);
  }

  {
    mir2::MapConfig target{"1", "NeedHoleGate", {}, 20, 20, 5, 5};
    target.need_hole = true;
    mir2::LegacyEventRecord event;
    event.map_id = "0";
    event.x = 11;
    event.y = 10;
    event.type = mir2::LegacyEventType::digout_zombi;
    event.continue_ms = 5ULL * 60ULL * 1000ULL;
    event.blocks_walk = false;
    const auto attempt = try_gate(make_config(target), make_character(), event);
    assert_transferred(attempt);
  }

  {
    mir2::MapConfig target{"1", "NeedHoleQuestGate", {}, 20, 20, 5, 5};
    target.need_hole = true;
    target.need_set_number = 1;
    target.need_set_value = 1;
    mir2::MapEntryQuestConfig quest;
    quest.qfile = "entry.txt";
    quest.dialog_sections.push_back({"@main", "#ACT\nSET [1] 1\n"});
    target.check_quest = std::move(quest);
    mir2::LegacyEventRecord event;
    event.map_id = "0";
    event.x = 11;
    event.y = 10;
    event.type = mir2::LegacyEventType::digout_zombi;
    event.continue_ms = 5ULL * 60ULL * 1000ULL;
    event.blocks_walk = false;
    const auto attempt = try_gate(make_config(target), make_character(), event);
    assert_transferred(attempt);
    assert((attempt.snapshot->quest_marks[0] & 0x80U) != 0U);
  }

  return 0;
}
