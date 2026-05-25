#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/legacy_random.hpp"
#include "world/map_actor.hpp"

namespace {

std::optional<mir2::DecodedLegacyGamePacket> find_packet_by_recog(
    const mir2::RuntimeDispatch& dispatch, std::uint16_t ident, std::int32_t recog) {
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident &&
        decoded->message.recog == recog) {
      return decoded;
    }
  }
  return std::nullopt;
}

std::filesystem::path write_test_map(
    const std::string& name, int width, int height,
    const std::vector<std::pair<int, int>>& blocked) {
  const auto path = std::filesystem::temp_directory_path() / name;
  std::vector<std::uint8_t> bytes(52U + static_cast<std::size_t>(width) *
                                            static_cast<std::size_t>(height) * 12U);
  bytes[0] = static_cast<std::uint8_t>(width & 0xff);
  bytes[1] = static_cast<std::uint8_t>((width >> 8) & 0xff);
  bytes[2] = static_cast<std::uint8_t>(height & 0xff);
  bytes[3] = static_cast<std::uint8_t>((height >> 8) & 0xff);
  for (const auto& [x, y] : blocked) {
    const auto offset = 52U +
        (static_cast<std::size_t>(x) * static_cast<std::size_t>(height) +
         static_cast<std::size_t>(y)) *
            12U;
    bytes[offset + 4U] = 0x00U;
    bytes[offset + 5U] = 0x80U;
  }
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  return path;
}

mir2::ActorMail make_monster(std::uint64_t actor_id, std::int32_t x, std::int32_t y,
                             std::uint8_t dir = 2) {
  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::spawn_monster;
  mail.map_id = "0";
  mail.actor_id = actor_id;
  mail.name = "Oma";
  mail.x = x;
  mail.y = y;
  mail.dir = dir;
  mail.max_hp = 20;
  mail.attack_power = 3;
  mail.dc_min = 3;
  mail.dc_max = 3;
  mail.walk_speed_ms = 200;
  mail.walk_step = 1;
  mail.monster_ai_profile = mir2::MonsterAiProfile::basic;
  mail.legacy_spawn_group = true;
  return mail;
}

mir2::ActorMail make_chasing_monster(std::uint64_t actor_id, std::int32_t x,
                                     std::int32_t y, std::uint64_t target_actor_id) {
  auto mail = make_monster(actor_id, x, y);
  mail.target_actor_id = target_actor_id;
  return mail;
}

mir2::ActorMail make_player(std::uint64_t actor_id, std::uint64_t session_id,
                            std::int32_t x, std::int32_t y) {
  mir2::CharacterRecord hero;
  hero.account_id = "acct" + std::to_string(actor_id);
  hero.character_name = "Hero" + std::to_string(actor_id);
  hero.map_id = "0";
  hero.x = x;
  hero.y = y;
  hero.ability.level = 1;
  hero.ability.hp = 40;
  hero.ability.max_hp = 40;
  hero.ability.max_exp = 100;

  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::spawn_player;
  mail.map_id = "0";
  mail.actor_id = actor_id;
  mail.session_id = session_id;
  mail.x = x;
  mail.y = y;
  mail.character = hero;
  return mail;
}

mir2::MapActor make_map(const std::filesystem::path& source_map = {}) {
  mir2::LogicBudgetConfig budgets;
  budgets.tick_ms = 20;
  return mir2::MapActor(mir2::MapConfig{"0", "MovementLegacy", source_map, 0, 0, 30, 30},
                        budgets, {}, {});
}

void spawn(mir2::MapActor& map, const std::vector<mir2::ActorMail>& mails) {
  for (const auto& mail : mails) {
    map.enqueue_mail(mail);
  }
  static_cast<void>(map.tick(1, 0));
}

}  // namespace

int main() {
  {
    auto map = make_map();
    spawn(map, {make_chasing_monster(100, 10, 10, 200),
                make_player(200, 20, 12, 10)});

    const auto dispatch = map.legacy_process_monster(100, 2, 251, 0, 0);
    const auto walk = find_packet_by_recog(dispatch, mir2::kSmWalk, 100);
    assert(walk.has_value());
    assert(walk->message.param == 11);
    assert(walk->message.tag == 10);
    assert(walk->message.series == 2);
    const auto snapshot = map.legacy_monster_snapshot(100);
    assert(snapshot.has_value());
    assert(snapshot->x == 11);
    assert(snapshot->y == 10);
    assert(snapshot->walk_time_ms == 251);
  }

  {
    const auto blocked_map = write_test_map(
        "mir2_monster_movement_static_block.map", 30, 30,
        {{9, 9}, {10, 9}, {11, 9}, {9, 10}, {11, 10}, {9, 11}, {10, 11}, {11, 11}});
    auto map = make_map(blocked_map);
    auto blocked = make_chasing_monster(101, 10, 10, 201);
    blocked.dir = 4;
    spawn(map, {blocked,
                make_player(201, 21, 12, 10),
                make_player(211, 31, 14, 14)});

    const auto dispatch = map.legacy_process_monster(101, 2, 251, 0, 0);
    assert(!find_packet_by_recog(dispatch, mir2::kSmWalk, 101).has_value());
    const auto snapshot = map.legacy_monster_snapshot(101);
    assert(snapshot.has_value());
    assert(snapshot->x == 10);
    assert(snapshot->y == 10);
    assert(snapshot->dir != 4);
    assert(snapshot->walk_time_ms == 251);
  }

  {
    const auto blocked_map = write_test_map(
        "mir2_monster_movement_occupied_block.map", 30, 30,
        {{9, 9}, {10, 9}, {11, 9}, {9, 10}, {9, 11}, {10, 11}, {11, 11}});
    auto map = make_map(blocked_map);
    spawn(map, {make_chasing_monster(102, 10, 10, 203),
                make_player(202, 22, 11, 10),
                make_player(203, 23, 12, 10)});

    const auto dispatch = map.legacy_process_monster(102, 2, 251, 0, 0);
    assert(!find_packet_by_recog(dispatch, mir2::kSmWalk, 102).has_value());
    const auto snapshot = map.legacy_monster_snapshot(102);
    assert(snapshot.has_value());
    assert(snapshot->x == 10);
    assert(snapshot->y == 10);
    assert(snapshot->walk_time_ms == 251);
  }

  {
    auto map = make_map();
    spawn(map, {make_monster(103, 10, 10, 2), make_player(204, 24, 12, 10)});
    mir2::LegacyRandom random(2);
    map.set_legacy_random(&random);

    const auto dispatch = map.legacy_process_monster(103, 2, 251, 0, 0);
    assert(!find_packet_by_recog(dispatch, mir2::kSmWalk, 103).has_value());
    const auto snapshot = map.legacy_monster_snapshot(103);
    assert(snapshot.has_value());
    assert(snapshot->x == 10);
    assert(snapshot->y == 10);
    assert(snapshot->walk_time_ms == 251);
  }

  {
    auto map = make_map();
    spawn(map, {make_monster(104, 10, 10, 2), make_player(205, 25, 12, 10)});
    mir2::LegacyRandom random(1);
    map.set_legacy_random(&random);

    const auto dispatch = map.legacy_process_monster(104, 2, 251, 0, 0);
    const auto walk = find_packet_by_recog(dispatch, mir2::kSmWalk, 104);
    assert(walk.has_value());
    assert(walk->message.param == 11);
    assert(walk->message.tag == 10);
    assert(walk->message.series == 2);
    const auto snapshot = map.legacy_monster_snapshot(104);
    assert(snapshot.has_value());
    assert(snapshot->x == 11);
    assert(snapshot->y == 10);
    assert(snapshot->walk_time_ms == 251);
  }

  return 0;
}
