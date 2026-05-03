#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/local_bus.hpp"
#include "core/metrics_registry.hpp"
#include "core/module.hpp"
#include "core/shutdown_token.hpp"
#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "services/auth_service.hpp"
#include "services/persistence_service.hpp"
#include "spdlog/logger.h"
#include "spdlog/sinks/null_sink.h"
#include "util/string_utils.hpp"

namespace {

std::optional<mir2::SessionEvent> wait_for_session_event(
    const std::shared_ptr<mir2::LocalBus::Endpoint>& endpoint,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (auto message = endpoint->queue->wait_pop_for(std::chrono::milliseconds(25));
        message.has_value()) {
      if (auto event = std::get_if<mir2::SessionEvent>(&*message)) {
        return *event;
      }
    }
  }
  return std::nullopt;
}

std::optional<mir2::LogicCommand> wait_for_logic_command(
    const std::shared_ptr<mir2::LocalBus::Endpoint>& endpoint,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (auto message = endpoint->queue->wait_pop_for(std::chrono::milliseconds(25));
        message.has_value()) {
      if (auto command = std::get_if<mir2::LogicCommand>(&*message)) {
        return *command;
      }
    }
  }
  return std::nullopt;
}

mir2::LegacyPacket make_login_packet(std::uint64_t session_id, std::uint16_t ident,
                                     const std::string& body = {}, std::int32_t recog = 0) {
  return mir2::make_legacy_game_packet(
      session_id, 0, 0, mir2::make_default_message(ident, recog, 0, 0, 0),
      body.empty() ? std::string{} : mir2::legacy_encode_string(body));
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto temp_root = std::filesystem::temp_directory_path() / "mir2_auth_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root, ignored);

  mir2::HostConfig config;
  config.runtime.data_dir = temp_root / "data";
  config.runtime.log_dir = temp_root / "logs";
  config.runtime.status_file = temp_root / "runtime" / "status.json";
  config.runtime.default_queue_capacity = 256;
  config.ports.login_gateway.address = "127.0.0.1";
  config.ports.login_gateway.port = 5500;
  config.ports.game_gateway.address = "127.0.0.1";
  config.ports.game_gateway.port = 7000;

  mir2::LocalBus bus;
  mir2::MetricsRegistry metrics;
  mir2::ShutdownToken shutdown;
  auto logger = std::make_shared<spdlog::logger>("auth_smoke", std::make_shared<spdlog::sinks::null_sink_mt>());

  mir2::HostContext context;
  context.root_dir = source_root;
  context.config = config;
  context.bus = &bus;
  context.metrics = &metrics;
  context.shutdown = &shutdown;
  context.logger = logger;

  auto login_gateway = bus.register_endpoint("login_gateway", 256);
  auto world_service = bus.register_endpoint("world_service", 256);
  static_cast<void>(bus.register_endpoint("log_service", 256));

  mir2::PersistenceService persistence;
  mir2::AuthService auth;
  persistence.start(context);
  auth.start(context);

  const auto stop_services = [&] {
    auth.stop();
    persistence.stop();
    auth.join();
    persistence.join();
  };

  if (!bus.post("auth_service",
                mir2::SessionEvent{mir2::SessionEventKind::packet_received,
                                   "login_gateway",
                                   1,
                                   "127.0.0.1:10001",
                                   make_login_packet(1, mir2::kCmIdPassword, "guest/pass", 2026),
                                   {}})) {
    stop_services();
    return 1;
  }

  const auto login_response = wait_for_session_event(login_gateway);
  if (!login_response.has_value()) {
    stop_services();
    return 1;
  }

  const auto decoded_login = mir2::decode_legacy_game_packet(login_response->packet);
  if (!decoded_login.has_value() || decoded_login->message.ident != mir2::kSmPassOkSelectServer) {
    stop_services();
    return 1;
  }

  if (!bus.post("auth_service",
                mir2::SessionEvent{mir2::SessionEventKind::packet_received,
                                   "login_gateway",
                                   1,
                                   "127.0.0.1:10001",
                                   make_login_packet(1, mir2::kCmSelectServer, "Alpha"),
                                   {}})) {
    stop_services();
    return 1;
  }

  const auto select_server_response = wait_for_session_event(login_gateway);
  if (!select_server_response.has_value()) {
    stop_services();
    return 1;
  }

  const auto decoded_select_server = mir2::decode_legacy_game_packet(select_server_response->packet);
  if (!decoded_select_server.has_value() ||
      decoded_select_server->message.ident != mir2::kSmSelectServerOk) {
    stop_services();
    return 1;
  }

  const auto server_info = mir2::legacy_decode_string(decoded_select_server->body);
  auto server_fields = mir2::util::split(server_info, '/');
  if (server_fields.size() < 3 || server_fields[0] != "127.0.0.1" || server_fields[1] != "7000") {
    stop_services();
    return 1;
  }
  const auto certification = std::stoi(server_fields[2]);
  if (certification < 2 || decoded_select_server->message.recog != certification) {
    stop_services();
    return 1;
  }

  if (!bus.post("auth_service",
                mir2::SessionEvent{mir2::SessionEventKind::packet_received,
                                   "login_gateway",
                                   1,
                                   "127.0.0.1:10001",
                                   make_login_packet(1, mir2::kCmQueryChr,
                                                     "guest/" + std::to_string(certification)),
                                   {}})) {
    stop_services();
    return 1;
  }

  const auto query_response = wait_for_session_event(login_gateway);
  if (!query_response.has_value()) {
    stop_services();
    return 1;
  }

  const auto decoded_query = mir2::decode_legacy_game_packet(query_response->packet);
  if (!decoded_query.has_value() || decoded_query->message.ident != mir2::kSmQueryChr) {
    stop_services();
    return 1;
  }
  if (mir2::legacy_decode_string(decoded_query->body).find("Hero/") == std::string::npos) {
    stop_services();
    return 1;
  }

  if (!bus.post("auth_service",
                mir2::SessionEvent{mir2::SessionEventKind::packet_received,
                                   "login_gateway",
                                   1,
                                   "127.0.0.1:10001",
                                   make_login_packet(1, mir2::kCmNewChr, "guest/Mage/2/1/1"),
                                   {}})) {
    stop_services();
    return 1;
  }

  const auto create_response = wait_for_session_event(login_gateway);
  if (!create_response.has_value()) {
    stop_services();
    return 1;
  }
  const auto decoded_create = mir2::decode_legacy_game_packet(create_response->packet);
  if (!decoded_create.has_value() || decoded_create->message.ident != mir2::kSmNewChrSuccess) {
    stop_services();
    return 1;
  }

  if (!bus.post("auth_service",
                mir2::SessionEvent{mir2::SessionEventKind::packet_received,
                                   "login_gateway",
                                   1,
                                   "127.0.0.1:10001",
                                   make_login_packet(1, mir2::kCmSelChr, "guest/Mage"),
                                   {}})) {
    stop_services();
    return 1;
  }

  const auto select_response = wait_for_session_event(login_gateway);
  const auto world_admission = wait_for_logic_command(world_service);
  if (!select_response.has_value() || !world_admission.has_value()) {
    stop_services();
    return 1;
  }

  const auto decoded_select = mir2::decode_legacy_game_packet(select_response->packet);
  if (!decoded_select.has_value() || decoded_select->message.ident != mir2::kSmStartPlay) {
    stop_services();
    return 1;
  }
  const auto play_target = mir2::legacy_decode_string(decoded_select->body);
  if (play_target != "127.0.0.1/7000") {
    stop_services();
    return 1;
  }

  if (world_admission->kind != mir2::LogicCommandKind::authenticate ||
      world_admission->account_id != "guest" || world_admission->character_name != "Mage" ||
      world_admission->certification != certification) {
    stop_services();
    return 1;
  }

  if (!bus.post("auth_service",
                mir2::SessionEvent{mir2::SessionEventKind::disconnected,
                                   "login_gateway",
                                   1,
                                   "127.0.0.1:10001",
                                   {},
                                   "login_done"})) {
    stop_services();
    return 1;
  }

  if (!bus.post("auth_service",
                mir2::SessionEvent{mir2::SessionEventKind::packet_received,
                                   "login_gateway",
                                   3,
                                   "127.0.0.1:10003",
                                   make_login_packet(3, mir2::kCmIdPassword, "guest/pass", 2026),
                                   {}})) {
    stop_services();
    return 1;
  }

  const auto revoke_admission = wait_for_logic_command(world_service);
  const auto duplicate_fail = wait_for_session_event(login_gateway);
  if (!revoke_admission.has_value() || !duplicate_fail.has_value()) {
    stop_services();
    return 1;
  }

  if (revoke_admission->kind != mir2::LogicCommandKind::revoke_authentication ||
      revoke_admission->account_id != "guest" || revoke_admission->character_name != "Mage" ||
      revoke_admission->certification != certification) {
    stop_services();
    return 1;
  }

  const auto decoded_duplicate_fail = mir2::decode_legacy_game_packet(duplicate_fail->packet);
  if (!decoded_duplicate_fail.has_value() ||
      decoded_duplicate_fail->message.ident != mir2::kSmPasswdFail ||
      decoded_duplicate_fail->message.recog != -3) {
    stop_services();
    return 1;
  }

  stop_services();
  std::filesystem::remove_all(temp_root, ignored);
  return 0;
}
