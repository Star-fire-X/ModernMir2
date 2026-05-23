#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "asio.hpp"
#include "core/local_bus.hpp"
#include "core/metrics_registry.hpp"
#include "core/module.hpp"
#include "core/shutdown_token.hpp"
#include "services/client_v1_admission_registry.hpp"
#include "services/client_v1_game_gateway_service.hpp"
#include "services/client_v1_login_gateway_service.hpp"
#include "services/world_service.hpp"
#include "shared/protocol/client_v1/protocol.hpp"
#include "spdlog/logger.h"
#include "spdlog/sinks/null_sink.h"

namespace {

template <typename T>
void send_message(asio::ip::tcp::socket& socket, const T& message, std::uint32_t& sequence) {
  const auto bytes = mir2::client_v1::encode_frame(mir2::client_v1::make_frame(message, sequence++));
  asio::write(socket, asio::buffer(bytes));
}

class SocketReader {
 public:
  explicit SocketReader(asio::ip::tcp::socket& socket) : socket_(socket) { socket_.non_blocking(true); }

  template <typename T>
  std::optional<T> wait_for_message(
      std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
    if (auto pending = take_pending<T>(); pending.has_value()) {
      return pending;
    }

    std::array<std::uint8_t, 4096> read_buffer{};
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      std::error_code error;
      const auto bytes_read = socket_.read_some(asio::buffer(read_buffer), error);
      if (!error) {
        buffer_.insert(buffer_.end(), read_buffer.begin(), read_buffer.begin() + bytes_read);
        auto frames = mir2::client_v1::drain_frames(buffer_);
        std::optional<T> matched;
        for (const auto& frame : frames) {
          const auto decoded = mir2::client_v1::decode_any(frame);
          if (!decoded.has_value()) {
            return std::nullopt;
          }
          if (const auto* value = std::get_if<T>(&*decoded); value != nullptr) {
            if (!matched.has_value()) {
              matched = *value;
              continue;
            }
          }
          pending_.push_back(*decoded);
        }
        if (matched.has_value()) {
          return matched;
        }
      } else if (error != asio::error::would_block && error != asio::error::try_again) {
        return std::nullopt;
      }

      if (auto pending = take_pending<T>(); pending.has_value()) {
        return pending;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return std::nullopt;
  }

 private:
  template <typename T>
  std::optional<T> take_pending() {
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
      if (const auto* value = std::get_if<T>(&*it); value != nullptr) {
        auto copy = *value;
        pending_.erase(it);
        return copy;
      }
    }
    return std::nullopt;
  }

  asio::ip::tcp::socket& socket_;
  std::vector<std::uint8_t> buffer_{};
  std::vector<mir2::client_v1::Message> pending_{};
};

std::optional<asio::ip::tcp::socket> connect_socket(asio::io_context& io_context,
                                                    const std::string& host,
                                                    const std::uint16_t port) {
  asio::ip::tcp::endpoint endpoint(asio::ip::make_address(host), port);
  asio::ip::tcp::socket socket(io_context);
  for (int attempt = 0; attempt < 50; ++attempt) {
    std::error_code connect_error;
    socket.connect(endpoint, connect_error);
    if (!connect_error) {
      return socket;
    }
    socket.close();
    socket = asio::ip::tcp::socket(io_context);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return std::nullopt;
}

mir2::client_v1::AccountProfile complete_profile() {
  mir2::client_v1::AccountProfile profile;
  profile.display_name = "Phase One";
  profile.user_name = "Phase One";
  profile.ss_no = "650101-1455111";
  profile.birthday = "1975/08/21";
  profile.quiz = "first";
  profile.answer = "answer";
  profile.quiz2 = "second";
  profile.answer2 = "answer2";
  profile.phone = "021";
  profile.mobile_phone = "13900000000";
  profile.email = "phaseone@example.test";
  return profile;
}

int fail(const char* stage) {
  std::cerr << "client_v1_login_to_world_flow_smoke failed at " << stage << '\n';
  return 1;
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto temp_root = std::filesystem::temp_directory_path() / "mir2_client_v1_login_to_world_flow_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root, ignored);

  mir2::HostConfig config;
  config.runtime.data_dir = temp_root / "data";
  config.runtime.log_dir = temp_root / "logs";
  config.runtime.status_file = temp_root / "runtime" / "status.json";
  config.runtime.default_queue_capacity = 256;
  config.runtime.io_threads = 2;
  config.runtime.login_notice_title = "Phase 1 Notice";
  config.runtime.login_notice_text = "Confirm to enter Phase 1.";
  config.maps.push_back(mir2::MapConfig{"0", "TestMap", {}, 700, 700, 330, 270});
  config.ports.client_v1_login_gateway.address = "127.0.0.1";
  config.ports.client_v1_login_gateway.port = 5614;
  config.ports.client_v1_game_gateway.address = "127.0.0.1";
  config.ports.client_v1_game_gateway.port = 7114;

  mir2::LocalBus bus;
  mir2::MetricsRegistry metrics;
  mir2::ShutdownToken shutdown;
  auto logger = std::make_shared<spdlog::logger>(
      "client_v1_login_to_world_flow_smoke", std::make_shared<spdlog::sinks::null_sink_mt>());

  mir2::HostContext context;
  context.root_dir = source_root;
  context.config = config;
  context.bus = &bus;
  context.metrics = &metrics;
  context.shutdown = &shutdown;
  context.logger = logger;

  auto admissions = std::make_shared<mir2::ClientV1AdmissionRegistry>();
  mir2::ClientV1LoginGatewayService login_gateway(admissions);
  mir2::WorldService world_service;
  mir2::ClientV1GameGatewayService game_gateway(admissions);
  world_service.start(context);
  login_gateway.start(context);
  game_gateway.start(context);

  const auto stop_services = [&] {
    login_gateway.stop();
    game_gateway.stop();
    world_service.stop();
    login_gateway.join();
    game_gateway.join();
    world_service.join();
    std::filesystem::remove_all(temp_root, ignored);
  };

  asio::io_context io_context;
  auto login_socket = connect_socket(io_context, "127.0.0.1", 5614);
  if (!login_socket.has_value()) {
    stop_services();
    return fail("login connect");
  }

  SocketReader login_reader(*login_socket);
  std::uint32_t login_sequence = 1;
  send_message(*login_socket, mir2::client_v1::ClientHello{}, login_sequence);

  mir2::client_v1::AccountProfile incomplete_profile;
  incomplete_profile.display_name = "Phase One";
  send_message(*login_socket,
               mir2::client_v1::CreateAccountRequest{"phaseone", "pass1", incomplete_profile},
               login_sequence);
  const auto create_result = login_reader.wait_for_message<mir2::client_v1::CreateAccountResult>();
  if (!create_result.has_value() || !create_result->success) {
    stop_services();
    return fail("create account");
  }

  send_message(*login_socket,
               mir2::client_v1::CreateAccountRequest{"phaseone", "pass1", incomplete_profile},
               login_sequence);
  const auto duplicate_create =
      login_reader.wait_for_message<mir2::client_v1::CreateAccountResult>();
  if (!duplicate_create.has_value() || duplicate_create->success) {
    stop_services();
    return fail("duplicate create account");
  }

  send_message(*login_socket, mir2::client_v1::LoginRequest{"phaseone", "pass1"}, login_sequence);
  const auto login_result = login_reader.wait_for_message<mir2::client_v1::LoginResult>();
  if (!login_result.has_value() || !login_result->success) {
    stop_services();
    return fail("login");
  }

  const auto need_update = login_reader.wait_for_message<mir2::client_v1::NeedUpdateAccount>();
  if (!need_update.has_value() || need_update->account_id != "phaseone") {
    stop_services();
    return fail("need update account");
  }

  send_message(*login_socket,
               mir2::client_v1::UpdateAccountRequest{"phaseone", "pass1", complete_profile()},
               login_sequence);
  const auto update_result = login_reader.wait_for_message<mir2::client_v1::UpdateAccountResult>();
  if (!update_result.has_value() || !update_result->success) {
    stop_services();
    return fail("update account");
  }

  const auto server_list = login_reader.wait_for_message<mir2::client_v1::ServerList>();
  if (!server_list.has_value() || server_list->servers.empty() ||
      server_list->servers.front().name != "ModernServer") {
    stop_services();
    return fail("server list");
  }

  send_message(*login_socket, mir2::client_v1::SelectServerRequest{"ModernServer"}, login_sequence);
  const auto select_server =
      login_reader.wait_for_message<mir2::client_v1::SelectServerResult>();
  if (!select_server.has_value() || !select_server->success ||
      select_server->lobby_token.empty()) {
    stop_services();
    return fail("select server");
  }

  auto char_socket = connect_socket(io_context, select_server->address, select_server->port);
  if (!char_socket.has_value()) {
    stop_services();
    return fail("character connect");
  }
  SocketReader char_reader(*char_socket);
  std::uint32_t char_sequence = 1;
  send_message(*char_socket, mir2::client_v1::ClientHello{}, char_sequence);
  send_message(*char_socket,
               mir2::client_v1::CharacterListRequest{select_server->lobby_token},
               char_sequence);
  const auto initial_characters = char_reader.wait_for_message<mir2::client_v1::CharacterList>();
  if (!initial_characters.has_value() || !initial_characters->characters.empty()) {
    stop_services();
    return fail("initial character list");
  }

  send_message(*char_socket, mir2::client_v1::CreateCharacterRequest{"X", 0, 0, 0},
               char_sequence);
  const auto invalid_character =
      char_reader.wait_for_message<mir2::client_v1::CreateCharacterResult>();
  if (!invalid_character.has_value() || invalid_character->success) {
    stop_services();
    return fail("invalid character create");
  }

  send_message(*char_socket, mir2::client_v1::CreateCharacterRequest{"HeroOne", 0, 0, 1},
               char_sequence);
  const auto create_hero_one =
      char_reader.wait_for_message<mir2::client_v1::CreateCharacterResult>();
  if (!create_hero_one.has_value() || !create_hero_one->success) {
    stop_services();
    return fail("create first character");
  }

  send_message(*char_socket, mir2::client_v1::DeleteCharacterRequest{"HeroOne"}, char_sequence);
  const auto delete_hero_one =
      char_reader.wait_for_message<mir2::client_v1::DeleteCharacterResult>();
  if (!delete_hero_one.has_value() || !delete_hero_one->success ||
      delete_hero_one->deleted_name != "HeroOne") {
    stop_services();
    return fail("delete first character");
  }

  send_message(*char_socket, mir2::client_v1::CreateCharacterRequest{"HeroTwo", 1, 1, 2},
               char_sequence);
  const auto create_hero_two =
      char_reader.wait_for_message<mir2::client_v1::CreateCharacterResult>();
  if (!create_hero_two.has_value() || !create_hero_two->success) {
    stop_services();
    return fail("create second character");
  }

  send_message(*char_socket, mir2::client_v1::CharacterListRequest{}, char_sequence);
  const auto final_characters = char_reader.wait_for_message<mir2::client_v1::CharacterList>();
  if (!final_characters.has_value() || final_characters->characters.size() != 1 ||
      final_characters->characters.front().name != "HeroTwo") {
    stop_services();
    return fail("final character list");
  }

  send_message(*char_socket, mir2::client_v1::SelectCharacterRequest{"HeroTwo"}, char_sequence);
  const auto select_character =
      char_reader.wait_for_message<mir2::client_v1::SelectCharacterResult>();
  if (!select_character.has_value() || !select_character->success ||
      select_character->enter_world_token.empty()) {
    stop_services();
    return fail("select character");
  }

  auto game_socket = connect_socket(io_context, select_character->address, select_character->port);
  if (!game_socket.has_value()) {
    stop_services();
    return fail("game connect");
  }
  SocketReader game_reader(*game_socket);
  std::uint32_t game_sequence = 1;
  send_message(*game_socket, mir2::client_v1::ClientHello{}, game_sequence);
  send_message(*game_socket,
               mir2::client_v1::EnterWorldRequest{select_character->enter_world_token, 1, 1},
               game_sequence);
  const auto notice = game_reader.wait_for_message<mir2::client_v1::LoginNotice>();
  if (!notice.has_value() || notice->title != "Phase 1 Notice") {
    stop_services();
    return fail("login notice");
  }

  send_message(*game_socket, mir2::client_v1::LoginNoticeOk{}, game_sequence);
  const auto enter_world = game_reader.wait_for_message<mir2::client_v1::EnterWorldResult>();
  if (!enter_world.has_value() || !enter_world->success ||
      enter_world->character_name != "HeroTwo") {
    stop_services();
    return fail("enter world");
  }

  const auto snapshot = game_reader.wait_for_message<mir2::client_v1::WorldSnapshot>();
  if (!snapshot.has_value() || snapshot->self_actor_id != enter_world->self_actor_id ||
      snapshot->actors.empty() || snapshot->actors.front().name != "HeroTwo") {
    stop_services();
    return fail("world snapshot");
  }

  const auto move_x = snapshot->actors.front().x + 1;
  const auto move_y = snapshot->actors.front().y + 1;
  send_message(*game_socket, mir2::client_v1::MoveIntent{move_x, move_y, mir2::client_v1::MoveMode::walk},
               game_sequence);
  const auto ack = game_reader.wait_for_message<mir2::client_v1::ActionAck>();
  if (!ack.has_value() || !ack->ok) {
    stop_services();
    return fail("move ack");
  }

  stop_services();
  return 0;
}
