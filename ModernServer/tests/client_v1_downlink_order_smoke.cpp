#include <cassert>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "asio.hpp"
#include "client_v1_test_utils.hpp"
#include "core/local_bus.hpp"
#include "core/metrics_registry.hpp"
#include "core/module.hpp"
#include "core/shutdown_token.hpp"
#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "services/client_v1_admission_registry.hpp"
#include "services/client_v1_game_gateway_service.hpp"
#include "shared/protocol/client_v1/protocol.hpp"
#include "spdlog/logger.h"
#include "spdlog/sinks/null_sink.h"

namespace {

constexpr std::uint64_t kSessionId = 902;

mir2::LegacyPacket make_actor_walk_packet(std::uint64_t session_id = kSessionId) {
  return mir2::make_legacy_game_packet(
      session_id, 0, 0, mir2::make_default_message(mir2::kSmWalk, 42, 12, 13, 2));
}

mir2::LegacyPacket make_item_show_packet(std::uint64_t session_id = kSessionId) {
  return mir2::make_legacy_game_packet(
      session_id, 0, 0, mir2::make_default_message(mir2::kSmItemShow, 901, 11, 10, 7),
      mir2::legacy_encode_string("Gold"));
}

mir2::LegacyPacket make_item_hide_packet(std::uint64_t session_id = kSessionId) {
  return mir2::make_legacy_game_packet(
      session_id, 0, 0, mir2::make_default_message(mir2::kSmItemHide, 901, 11, 10, 0));
}

mir2::LegacyPacket make_sys_message_packet(std::uint64_t session_id = kSessionId) {
  return mir2::make_legacy_game_packet(
      session_id, 0, 0,
      mir2::make_default_message(mir2::kSmSysMessage, 0, mir2::make_word(5, 1), 0, 0),
      mir2::legacy_encode_string("server notice"));
}

template <typename T>
const T& require_message(const std::vector<mir2::client_v1::Message>& messages,
                         std::size_t index) {
  assert(index < messages.size());
  const auto* value = std::get_if<T>(&messages[index]);
  assert(value != nullptr);
  return *value;
}

class OrderedSocketReader {
 public:
  explicit OrderedSocketReader(asio::ip::tcp::socket& socket) : socket_(socket) {
    socket_.non_blocking(true);
  }

  std::optional<mir2::client_v1::Message> wait_for_next(
      std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
    if (!pending_.empty()) {
      auto message = pending_.front();
      pending_.erase(pending_.begin());
      return message;
    }

    std::array<std::uint8_t, 4096> read_buffer{};
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      std::error_code error;
      const auto bytes_read = socket_.read_some(asio::buffer(read_buffer), error);
      if (!error) {
        buffer_.insert(buffer_.end(), read_buffer.begin(), read_buffer.begin() + bytes_read);
        auto frames = mir2::client_v1::drain_frames(buffer_);
        for (const auto& frame : frames) {
          const auto decoded = mir2::client_v1::decode_any(frame);
          if (!decoded.has_value()) {
            return std::nullopt;
          }
          pending_.push_back(*decoded);
        }
      } else if (error != asio::error::would_block && error != asio::error::try_again) {
        return std::nullopt;
      }

      if (!pending_.empty()) {
        auto message = pending_.front();
        pending_.erase(pending_.begin());
        return message;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return std::nullopt;
  }

 private:
  asio::ip::tcp::socket& socket_;
  std::vector<std::uint8_t> buffer_{};
  std::vector<mir2::client_v1::Message> pending_{};
};

std::optional<mir2::SessionEvent> wait_for_connected(
    const std::shared_ptr<mir2::LocalBus::Endpoint>& endpoint) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (std::chrono::steady_clock::now() < deadline) {
    auto message = endpoint->queue->wait_pop_for(std::chrono::milliseconds(25));
    if (!message.has_value()) {
      continue;
    }
    if (const auto* event = std::get_if<mir2::SessionEvent>(&*message);
        event != nullptr && event->kind == mir2::SessionEventKind::connected) {
      return *event;
    }
  }
  return std::nullopt;
}

template <typename T>
const T& require_wire_message(const mir2::client_v1::Message& message) {
  const auto* value = std::get_if<T>(&message);
  assert(value != nullptr);
  return *value;
}

void check_socket_downlink_order() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto temp_root =
      std::filesystem::temp_directory_path() / "mir2_client_v1_downlink_order_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root / "data", ignored);

  mir2::HostConfig config;
  config.runtime.data_dir = temp_root / "data";
  config.runtime.log_dir = temp_root / "logs";
  config.runtime.status_file = temp_root / "runtime" / "status.json";
  config.runtime.default_queue_capacity = 256;
  config.runtime.io_threads = 1;
  config.ports.client_v1_game_gateway.address = "127.0.0.1";
  config.ports.client_v1_game_gateway.port = 7121;

  mir2::LocalBus bus;
  auto world_endpoint = bus.register_endpoint("world_service", 256);
  mir2::MetricsRegistry metrics;
  mir2::ShutdownToken shutdown;
  auto logger = std::make_shared<spdlog::logger>(
      "client_v1_downlink_order_smoke", std::make_shared<spdlog::sinks::null_sink_mt>());

  mir2::HostContext context;
  context.root_dir = source_root;
  context.config = config;
  context.bus = &bus;
  context.metrics = &metrics;
  context.shutdown = &shutdown;
  context.logger = logger;

  auto admissions = std::make_shared<mir2::ClientV1AdmissionRegistry>();
  mir2::ClientV1GameGatewayService service(admissions);
  service.start(context);

  const auto stop_service = [&] {
    service.stop();
    service.join();
    std::filesystem::remove_all(temp_root, ignored);
  };

  asio::io_context io_context;
  auto socket = mir2::tests::connect_socket(io_context, "127.0.0.1", 7121);
  assert(socket.has_value());
  OrderedSocketReader reader(*socket);

  const auto connected = wait_for_connected(world_endpoint);
  assert(connected.has_value());
  const auto session_id = connected->session_id;

  assert(bus.post("client_v1_game_gateway",
                  mir2::SessionEvent{mir2::SessionEventKind::send_packet,
                                     "client_v1_game_gateway", session_id, {},
                                     mir2::make_legacy_raw_packet(session_id, "+GOOD/123"),
                                     {}}));
  assert(bus.post("client_v1_game_gateway",
                  mir2::SessionEvent{mir2::SessionEventKind::send_packet,
                                     "client_v1_game_gateway", session_id, {},
                                     make_item_show_packet(session_id), {}}));
  assert(bus.post("client_v1_game_gateway",
                  mir2::SessionEvent{mir2::SessionEventKind::send_packet,
                                     "client_v1_game_gateway", session_id, {},
                                     make_item_hide_packet(session_id), {}}));
  assert(bus.post("client_v1_game_gateway",
                  mir2::SessionEvent{mir2::SessionEventKind::send_packet,
                                     "client_v1_game_gateway", session_id, {},
                                     make_sys_message_packet(session_id), {}}));

  auto message = reader.wait_for_next();
  assert(message.has_value());
  const auto& ack = require_wire_message<mir2::client_v1::ActionAck>(*message);
  assert(ack.ok);
  assert(ack.server_time_ms == 123);

  message = reader.wait_for_next();
  assert(message.has_value());
  const auto& add = require_wire_message<mir2::client_v1::GroundItemAdd>(*message);
  assert(add.item.object_id == 901);

  message = reader.wait_for_next();
  assert(message.has_value());
  const auto& remove = require_wire_message<mir2::client_v1::GroundItemRemove>(*message);
  assert(remove.object_id == 901);

  message = reader.wait_for_next();
  assert(message.has_value());
  const auto& chat = require_wire_message<mir2::client_v1::ChatLine>(*message);
  assert(chat.text == "server notice");

  stop_service();
}

}  // namespace

int main() {
  auto admissions = std::make_shared<mir2::ClientV1AdmissionRegistry>();
  mir2::ClientV1GameGatewayService service(admissions);
  service.seed_session_for_test(kSessionId);

  std::vector<mir2::client_v1::Message> messages;
  service.translate_legacy_packet_for_test(kSessionId, make_actor_walk_packet(), messages);
  assert(messages.size() == 2);

  const auto& upsert = require_message<mir2::client_v1::ActorUpsert>(messages, 0);
  assert(upsert.actor.actor_id == 42);
  assert(upsert.actor.x == 12);
  assert(upsert.actor.y == 13);
  assert(upsert.actor.dir == 2);

  const auto& walk = require_message<mir2::client_v1::ActorAction>(messages, 1);
  assert(walk.actor_id == 42);
  assert(walk.kind == mir2::client_v1::ActorActionKind::walk);
  assert(walk.legacy_ident == mir2::kSmWalk);
  assert(walk.x == 12);
  assert(walk.y == 13);
  assert(walk.dir == 2);

  messages.clear();
  service.translate_legacy_packet_for_test(
      kSessionId, mir2::make_legacy_raw_packet(kSessionId, "+GOOD/123"), messages);
  service.translate_legacy_packet_for_test(kSessionId, make_item_show_packet(), messages);
  service.translate_legacy_packet_for_test(kSessionId, make_item_hide_packet(), messages);
  service.translate_legacy_packet_for_test(kSessionId, make_sys_message_packet(), messages);
  assert(messages.size() == 4);

  const auto& ack = require_message<mir2::client_v1::ActionAck>(messages, 0);
  assert(ack.ok);
  assert(ack.server_time_ms == 123);

  const auto& add = require_message<mir2::client_v1::GroundItemAdd>(messages, 1);
  assert(add.item.object_id == 901);
  assert(add.item.x == 11);
  assert(add.item.y == 10);
  assert(add.item.looks == 7);
  assert(add.item.name == "Gold");

  const auto& remove = require_message<mir2::client_v1::GroundItemRemove>(messages, 2);
  assert(remove.object_id == 901);
  assert(remove.x == 11);
  assert(remove.y == 10);

  const auto& chat = require_message<mir2::client_v1::ChatLine>(messages, 3);
  assert(chat.text == "server notice");
  assert(chat.fore_color == 5);
  assert(chat.back_color == 1);

  check_socket_downlink_order();

  return 0;
}
