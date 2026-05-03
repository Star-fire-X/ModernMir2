#include <cassert>
#include <string>
#include <string_view>
#include <vector>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

mir2::CharacterRecord make_character(std::string name, int x = 10, int y = 10) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = x;
  character.y = y;
  character.ability.hp = 15;
  character.ability.max_hp = 15;
  return character;
}

mir2::LegacyReadyUser make_ready(std::uint64_t session_id, mir2::CharacterRecord character) {
  mir2::LegacyReadyUser ready;
  ready.session_id = session_id;
  ready.account_id = character.account_id;
  ready.character_name = character.character_name;
  ready.map_id = character.map_id;
  ready.x = character.x;
  ready.y = character.y;
  ready.character = std::move(character);
  return ready;
}

bool has_packet_ident(const mir2::RuntimeDispatch& dispatch, std::uint16_t ident) {
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> audit_messages(const mir2::RuntimeDispatch& dispatch,
                                        std::string_view category) {
  std::vector<std::string> messages;
  for (const auto& audit : dispatch.audit_events) {
    if (audit.category == category) {
      messages.push_back(audit.message);
    }
  }
  return messages;
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "LifecycleMap", {}, 0, 0, 10, 10});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  auto ready_dispatch = runtime.enqueue_ready_user(make_ready(11, make_character("Hero")));
  assert(ready_dispatch.session_events.empty());
  assert(runtime.legacy_ready_count() == 1);
  assert(runtime.legacy_run_user_count() == 0);
  assert(!runtime.legacy_session_state(11).has_value());

  const auto notice_dispatch = runtime.tick(1000);
  assert(runtime.legacy_ready_count() == 0);
  assert(runtime.legacy_run_user_count() == 1);
  assert(runtime.online_session_count() == 1);
  assert(runtime.legacy_session_state(11) == mir2::LegacyPlayerState::initialize_pending);
  assert(!has_packet_ident(notice_dispatch, mir2::kSmLogon));

  const auto initialize_dispatch = runtime.tick(1251);
  assert(runtime.legacy_session_state(11) == mir2::LegacyPlayerState::running);
  assert(has_packet_ident(initialize_dispatch, mir2::kSmNewMap));
  assert(has_packet_ident(initialize_dispatch, mir2::kSmLogon));
  assert(has_packet_ident(initialize_dispatch, mir2::kSmAbility));

  const auto operate_dispatch = runtime.tick(1502);
  assert(runtime.legacy_session_state(11) == mir2::LegacyPlayerState::running);
  assert(operate_dispatch.persist_requests.empty());

  mir2::LogicRuntime fifo_runtime(config);
  fifo_runtime.initialize();
  static_cast<void>(fifo_runtime.enqueue_ready_user(make_ready(21, make_character("Alpha", 12, 10))));
  static_cast<void>(fifo_runtime.enqueue_ready_user(make_ready(22, make_character("Beta", 14, 10))));
  const auto fifo_dispatch = fifo_runtime.tick(2000);
  const auto ready_messages = audit_messages(fifo_dispatch, "world.ready_user");
  assert(ready_messages.size() == 2);
  assert(ready_messages[0] == "acct_Alpha:Alpha");
  assert(ready_messages[1] == "acct_Beta:Beta");
  assert(fifo_runtime.legacy_session_state(21) == mir2::LegacyPlayerState::initialize_pending);
  assert(fifo_runtime.legacy_session_state(22) == mir2::LegacyPlayerState::initialize_pending);

  return 0;
}
