#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "protocol/legacy_game_codec.hpp"
#include "world/legacy_random.hpp"
#include "world/map_actor.hpp"

namespace {

void write_u16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value & 0xffU);
  bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
}

std::filesystem::path write_mine_map() {
  constexpr int width = 4;
  constexpr int height = 4;
  const auto path = std::filesystem::temp_directory_path() / "mir2_stone_mine_legacy.map";
  std::vector<std::uint8_t> bytes(52U + static_cast<std::size_t>(width) *
                                            static_cast<std::size_t>(height) * 12U);
  write_u16(bytes, 0, width);
  write_u16(bytes, 2, height);
  for (const auto& [x, y] : std::vector<std::pair<int, int>>{{1, 1}, {2, 2}}) {
    const auto offset = 52U +
        (static_cast<std::size_t>(x) * static_cast<std::size_t>(height) +
         static_cast<std::size_t>(y)) * 12U;
    write_u16(bytes, offset, 0x8000U);
  }
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  return path;
}

mir2::ActorMail make_spawn_player() {
  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::spawn_player;
  mail.actor_id = 1;
  mail.session_id = 1;
  mail.map_id = "0";
  mail.x = 0;
  mail.y = 0;
  mail.character.account_id = "acct";
  mail.character.character_name = "Hero";
  mail.character.map_id = "0";
  mail.character.x = 0;
  mail.character.y = 0;
  mail.character.ability.hp = 20;
  mail.character.ability.max_hp = 20;
  return mail;
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

}  // namespace

int main() {
  mir2::MapConfig map;
  map.id = "0";
  map.title = "MineMap";
  map.source_map = write_mine_map();
  map.mine_map = 1;

  mir2::MapActor actor(map, mir2::LogicBudgetConfig{}, {}, {});
  mir2::LegacyRandom random(1);
  actor.set_legacy_random(&random);
  assert(actor.legacy_stone_mine_count() == 2);

  const auto first = actor.legacy_stone_mine_snapshot(1, 1);
  const auto second = actor.legacy_stone_mine_snapshot(2, 2);
  assert(first.has_value());
  assert(second.has_value());
  assert(first->type == 1);
  assert(second->type == 1);

  const auto first_count = first->mine_count;
  if (first_count > 0) {
    assert(actor.legacy_try_mine(1, 1));
    const auto mined = actor.legacy_stone_mine_snapshot(1, 1);
    assert(mined.has_value());
    assert(mined->mine_count == first_count - 1);
  } else {
    assert(!actor.legacy_try_mine(1, 1));
  }

  actor.refill_legacy_stone_mines(1000);
  const auto armed = actor.legacy_stone_mine_snapshot(1, 1);
  assert(armed.has_value());
  assert(armed->refill_time_ms == 1000);

  while (actor.legacy_try_mine(1, 1)) {
  }
  actor.refill_legacy_stone_mines(1000 + 10ULL * 60ULL * 1000ULL);
  const auto at_boundary = actor.legacy_stone_mine_snapshot(1, 1);
  assert(at_boundary.has_value());
  assert(at_boundary->mine_count == 0);

  actor.refill_legacy_stone_mines(1000 + 10ULL * 60ULL * 1000ULL + 1ULL);
  const auto refilled = actor.legacy_stone_mine_snapshot(1, 1);
  assert(refilled.has_value());
  assert(refilled->mine_count == refilled->mine_fill_count);

  auto dispatch = actor.legacy_spawn_player(make_spawn_player(), 1, 0, true);
  assert(!find_packet(dispatch, 1, mir2::kSmShowEvent).has_value());
  return 0;
}
