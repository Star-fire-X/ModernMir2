#include <cassert>
#include <cstdint>
#include <optional>
#include <string>

#include "world/logic_runtime.hpp"

namespace {

mir2::CharacterRecord make_character() {
  mir2::CharacterRecord character;
  character.account_id = "acct";
  character.character_name = "Hero";
  character.map_id = "1";
  character.x = 10;
  character.y = 10;
  character.ability.hp = 15;
  character.ability.max_hp = 15;
  return character;
}

const mir2::PersistRequest* last_saved_character(const mir2::RuntimeDispatch& dispatch) {
  const mir2::PersistRequest* saved = nullptr;
  for (const auto& request : dispatch.persist_requests) {
    if (request.kind == mir2::PersistRequestKind::save_character &&
        request.character_name == "Hero") {
      saved = &request;
    }
  }
  return saved;
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 7;
  config.maps.push_back(mir2::MapConfig{"0", "BackMap", {}, 20, 20, 5, 5});
  mir2::MapConfig no_reconnect{"1", "NoReconnect", {}, 20, 20, 10, 10};
  no_reconnect.no_reconnect = true;
  no_reconnect.back_map = "0";
  config.maps.push_back(no_reconnect);

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  const auto character = make_character();
  mir2::LogicCommand enter;
  enter.kind = mir2::LogicCommandKind::enter_world;
  enter.session_id = 1;
  enter.account_id = character.account_id;
  enter.character_name = character.character_name;
  enter.map_id = character.map_id;
  enter.x = character.x;
  enter.y = character.y;
  enter.character = character;
  static_cast<void>(runtime.route_logic_command(enter));
  static_cast<void>(runtime.tick(1000));

  const auto dispatch = runtime.mark_session_disconnected(1, "logout");
  const auto* saved = last_saved_character(dispatch);
  assert(saved != nullptr);
  assert(saved->character.map_id == "0");
  assert(saved->character.x >= 0 && saved->character.x < 20);
  assert(saved->character.y >= 0 && saved->character.y < 20);
  return 0;
}
