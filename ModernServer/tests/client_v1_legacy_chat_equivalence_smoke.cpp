#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "asio.hpp"
#include "client_v1_test_utils.hpp"
#include "core/local_bus.hpp"
#include "core/metrics_registry.hpp"
#include "core/module.hpp"
#include "core/shutdown_token.hpp"
#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "services/client_v1_admission_registry.hpp"
#include "services/client_v1_game_gateway_service.hpp"
#include "shared/protocol/client_v1/protocol.hpp"
#include "spdlog/logger.h"
#include "spdlog/sinks/null_sink.h"
#include "storage/repository.hpp"

namespace {

constexpr std::uint16_t kGamePort = 7137;

mir2::CharacterRecord make_character(std::string account_id, std::string name,
                                      std::int32_t x) {
  mir2::CharacterRecord character;
  character.account_id = std::move(account_id);
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = x;
  character.y = 10;
  character.dir = 0;
  character.ability.level = 8;
  character.ability.hp = 15;
  character.ability.max_hp = 15;
  character.ability.mp = 15;
  character.ability.max_mp = 15;
  character.ability.max_exp = 100;
  return character;
}

void seed_database(const std::filesystem::path& source_root,
                   const std::filesystem::path& database_path) {
  mir2::Repository repository(database_path);
  repository.ensure_schema(source_root / "schema" / "mir2.sql");
  repository.seed_runtime();

  mir2::AccountRecord alice_account;
  alice_account.account_id = "alice";
  alice_account.password = "pw";
  alice_account.display_name = "Alice";
  static_cast<void>(repository.create_account(alice_account));
  static_cast<void>(repository.create_character(make_character("alice", "Alice", 10)));

  mir2::AccountRecord bob_account;
  bob_account.account_id = "bob";
  bob_account.password = "pw";
  bob_account.display_name = "Bob";
  static_cast<void>(repository.create_account(bob_account));
  static_cast<void>(repository.create_character(make_character("bob", "Bob", 11)));
}

std::optional<asio::ip::tcp::socket> connect_game(asio::io_context& io_context) {
  return mir2::tests::connect_socket(io_context, "127.0.0.1", kGamePort);
}

std::optional<mir2::LogicCommand> wait_for_logic(
    const std::shared_ptr<mir2::LocalBus::Endpoint>& endpoint,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    auto message = endpoint->queue->wait_pop_for(std::chrono::milliseconds(25));
    if (!message.has_value()) {
      continue;
    }
    if (const auto* command = std::get_if<mir2::LogicCommand>(&*message);
        command != nullptr) {
      return *command;
    }
  }
  return std::nullopt;
}

std::optional<mir2::LogicCommand> wait_for_logic_kind(
    const std::shared_ptr<mir2::LocalBus::Endpoint>& endpoint, mir2::LogicCommandKind kind,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    auto command = wait_for_logic(endpoint, std::chrono::milliseconds(250));
    if (command.has_value() && command->kind == kind) {
      return command;
    }
  }
  return std::nullopt;
}

mir2::LegacyPacket make_text_packet(std::uint64_t session_id, std::uint16_t ident,
                                    std::int32_t recog, std::uint16_t color,
                                    std::string_view text) {
  return mir2::make_legacy_game_packet(
      session_id, 0, 0, mir2::make_default_message(ident, recog, color, 0, 1),
      mir2::legacy_encode_string(text));
}

mir2::LegacyPacket make_deal_menu_packet(std::uint64_t session_id,
                                         std::string_view peer_name) {
  return mir2::make_legacy_game_packet(
      session_id, 0, 0, mir2::make_default_message(mir2::kSmDealMenu, 0, 0, 0, 0),
      mir2::legacy_encode_string(peer_name));
}

void post_legacy_packet(mir2::LocalBus& bus, std::uint64_t session_id,
                        mir2::LegacyPacket packet) {
  bus.post("client_v1_game_gateway",
           mir2::SessionEvent{mir2::SessionEventKind::send_packet, "client_v1_game_gateway",
                              session_id, {}, std::move(packet), {}});
}

bool expect_chat_line(mir2::tests::ClientV1SocketReader& reader, std::string_view text,
                      std::uint32_t fore_color, std::uint32_t back_color) {
  const auto line = reader.wait_for_message<mir2::client_v1::ChatLine>();
  return line.has_value() && line->text == text && line->fore_color == fore_color &&
         line->back_color == back_color;
}

int fail(const char* stage) {
  std::cerr << "client_v1_legacy_chat_equivalence_smoke failed at " << stage << '\n';
  return 1;
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto temp_root =
      std::filesystem::temp_directory_path() / "mir2_client_v1_legacy_chat_equivalence_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root / "data", ignored);

  mir2::HostConfig config;
  config.runtime.data_dir = temp_root / "data";
  config.runtime.log_dir = temp_root / "logs";
  config.runtime.status_file = temp_root / "runtime" / "status.json";
  config.runtime.default_queue_capacity = 512;
  config.runtime.io_threads = 2;
  config.ports.client_v1_game_gateway.address = "127.0.0.1";
  config.ports.client_v1_game_gateway.port = kGamePort;

  seed_database(source_root, temp_root / "data" / "mir2.sqlite");

  mir2::LocalBus bus;
  auto world_endpoint = bus.register_endpoint("world_service", 512);
  mir2::MetricsRegistry metrics;
  mir2::ShutdownToken shutdown;
  auto logger = std::make_shared<spdlog::logger>(
      "client_v1_legacy_chat_equivalence_smoke",
      std::make_shared<spdlog::sinks::null_sink_mt>());

  mir2::HostContext context;
  context.root_dir = source_root;
  context.config = config;
  context.bus = &bus;
  context.metrics = &metrics;
  context.shutdown = &shutdown;
  context.logger = logger;

  auto admissions = std::make_shared<mir2::ClientV1AdmissionRegistry>();
  mir2::ClientV1GameGatewayService game_gateway(admissions);
  game_gateway.start(context);

  const auto stop_services = [&] {
    game_gateway.stop();
    game_gateway.join();
    std::filesystem::remove_all(temp_root, ignored);
  };

  asio::io_context io_context;
  auto alice_socket = connect_game(io_context);
  auto bob_socket = connect_game(io_context);
  if (!alice_socket.has_value() || !bob_socket.has_value()) {
    stop_services();
    return fail("connect");
  }

  mir2::tests::ClientV1SocketReader alice_reader(*alice_socket);
  mir2::tests::ClientV1SocketReader bob_reader(*bob_socket);
  std::uint32_t alice_sequence = 1;
  std::uint32_t bob_sequence = 1;
  mir2::tests::send_client_v1_message(*alice_socket, mir2::client_v1::ClientHello{},
                                      alice_sequence);
  mir2::tests::send_client_v1_message(*bob_socket, mir2::client_v1::ClientHello{},
                                      bob_sequence);

  const auto alice_token = admissions->issue("alice", "Alice");
  const auto bob_token = admissions->issue("bob", "Bob");
  mir2::tests::send_client_v1_message(
      *alice_socket, mir2::client_v1::EnterWorldRequest{alice_token, 1, 1},
      alice_sequence);
  mir2::tests::send_client_v1_message(
      *bob_socket, mir2::client_v1::EnterWorldRequest{bob_token, 1, 1}, bob_sequence);

  const auto first_enter =
      wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::enter_world);
  const auto second_enter =
      wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::enter_world);
  const mir2::LogicCommand* alice_enter = nullptr;
  const mir2::LogicCommand* bob_enter = nullptr;
  if (first_enter.has_value() && first_enter->character_name == "Alice") {
    alice_enter = &*first_enter;
  } else if (first_enter.has_value() && first_enter->character_name == "Bob") {
    bob_enter = &*first_enter;
  }
  if (second_enter.has_value() && second_enter->character_name == "Alice") {
    alice_enter = &*second_enter;
  } else if (second_enter.has_value() && second_enter->character_name == "Bob") {
    bob_enter = &*second_enter;
  }
  if (alice_enter == nullptr || bob_enter == nullptr) {
    stop_services();
    return fail("enter world");
  }
  const auto alice_session_id = alice_enter->session_id;

  post_legacy_packet(bus, alice_session_id,
                     make_text_packet(alice_session_id, mir2::kSmHear, 1234,
                                      mir2::make_word(0, 255), "Alice: hi"));
  const auto actor_say = alice_reader.wait_for_message<mir2::client_v1::ActorSay>();
  if (!actor_say.has_value() || actor_say->actor_id != 1234 ||
      actor_say->text != "Alice: hi" || actor_say->fore_color != 0 ||
      actor_say->back_color != 255 ||
      alice_reader.wait_for_message<mir2::client_v1::SysMessage>(
                      std::chrono::milliseconds(100))
          .has_value()) {
    stop_services();
    return fail("normal chat actor say");
  }

  post_legacy_packet(bus, alice_session_id,
                     make_text_packet(alice_session_id, mir2::kSmHear, 0,
                                      mir2::make_word(0, 151), "(!)Alice:hi"));
  if (!expect_chat_line(alice_reader, "(!)Alice:hi", 0, 151) ||
      alice_reader.wait_for_message<mir2::client_v1::ActorSay>(
                      std::chrono::milliseconds(100))
          .has_value()) {
    stop_services();
    return fail("shout chat line");
  }

  post_legacy_packet(bus, alice_session_id,
                     make_text_packet(alice_session_id, mir2::kSmWhisper, 1234,
                                      mir2::make_word(252, 255), "Alice=> hi"));
  if (!expect_chat_line(alice_reader, "Alice=> hi", 252, 255)) {
    stop_services();
    return fail("whisper chat line");
  }

  post_legacy_packet(bus, alice_session_id,
                     make_text_packet(alice_session_id, mir2::kSmGuildMessage, 1234,
                                      mir2::make_word(212, 255), "Alice:hi"));
  if (!expect_chat_line(alice_reader, "Alice:hi", 212, 255)) {
    stop_services();
    return fail("guild chat line");
  }

  post_legacy_packet(bus, alice_session_id,
                     make_text_packet(alice_session_id, mir2::kSmSysMessage, 1234,
                                      mir2::make_word(255, 56), "system text"));
  if (!expect_chat_line(alice_reader, "system text", 255, 56)) {
    stop_services();
    return fail("system chat line");
  }

  post_legacy_packet(bus, alice_session_id,
                     make_text_packet(alice_session_id, mir2::kSmGroupMessage, 1234,
                                      mir2::make_word(196, 255), "-Alice: hi"));
  if (!expect_chat_line(alice_reader, "-Alice: hi", 196, 255)) {
    stop_services();
    return fail("group defensive chat line");
  }

  post_legacy_packet(bus, alice_session_id,
                     make_text_packet(alice_session_id, mir2::kSmCry, 0,
                                      mir2::make_word(0, 151), "cry text"));
  if (!expect_chat_line(alice_reader, "cry text", 0, 151)) {
    stop_services();
    return fail("cry defensive chat line");
  }

  post_legacy_packet(bus, alice_session_id,
                     make_text_packet(alice_session_id, mir2::kSmWhisper, 1234,
                                      mir2::make_word(252, 255), "fifo1"));
  post_legacy_packet(bus, alice_session_id,
                     make_text_packet(alice_session_id, mir2::kSmGuildMessage, 1234,
                                      mir2::make_word(212, 255), "fifo2"));
  post_legacy_packet(bus, alice_session_id,
                     make_text_packet(alice_session_id, mir2::kSmSysMessage, 1234,
                                      mir2::make_word(255, 56), "fifo3"));
  const auto fifo1 = alice_reader.wait_for_message<mir2::client_v1::ChatLine>();
  const auto fifo2 = alice_reader.wait_for_message<mir2::client_v1::ChatLine>();
  const auto fifo3 = alice_reader.wait_for_message<mir2::client_v1::ChatLine>();
  if (!fifo1.has_value() || !fifo2.has_value() || !fifo3.has_value() ||
      fifo1->text != "fifo1" || fifo2->text != "fifo2" || fifo3->text != "fifo3") {
    stop_services();
    return fail("chat fifo");
  }

  std::string gbk_like = "Alice: ";
  gbk_like.push_back(static_cast<char>(0xB0));
  gbk_like.push_back(static_cast<char>(0xA1));
  gbk_like += " hi";
  post_legacy_packet(bus, alice_session_id,
                     make_text_packet(alice_session_id, mir2::kSmWhisper, 1234,
                                      mir2::make_word(252, 255), gbk_like));
  if (!expect_chat_line(alice_reader, gbk_like, 252, 255)) {
    stop_services();
    return fail("gbk-like bytes");
  }

  post_legacy_packet(bus, alice_session_id,
                     make_text_packet(alice_session_id, mir2::kSmHear, 0,
                                      mir2::make_word(255, 0), "system notice"));
  const auto system_notice = alice_reader.wait_for_message<mir2::client_v1::SysMessage>();
  if (!system_notice.has_value() || system_notice->text != "system notice" ||
      alice_reader.wait_for_message<mir2::client_v1::ChatLine>(
                      std::chrono::milliseconds(100))
          .has_value()) {
    stop_services();
    return fail("non-chat hear remains sys message");
  }

  mir2::tests::send_client_v1_message(*alice_socket,
                                      mir2::client_v1::TradeTryRequest{"Bob"},
                                      alice_sequence);
  const auto trade_try =
      wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::trade_try);
  if (!trade_try.has_value() || trade_try->session_id != alice_session_id ||
      trade_try->text != "Bob") {
    stop_services();
    return fail("trade try command");
  }
  post_legacy_packet(bus, alice_session_id, make_deal_menu_packet(alice_session_id, "Bob"));
  if (!alice_reader.wait_for_matching<mir2::client_v1::TradeState>(
          [](const auto& state) { return state.visible && state.remote_name == "Bob"; })) {
    stop_services();
    return fail("trade menu visible");
  }

  post_legacy_packet(bus, alice_session_id,
                     make_text_packet(alice_session_id, mir2::kSmHear, 0,
                                      mir2::make_word(255, 0), "Trade cancelled."));
  const auto trade_cancelled = alice_reader.wait_for_message<mir2::client_v1::SysMessage>();
  const auto trade_closed = alice_reader.wait_for_message<mir2::client_v1::TradeState>();
  if (!trade_cancelled.has_value() || trade_cancelled->text != "Trade cancelled." ||
      !trade_closed.has_value() || trade_closed->visible) {
    stop_services();
    return fail("trade cancel hear side effect");
  }

  if (bob_reader.wait_for_message<mir2::client_v1::ChatLine>(
          std::chrono::milliseconds(100)).has_value()) {
    stop_services();
    return fail("unexpected bob chat");
  }

  stop_services();
  return 0;
}
