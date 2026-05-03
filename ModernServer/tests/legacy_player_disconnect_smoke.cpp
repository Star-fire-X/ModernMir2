#include <cassert>
#include <string>
#include <string_view>

#include "world/logic_runtime.hpp"

namespace {

mir2::CharacterRecord make_character(std::string name = "Hero") {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = 10;
  character.y = 10;
  character.ability.hp = 15;
  character.ability.max_hp = 15;
  return character;
}

mir2::LegacyReadyUser make_ready(std::uint64_t session_id, mir2::CharacterRecord character,
                                 bool fast = true) {
  mir2::LegacyReadyUser ready;
  ready.session_id = session_id;
  ready.account_id = character.account_id;
  ready.character_name = character.character_name;
  ready.map_id = character.map_id;
  ready.x = character.x;
  ready.y = character.y;
  ready.character = std::move(character);
  ready.fast_initialize = fast;
  return ready;
}

mir2::LogicCommand make_walk(std::uint64_t session_id, int x, int y) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::walk;
  command.session_id = session_id;
  command.x = x;
  command.y = y;
  return command;
}

bool has_force_disconnect(const mir2::RuntimeDispatch& dispatch, std::uint64_t session_id) {
  for (const auto& event : dispatch.session_events) {
    if (event.session_id == session_id &&
        event.kind == mir2::SessionEventKind::force_disconnect) {
      return true;
    }
  }
  return false;
}

bool has_save_character(const mir2::RuntimeDispatch& dispatch, std::string_view name) {
  for (const auto& request : dispatch.persist_requests) {
    if (request.kind == mir2::PersistRequestKind::save_character &&
        request.character_name == name) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "DisconnectMap", {}, 0, 0, 10, 10});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  static_cast<void>(runtime.enqueue_ready_user(make_ready(41, make_character())));
  static_cast<void>(runtime.tick(1000));
  assert(runtime.legacy_session_state(41) == mir2::LegacyPlayerState::running);

  static_cast<void>(runtime.mark_session_disconnected(41, "socket_closed"));
  assert(runtime.legacy_session_state(41) == mir2::LegacyPlayerState::ghost);
  static_cast<void>(runtime.route_logic_command(make_walk(41, 11, 10)));
  assert(runtime.legacy_session_inbox_size(41) == 0);

  const auto close_dispatch = runtime.tick(1251);
  assert(!runtime.legacy_session_state(41).has_value());
  assert(runtime.online_session_count() == 0);
  assert(runtime.legacy_close_record_count() == 1);
  assert(has_save_character(close_dispatch, "Hero"));
  assert(has_force_disconnect(close_dispatch, 41));
  assert(!runtime.snapshot_character_actor("Hero").has_value());

  const auto duplicate_dispatch =
      runtime.enqueue_ready_user(make_ready(42, make_character(), false));
  assert(has_force_disconnect(duplicate_dispatch, 42));
  assert(runtime.legacy_ready_count() == 0);

  static_cast<void>(runtime.tick(1251 + 5ULL * 60ULL * 1000ULL));
  assert(runtime.legacy_close_record_count() == 0);

  return 0;
}
