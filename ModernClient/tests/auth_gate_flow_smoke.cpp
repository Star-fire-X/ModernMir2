#include "app/client_app.hpp"

#include <cassert>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

using mir2::client::AuthFlowPhase;
using mir2::client::ClientApp;
using mir2::client::ClientConfig;
using mir2::client::GameStateStore;
using mir2::client::SceneId;
using namespace mir2::client_v1;

ClientConfig test_config() {
  ClientConfig config;
  config.login_host = "10.0.0.1";
  config.login_port = 5600;
  config.client_build = 77;
  config.resource_revision = 88;
  config.auto_play.enabled = false;
  return config;
}

void configure(ClientApp& app) {
  app.set_config_for_test(test_config());
  app.enable_protocol_test_mode_for_test();
}

std::vector<Frame> expect_sent(ClientApp& app, std::initializer_list<MessageId> expected) {
  auto frames = app.drain_sent_frames_for_test();
  assert(frames.size() == expected.size());
  auto it = expected.begin();
  for (const auto& frame : frames) {
    assert(frame.message_id == *it);
    ++it;
  }
  return frames;
}

template <typename T>
T decode(const Frame& frame) {
  auto message = decode_message<T>(frame);
  assert(message.has_value());
  return *message;
}

CharacterSummary hero() { return CharacterSummary{"Hero", 22, 0, 0, 1, "0"}; }

void start_login(ClientApp& app) {
  app.request_login("guest", "pass");
  assert(app.state_for_test().auth_phase == AuthFlowPhase::ConnectingLoginGate);
  assert(app.state_for_test().connection_phase == GameStateStore::ConnectionPhase::login);
  assert(app.connect_attempts_for_test().size() == 1U);
  assert(app.connect_attempts_for_test()[0].host == "10.0.0.1");
  assert(app.connect_attempts_for_test()[0].port == 5600);

  app.complete_connect_for_test();
  app.pump_protocol_for_test();
  const auto frames =
      expect_sent(app, {MessageId::client_hello, MessageId::login_request});
  const auto hello = decode<ClientHello>(frames[0]);
  assert(hello.client_build == 77);
  assert(hello.resource_revision == 88);
  const auto request = decode<LoginRequest>(frames[1]);
  assert(request.account_id == "guest");
  assert(request.password == "pass");
  assert(app.state_for_test().auth_phase == AuthFlowPhase::WaitingLoginResult);
}

void reach_server_select(ClientApp& app) {
  start_login(app);
  app.push_protocol_message_for_test(LoginResult{true, 0, "guest", "Guest", ""});
  app.push_protocol_message_for_test(
      ServerList{{ServerEntry{"Alpha", "10.0.0.2", 5800}}});
  app.pump_protocol_for_test();
  assert(app.state_for_test().auth_phase == AuthFlowPhase::BrowsingServers);
  assert(app.state_for_test().connection_phase == GameStateStore::ConnectionPhase::login);
  assert(app.current_scene_for_test() == SceneId::server_select);
}

void select_server_and_connect_character_gate(ClientApp& app) {
  app.request_select_server("Alpha");
  const auto select_frames = expect_sent(app, {MessageId::select_server_request});
  assert(decode<SelectServerRequest>(select_frames[0]).name == "Alpha");

  app.push_protocol_message_for_test(
      SelectServerResult{true, "Alpha", "10.0.0.2", 5800, "lobby-token", ""});
  app.pump_protocol_for_test();
  assert(app.state_for_test().auth_phase == AuthFlowPhase::ConnectingCharacterGate);
  assert(app.connect_attempts_for_test().size() == 2U);
  assert(app.connect_attempts_for_test()[1].host == "10.0.0.2");
  assert(app.connect_attempts_for_test()[1].port == 5800);

  app.pump_protocol_for_test();
  assert(app.state_for_test().auth_phase == AuthFlowPhase::ConnectingCharacterGate);
  assert(!app.state_for_test().modal.visible);
  assert(app.current_scene_for_test() != SceneId::login);

  app.complete_connect_for_test();
  app.pump_protocol_for_test();
  const auto frames =
      expect_sent(app, {MessageId::client_hello, MessageId::character_list_request});
  assert(decode<CharacterListRequest>(frames[1]).lobby_token == "lobby-token");
  assert(app.state_for_test().auth_phase == AuthFlowPhase::QueryingCharacters);
  assert(app.state_for_test().connection_phase ==
         GameStateStore::ConnectionPhase::select_character);
}

void reach_character_select(ClientApp& app) {
  reach_server_select(app);
  select_server_and_connect_character_gate(app);
  CharacterList characters;
  characters.characters.push_back(hero());
  characters.selected_name = "Hero";
  app.push_protocol_message_for_test(characters);
  app.pump_protocol_for_test();
  assert(app.state_for_test().auth_phase == AuthFlowPhase::BrowsingCharacters);
  assert(app.current_scene_for_test() == SceneId::character_select);
  assert(app.state_for_test().lobby.selected_index == 0);
}

void select_character_and_connect_run_gate(ClientApp& app) {
  app.request_selected_character_enter();
  const auto select_frames = expect_sent(app, {MessageId::select_character_request});
  assert(decode<SelectCharacterRequest>(select_frames[0]).name == "Hero");

  app.push_protocol_message_for_test(
      SelectCharacterResult{true, "Hero", "world-token", "10.0.0.3", 5690, ""});
  app.pump_protocol_for_test();
  assert(app.state_for_test().auth_phase == AuthFlowPhase::ConnectingRunGate);
  assert(app.state_for_test().connection_phase == GameStateStore::ConnectionPhase::play);
  assert(app.current_scene_for_test() == SceneId::loading);
  assert(app.connect_attempts_for_test().size() == 3U);
  assert(app.connect_attempts_for_test()[2].host == "10.0.0.3");
  assert(app.connect_attempts_for_test()[2].port == 5690);

  app.pump_protocol_for_test();
  assert(app.state_for_test().auth_phase == AuthFlowPhase::ConnectingRunGate);
  assert(!app.state_for_test().modal.visible);
  assert(app.current_scene_for_test() != SceneId::login);

  app.complete_connect_for_test();
  app.pump_protocol_for_test();
  const auto frames =
      expect_sent(app, {MessageId::client_hello, MessageId::enter_world_request});
  const auto enter = decode<EnterWorldRequest>(frames[1]);
  assert(enter.token == "world-token");
  assert(enter.client_build == 77);
  assert(enter.resource_revision == 88);
  assert(app.state_for_test().auth_phase == AuthFlowPhase::EnteringWorld);
}

void test_happy_path_with_login_notice() {
  ClientApp app;
  configure(app);
  reach_character_select(app);
  select_character_and_connect_run_gate(app);

  app.push_protocol_message_for_test(LoginNotice{"Notice", "Read this"});
  app.pump_protocol_for_test();
  assert(app.state_for_test().auth_phase == AuthFlowPhase::ViewingLoginNotice);
  assert(app.current_scene_for_test() == SceneId::login_notice);

  app.acknowledge_login_notice();
  app.acknowledge_login_notice();
  expect_sent(app, {MessageId::login_notice_ok});
  assert(app.state_for_test().auth_phase == AuthFlowPhase::EnteringWorld);
  app.pump_protocol_for_test();
  assert(app.current_scene_for_test() == SceneId::loading);

  EnterWorldResult enter;
  enter.success = true;
  enter.self_actor_id = 1000;
  enter.character_name = "Hero";
  enter.map_id = "0";
  enter.x = 330;
  enter.y = 270;
  WorldSnapshot snapshot;
  snapshot.map_id = "0";
  snapshot.width = 700;
  snapshot.height = 700;
  snapshot.self_actor_id = 1000;
  snapshot.actors.push_back(WorldActor{1000, "Hero", 330, 270, 0, 0, 0, ActorType::player});
  app.push_protocol_message_for_test(enter);
  app.push_protocol_message_for_test(snapshot);
  app.pump_protocol_for_test();
  assert(app.state_for_test().auth_phase == AuthFlowPhase::InGame);
  assert(app.current_scene_for_test() == SceneId::world);
}

void test_failure_paths() {
  {
    ClientApp app;
    configure(app);
    start_login(app);
    app.push_protocol_message_for_test(LoginResult{false, -1, "guest", "", "login_failed"});
    app.pump_protocol_for_test();
    assert(app.state_for_test().auth_phase == AuthFlowPhase::EditingLogin);
    assert(app.state_for_test().modal.visible);
  }
  {
    ClientApp app;
    configure(app);
    reach_server_select(app);
    app.request_select_server("Alpha");
    expect_sent(app, {MessageId::select_server_request});
    app.push_protocol_message_for_test(SelectServerResult{false, "Alpha", "", 0, "", "missing"});
    app.pump_protocol_for_test();
    assert(app.state_for_test().auth_phase == AuthFlowPhase::BrowsingServers);
    assert(app.connect_attempts_for_test().size() == 1U);
    assert(app.state_for_test().modal.visible);
  }
  {
    ClientApp app;
    configure(app);
    reach_character_select(app);
    app.request_selected_character_enter();
    expect_sent(app, {MessageId::select_character_request});
    app.push_protocol_message_for_test(
        SelectCharacterResult{false, "Hero", "", "", 0, "character_not_found"});
    app.pump_protocol_for_test();
    assert(app.state_for_test().auth_phase == AuthFlowPhase::BrowsingCharacters);
    assert(app.connect_attempts_for_test().size() == 2U);
    assert(app.state_for_test().modal.visible);
  }
}

void test_disconnect_paths() {
  {
    ClientApp app;
    configure(app);
    reach_server_select(app);
    app.push_protocol_disconnect_for_test("remote_closed");
    app.pump_protocol_for_test();
    assert(app.state_for_test().auth_phase == AuthFlowPhase::Disconnected);
    assert(app.state_for_test().modal.visible);
  }
  {
    ClientApp app;
    configure(app);
    auto& state = app.state_for_test();
    state.login.account_id = "guest";
    state.login.password = "pass";
    state.connection_phase = GameStateStore::ConnectionPhase::play;
    state.auth_phase = AuthFlowPhase::InGame;
    app.push_protocol_disconnect_for_test("remote_closed");
    app.pump_protocol_for_test();
    assert(state.auth_phase == AuthFlowPhase::Disconnected);
    assert(state.connection_phase == GameStateStore::ConnectionPhase::play);
    assert(state.modal.visible);
    assert(state.modal.title == L"Disconnected");
  }
  {
    ClientApp app;
    configure(app);
    auto& state = app.state_for_test();
    state.connection_phase = GameStateStore::ConnectionPhase::play;
    state.auth_phase = AuthFlowPhase::InGame;
    state.world.actors[1000].actor_id = 1000;
    state.world.actors[1000].name = "Hero";
    app.push_protocol_message_for_test(DisconnectReason{501, "character_not_found"});
    app.pump_protocol_for_test();
    assert(state.auth_phase == AuthFlowPhase::EditingLogin);
    assert(state.connection_phase == GameStateStore::ConnectionPhase::login);
    assert(state.world.actors.empty());
    assert(state.modal.visible);
    assert(app.current_scene_for_test() == SceneId::login);
  }
}

}  // namespace

int main() {
  test_happy_path_with_login_notice();
  test_failure_paths();
  test_disconnect_paths();
  return 0;
}
