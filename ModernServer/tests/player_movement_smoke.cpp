#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "shared/legacy/movement_rules.hpp"
#include "world/logic_runtime.hpp"

namespace {

void write_u16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
  bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

std::filesystem::path write_test_map(
    int width, int height, const std::vector<std::pair<int, int>>& blocked) {
  const auto path = std::filesystem::temp_directory_path() / "mir2_player_movement_smoke.map";
  std::vector<std::uint8_t> bytes(52U + static_cast<std::size_t>(width) *
                                            static_cast<std::size_t>(height) * 12U);
  write_u16(bytes, 0, static_cast<std::uint16_t>(width));
  write_u16(bytes, 2, static_cast<std::uint16_t>(height));
  for (const auto& [x, y] : blocked) {
    const auto offset = 52U +
        (static_cast<std::size_t>(x) * static_cast<std::size_t>(height) +
         static_cast<std::size_t>(y)) *
            12U;
    write_u16(bytes, offset + 4U, 0x8000U);
  }
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return path;
}

mir2::CharacterRecord make_character(std::string name, int x, int y, std::uint16_t hp = 15) {
  mir2::CharacterRecord hero;
  hero.account_id = "acct_" + name;
  hero.character_name = std::move(name);
  hero.map_id = "0";
  hero.x = x;
  hero.y = y;
  hero.dir = mir2::legacy::kDirDown;
  hero.feature = 0x01020304;
  hero.status = 0x05060708;
  hero.ability.hp = hp;
  hero.ability.max_hp = 15;
  return hero;
}

void enter(mir2::LogicRuntime& runtime, std::uint64_t session_id,
           const mir2::CharacterRecord& character) {
  mir2::LogicCommand enter;
  enter.kind = mir2::LogicCommandKind::enter_world;
  enter.session_id = session_id;
  enter.account_id = character.account_id;
  enter.character_name = character.character_name;
  enter.map_id = character.map_id;
  enter.x = character.x;
  enter.y = character.y;
  enter.character = character;
  static_cast<void>(runtime.route_logic_command(enter));
  static_cast<void>(runtime.tick());
}

mir2::RuntimeDispatch move(mir2::LogicRuntime& runtime, std::uint64_t session_id,
                           mir2::LogicCommandKind kind, int x, int y) {
  mir2::LogicCommand command;
  command.kind = kind;
  command.session_id = session_id;
  command.x = x;
  command.y = y;
  static_cast<void>(runtime.route_logic_command(command));
  return runtime.tick();
}

void advance(mir2::LogicRuntime& runtime, int ticks) {
  for (int i = 0; i < ticks; ++i) {
    static_cast<void>(runtime.tick());
  }
}

mir2::CharacterRecord snapshot(mir2::LogicRuntime& runtime, std::string_view name) {
  const auto record = runtime.snapshot_character_actor(name);
  assert(record.has_value());
  return *record;
}

bool has_packet_ident(const mir2::RuntimeDispatch& dispatch, std::uint64_t session_id,
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

bool has_raw_text(const mir2::RuntimeDispatch& dispatch, std::uint64_t session_id,
                  std::string_view text) {
  for (const auto& event : dispatch.session_events) {
    if (event.session_id != session_id) {
      continue;
    }
    const auto body =
        std::string(event.packet.body.begin(), event.packet.body.end());
    if (body.find(text) != std::string::npos) {
      return true;
    }
  }
  return false;
}

void assert_move_fail_packet(const mir2::RuntimeDispatch& dispatch, std::uint64_t session_id,
                             const mir2::CharacterRecord& character) {
  assert(has_raw_text(dispatch, session_id, "+FAIL/"));
  const auto decoded = find_packet(dispatch, session_id, mir2::kSmMoveFail);
  assert(decoded.has_value());
  assert(decoded->message.recog > 0);
  assert(decoded->message.param == static_cast<std::uint16_t>(character.x));
  assert(decoded->message.tag == static_cast<std::uint16_t>(character.y));
  assert(decoded->message.series == character.dir);
  mir2::LegacyCharDesc desc;
  assert(mir2::legacy_decode_buffer(decoded->body, &desc, sizeof(desc)));
  assert(desc.feature == character.feature);
  assert(desc.status == character.status);
}

}  // namespace

int main() {
  assert(mir2::legacy::next_direction(5, 5, 5, 4) == mir2::legacy::kDirUp);
  assert(mir2::legacy::next_direction(5, 5, 8, 8) == mir2::legacy::kDirDownRight);
  assert(mir2::legacy::requested_walk_target(10, 10, 5, 5, 9, 6)->x == 6);
  assert(mir2::legacy::requested_run_target(10, 10, 5, 5, 3, 5)->x == 3);

  const auto map_path = write_test_map(10, 10, {{5, 3}, {4, 6}, {6, 5}});
  mir2::HostConfig config;
  config.budgets.tick_ms = 20;
  config.maps.push_back(mir2::MapConfig{"0", "MovementMap", map_path, 0, 0, 1, 1});
  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  enter(runtime, 7, make_character("Hero", 3, 3));
  enter(runtime, 8, make_character("Watcher", 8, 8));

  auto dispatch = move(runtime, 7, mir2::LogicCommandKind::walk, 4, 3);
  auto hero = snapshot(runtime, "Hero");
  assert(hero.x == 4 && hero.y == 3);
  assert(has_packet_ident(dispatch, 8, mir2::kSmWalk));

  advance(runtime, 12);
  dispatch = move(runtime, 7, mir2::LogicCommandKind::walk, 5, 3);
  hero = snapshot(runtime, "Hero");
  assert(hero.x == 4 && hero.y == 3);
  assert_move_fail_packet(dispatch, 7, hero);

  advance(runtime, 12);
  dispatch = move(runtime, 7, mir2::LogicCommandKind::run, 4, 5);
  hero = snapshot(runtime, "Hero");
  assert(hero.x == 4 && hero.y == 5);
  assert(has_packet_ident(dispatch, 8, mir2::kSmRun));

  advance(runtime, 12);
  dispatch = move(runtime, 7, mir2::LogicCommandKind::run, 4, 7);
  hero = snapshot(runtime, "Hero");
  assert(hero.x == 4 && hero.y == 5);
  assert_move_fail_packet(dispatch, 7, hero);

  advance(runtime, 12);
  dispatch = move(runtime, 7, mir2::LogicCommandKind::run, 6, 5);
  hero = snapshot(runtime, "Hero");
  assert(hero.x == 4 && hero.y == 5);
  assert_move_fail_packet(dispatch, 7, hero);

  advance(runtime, 12);
  dispatch = move(runtime, 7, mir2::LogicCommandKind::walk, 9, 9);
  hero = snapshot(runtime, "Hero");
  assert(hero.x == 4 && hero.y == 5);
  assert_move_fail_packet(dispatch, 7, hero);

  enter(runtime, 11, make_character("Blocker", 5, 5));
  advance(runtime, 12);
  dispatch = move(runtime, 7, mir2::LogicCommandKind::walk, 5, 5);
  hero = snapshot(runtime, "Hero");
  assert(hero.x == 4 && hero.y == 5);
  assert_move_fail_packet(dispatch, 7, hero);

  enter(runtime, 9, make_character("LowHp", 1, 8, 9));
  dispatch = move(runtime, 9, mir2::LogicCommandKind::run, 3, 8);
  auto low_hp = snapshot(runtime, "LowHp");
  assert(low_hp.x == 1 && low_hp.y == 8);
  assert_move_fail_packet(dispatch, 9, low_hp);

  enter(runtime, 12, make_character("Dead", 1, 6, 0));
  dispatch = move(runtime, 12, mir2::LogicCommandKind::walk, 2, 6);
  auto dead = snapshot(runtime, "Dead");
  assert(dead.x == 1 && dead.y == 6);
  assert_move_fail_packet(dispatch, 12, dead);

  enter(runtime, 10, make_character("Sprinter", 1, 1));
  for (int x = 2; x <= 5; ++x) {
    static_cast<void>(move(runtime, 10, mir2::LogicCommandKind::walk, x, 1));
    advance(runtime, 12);
  }
  static_cast<void>(move(runtime, 10, mir2::LogicCommandKind::walk, 6, 1));
  const auto sprinter = snapshot(runtime, "Sprinter");
  assert(sprinter.x == 5 && sprinter.y == 1);

  return 0;
}
