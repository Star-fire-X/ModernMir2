#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

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

mir2::LegacyPacket make_login_packet(std::uint64_t session_id, std::uint16_t ident,
                                     const std::string& body = {}, std::int32_t recog = 0) {
  return mir2::make_legacy_game_packet(
      session_id, 0, 0, mir2::make_default_message(ident, recog, 0, 0, 0),
      body.empty() ? std::string{} : mir2::legacy_encode_string(body));
}

mir2::LegacyPacket make_account_packet(std::uint64_t session_id, std::uint16_t ident,
                                       const mir2::LegacyUserEntryInfo& info,
                                       const mir2::LegacyUserEntryAddInfo& add_info) {
  return mir2::make_legacy_game_packet(
      session_id, 0, 0, mir2::make_default_message(ident, 0, 0, 0, 0),
      mir2::legacy_encode_buffer(&info, sizeof(info)) +
          mir2::legacy_encode_buffer(&add_info, sizeof(add_info)));
}

mir2::LegacyUserEntryInfo make_info(const std::string& account_id, const std::string& password,
                                    const std::string& user_name = "User") {
  mir2::LegacyUserEntryInfo info;
  mir2::set_short_string(info.login_id, account_id);
  mir2::set_short_string(info.password, password);
  mir2::set_short_string(info.user_name, user_name);
  mir2::set_short_string(info.ss_no, "721109-1476110");
  mir2::set_short_string(info.phone, "0212345678");
  mir2::set_short_string(info.quiz, "first");
  mir2::set_short_string(info.answer, "answer");
  mir2::set_short_string(info.email, account_id + "@example.invalid");
  return info;
}

mir2::LegacyUserEntryAddInfo make_add_info(const std::string& quiz2 = "second",
                                           const std::string& answer2 = "answer2") {
  mir2::LegacyUserEntryAddInfo add_info;
  mir2::set_short_string(add_info.quiz2, quiz2);
  mir2::set_short_string(add_info.answer2, answer2);
  mir2::set_short_string(add_info.birthday, "1972/11/09");
  mir2::set_short_string(add_info.mobile_phone, "017-6227-1234");
  mir2::set_short_string(add_info.memo1, "memo1");
  mir2::set_short_string(add_info.memo2, "memo2");
  return add_info;
}

std::optional<mir2::DecodedLegacyGamePacket> wait_for_decoded_packet(
    const std::shared_ptr<mir2::LocalBus::Endpoint>& endpoint,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) {
  const auto event = wait_for_session_event(endpoint, timeout);
  if (!event.has_value()) {
    return std::nullopt;
  }
  return mir2::decode_legacy_game_packet(event->packet);
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto temp_root = std::filesystem::temp_directory_path() / "mir2_account_smoke";
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
  auto logger =
      std::make_shared<spdlog::logger>("account_smoke", std::make_shared<spdlog::sinks::null_sink_mt>());

  mir2::HostContext context;
  context.root_dir = source_root;
  context.config = config;
  context.bus = &bus;
  context.metrics = &metrics;
  context.shutdown = &shutdown;
  context.logger = logger;

  auto login_gateway = bus.register_endpoint("login_gateway", 512);
  static_cast<void>(bus.register_endpoint("world_service", 256));
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

  const auto fail = [&](int step) {
    std::cerr << "account_smoke_step=" << step << '\n';
    stop_services();
    return step;
  };

  if (!bus.post("auth_service",
                mir2::SessionEvent{mir2::SessionEventKind::packet_received,
                                   "login_gateway",
                                   10,
                                   "127.0.0.1:10010",
                                   make_account_packet(10, mir2::kCmAddNewUser,
                                                       make_info("alpha", "oldpw", "Alpha"),
                                                       make_add_info()),
                                   {}})) {
    return fail(1);
  }
  auto packet = wait_for_decoded_packet(login_gateway);
  if (!packet.has_value() || packet->message.ident != mir2::kSmNewIdSuccess) {
    return fail(2);
  }

  if (!bus.post("auth_service",
                mir2::SessionEvent{mir2::SessionEventKind::packet_received,
                                   "login_gateway",
                                   11,
                                   "127.0.0.1:10011",
                                   make_account_packet(11, mir2::kCmAddNewUser,
                                                       make_info("alpha", "oldpw", "Alpha"),
                                                       make_add_info()),
                                   {}})) {
    return fail(3);
  }
  packet = wait_for_decoded_packet(login_gateway);
  if (!packet.has_value() || packet->message.ident != mir2::kSmNewIdFail ||
      packet->message.recog != 0) {
    return fail(4);
  }

  if (!bus.post("auth_service",
                mir2::SessionEvent{mir2::SessionEventKind::packet_received,
                                   "login_gateway",
                                   12,
                                   "127.0.0.1:10012",
                                   make_login_packet(12, mir2::kCmChangePassword,
                                                     "alpha\toldpw\tnewpw"),
                                   {}})) {
    return fail(5);
  }
  packet = wait_for_decoded_packet(login_gateway);
  if (!packet.has_value() || packet->message.ident != mir2::kSmChgPasswdSuccess) {
    return fail(6);
  }

  if (!bus.post("auth_service",
                mir2::SessionEvent{mir2::SessionEventKind::packet_received,
                                   "login_gateway",
                                   13,
                                   "127.0.0.1:10013",
                                   make_login_packet(13, mir2::kCmIdPassword, "alpha/oldpw", 2026),
                                   {}})) {
    return fail(7);
  }
  packet = wait_for_decoded_packet(login_gateway);
  if (!packet.has_value() || packet->message.ident != mir2::kSmPasswdFail ||
      packet->message.recog != -1) {
    return fail(8);
  }

  if (!bus.post("auth_service",
                mir2::SessionEvent{mir2::SessionEventKind::packet_received,
                                   "login_gateway",
                                   14,
                                   "127.0.0.1:10014",
                                   make_login_packet(14, mir2::kCmIdPassword, "alpha/newpw", 2026),
                                   {}})) {
    return fail(9);
  }
  packet = wait_for_decoded_packet(login_gateway);
  if (!packet.has_value() || packet->message.ident != mir2::kSmPassOkSelectServer) {
    return fail(10);
  }

  if (!bus.post("auth_service",
                mir2::SessionEvent{mir2::SessionEventKind::packet_received,
                                   "login_gateway",
                                   15,
                                   "127.0.0.1:10015",
                                   make_login_packet(15, mir2::kCmIdPassword, "alpha/newpw", 2026),
                                   {}})) {
    return fail(11);
  }

  const auto kicked_alpha = wait_for_session_event(login_gateway);
  if (!kicked_alpha.has_value() || kicked_alpha->kind != mir2::SessionEventKind::force_disconnect ||
      kicked_alpha->session_id != 14 || kicked_alpha->reason != "duplicate_login") {
    return fail(12);
  }

  packet = wait_for_decoded_packet(login_gateway);
  if (!packet.has_value() || packet->message.ident != mir2::kSmPasswdFail ||
      packet->message.recog != -3) {
    return fail(13);
  }

  if (!bus.post("auth_service",
                mir2::SessionEvent{mir2::SessionEventKind::packet_received,
                                   "login_gateway",
                                   20,
                                   "127.0.0.1:10020",
                                   make_account_packet(20, mir2::kCmAddNewUser,
                                                       make_info("beta", "pw", ""),
                                                       make_add_info("", "")),
                                   {}})) {
    return fail(14);
  }
  packet = wait_for_decoded_packet(login_gateway);
  if (!packet.has_value() || packet->message.ident != mir2::kSmNewIdSuccess) {
    return fail(15);
  }

  if (!bus.post("auth_service",
                mir2::SessionEvent{mir2::SessionEventKind::packet_received,
                                   "login_gateway",
                                   21,
                                   "127.0.0.1:10021",
                                   make_login_packet(21, mir2::kCmIdPassword, "beta/pw", 2026),
                                   {}})) {
    return fail(16);
  }

  auto need_update = wait_for_decoded_packet(login_gateway);
  auto pass_ok = wait_for_decoded_packet(login_gateway);
  if (!need_update.has_value() || !pass_ok.has_value() ||
      need_update->message.ident != mir2::kSmNeedUpdateAccount ||
      pass_ok->message.ident != mir2::kSmPassOkSelectServer) {
    return fail(17);
  }

  mir2::LegacyUserEntryInfo need_update_info{};
  if (!mir2::legacy_decode_buffer(need_update->body, &need_update_info, sizeof(need_update_info)) ||
      mir2::to_string(need_update_info.login_id) != "beta" ||
      mir2::to_string(need_update_info.password) != "pw") {
    return fail(18);
  }

  if (!bus.post("auth_service",
                mir2::SessionEvent{mir2::SessionEventKind::packet_received,
                                   "login_gateway",
                                   21,
                                   "127.0.0.1:10021",
                                   make_account_packet(21, mir2::kCmUpdateUser,
                                                       make_info("beta", "pw", "Beta"),
                                                       make_add_info("second", "again")),
                                   {}})) {
    return fail(19);
  }
  packet = wait_for_decoded_packet(login_gateway);
  if (!packet.has_value() || packet->message.ident != mir2::kSmUpdateIdSuccess) {
    return fail(20);
  }

  if (!bus.post("auth_service",
                mir2::SessionEvent{mir2::SessionEventKind::disconnected, "login_gateway", 21,
                                   "127.0.0.1:10021", {}, {}})) {
    return fail(21);
  }

  if (!bus.post("auth_service",
                mir2::SessionEvent{mir2::SessionEventKind::packet_received,
                                   "login_gateway",
                                   22,
                                   "127.0.0.1:10022",
                                    make_login_packet(22, mir2::kCmIdPassword, "beta/pw", 2026),
                                    {}})) {
    return fail(22);
  }
  packet = wait_for_decoded_packet(login_gateway);
  if (!packet.has_value() || packet->message.ident != mir2::kSmPassOkSelectServer) {
    return fail(23);
  }

  if (!bus.post("auth_service",
                mir2::SessionEvent{mir2::SessionEventKind::packet_received,
                                   "login_gateway",
                                   30,
                                   "127.0.0.1:10030",
                                   make_account_packet(30, mir2::kCmAddNewUser,
                                                       make_info("lockme", "pw1", "Lock"),
                                                       make_add_info()),
                                   {}})) {
    return fail(24);
  }
  packet = wait_for_decoded_packet(login_gateway);
  if (!packet.has_value() || packet->message.ident != mir2::kSmNewIdSuccess) {
    return fail(25);
  }

  for (std::uint64_t session_id = 31; session_id <= 35; ++session_id) {
    if (!bus.post("auth_service",
                  mir2::SessionEvent{mir2::SessionEventKind::packet_received,
                                     "login_gateway",
                                     session_id,
                                     "127.0.0.1:10031",
                                     make_login_packet(session_id, mir2::kCmIdPassword,
                                                       "lockme/badpw", 2026),
                                     {}})) {
      return fail(26);
    }
    packet = wait_for_decoded_packet(login_gateway);
    if (!packet.has_value() || packet->message.ident != mir2::kSmPasswdFail ||
        packet->message.recog != -1) {
      return fail(27);
    }
  }

  if (!bus.post("auth_service",
                mir2::SessionEvent{mir2::SessionEventKind::packet_received,
                                   "login_gateway",
                                   36,
                                   "127.0.0.1:10036",
                                   make_login_packet(36, mir2::kCmIdPassword, "lockme/badpw", 2026),
                                   {}})) {
    return fail(28);
  }
  packet = wait_for_decoded_packet(login_gateway);
  if (!packet.has_value() || packet->message.ident != mir2::kSmPasswdFail ||
      packet->message.recog != -2) {
    return fail(29);
  }

  stop_services();
  std::filesystem::remove_all(temp_root, ignored);
  return 0;
}
