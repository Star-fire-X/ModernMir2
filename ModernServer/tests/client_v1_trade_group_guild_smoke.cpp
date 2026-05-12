#include <array>
#include <algorithm>
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
#include "services/world_service.hpp"
#include "shared/protocol/client_v1/protocol.hpp"
#include "spdlog/logger.h"
#include "spdlog/sinks/null_sink.h"
#include "storage/repository.hpp"

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

  template <typename T, typename Predicate>
  std::optional<T> wait_for_matching(
      Predicate predicate, std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      auto message = wait_for_message<T>(std::chrono::milliseconds(250));
      if (message.has_value() && predicate(*message)) {
        return message;
      }
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

bool has_member(const mir2::client_v1::GroupState& state, const std::string& name) {
  return std::find(state.members.begin(), state.members.end(), name) != state.members.end();
}

bool has_guild_member(const mir2::client_v1::GuildState& state, const std::string& name) {
  return std::any_of(state.members.begin(), state.members.end(),
                     [&](const mir2::client_v1::GuildMemberState& member) {
                       return member.name == name;
                     });
}

int fail(const char* stage) {
  std::cerr << "client_v1_trade_group_guild_smoke failed at " << stage << '\n';
  return 1;
}

mir2::LegacyUserItem make_item(const std::uint16_t index, const std::int32_t make_index) {
  mir2::LegacyUserItem item;
  item.index = index;
  item.make_index = make_index;
  item.dura = 1000;
  item.dura_max = 1000;
  return item;
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto temp_root = std::filesystem::temp_directory_path() / "mir2_client_v1_trade_group_guild_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root / "data", ignored);

  mir2::HostConfig config;
  config.runtime.data_dir = temp_root / "data";
  config.runtime.log_dir = temp_root / "logs";
  config.runtime.status_file = temp_root / "runtime" / "status.json";
  config.runtime.default_queue_capacity = 256;
  config.runtime.io_threads = 2;
  config.maps.push_back(mir2::MapConfig{"0", "SocialMap", {}, 700, 700, 10, 10});
  config.items.push_back(mir2::ItemConfig{1, "Ruby", 1, 40, 0, 2, 1, 1000, 10, 0, 0});
  config.items.push_back(mir2::ItemConfig{2, "Sapphire", 1, 41, 0, 3, 1, 1000, 10, 0, 0});
  config.ports.client_v1_game_gateway.address = "127.0.0.1";
  config.ports.client_v1_game_gateway.port = 7116;

  {
    mir2::Repository repository(temp_root / "data" / "mir2.sqlite");
    repository.ensure_schema(source_root / "schema" / "mir2.sql");
    repository.seed_runtime();

    for (const auto* account_id : {"guest_a", "guest_b"}) {
      mir2::AccountRecord account;
      account.account_id = account_id;
      account.password = "pass";
      account.display_name = account_id;
      static_cast<void>(repository.create_account(account));
    }

    mir2::CharacterRecord hero_a;
    hero_a.account_id = "guest_a";
    hero_a.character_name = "HeroA";
    hero_a.map_id = "0";
    hero_a.x = 10;
    hero_a.y = 10;
    hero_a.dir = 2;
    hero_a.ability.level = 30;
    hero_a.ability.hp = 50;
    hero_a.ability.max_hp = 50;
    hero_a.ability.mp = 50;
    hero_a.ability.max_mp = 50;
    hero_a.ability.max_exp = 100;
    hero_a.ability.max_weight = 100;
    hero_a.ability.max_wear_weight = 100;
    hero_a.ability.max_hand_weight = 100;
    hero_a.gold = 100;
    hero_a.guild_name = "Guild";
    hero_a.guild_title = "Leader";
    hero_a.bag_items[0] = make_item(1, 1001);
    if (!repository.create_character(hero_a)) {
      return fail("create HeroA");
    }

    mir2::CharacterRecord hero_b = hero_a;
    hero_b.account_id = "guest_b";
    hero_b.character_name = "HeroB";
    hero_b.x = 11;
    hero_b.dir = 6;
    hero_b.gold = 50;
    hero_b.guild_name.clear();
    hero_b.guild_title.clear();
    hero_b.bag_items = {};
    hero_b.bag_items[0] = make_item(2, 2001);
    if (!repository.create_character(hero_b)) {
      return fail("create HeroB");
    }
  }

  mir2::LocalBus bus;
  mir2::MetricsRegistry metrics;
  mir2::ShutdownToken shutdown;
  auto logger = std::make_shared<spdlog::logger>(
      "client_v1_trade_group_guild_smoke", std::make_shared<spdlog::sinks::null_sink_mt>());

  mir2::HostContext context;
  context.root_dir = source_root;
  context.config = config;
  context.bus = &bus;
  context.metrics = &metrics;
  context.shutdown = &shutdown;
  context.logger = logger;

  auto admissions = std::make_shared<mir2::ClientV1AdmissionRegistry>();
  const auto token_a = admissions->issue("guest_a", "HeroA");
  const auto token_b = admissions->issue("guest_b", "HeroB");
  mir2::WorldService world_service;
  mir2::ClientV1GameGatewayService game_gateway(admissions);
  world_service.start(context);
  game_gateway.start(context);

  const auto stop_services = [&] {
    game_gateway.stop();
    world_service.stop();
    game_gateway.join();
    world_service.join();
    std::filesystem::remove_all(temp_root, ignored);
  };

  asio::io_context io_context;
  auto socket_a = connect_socket(io_context, "127.0.0.1", 7116);
  auto socket_b = connect_socket(io_context, "127.0.0.1", 7116);
  if (!socket_a.has_value() || !socket_b.has_value()) {
    stop_services();
    return fail("connect");
  }

  SocketReader reader_a(*socket_a);
  SocketReader reader_b(*socket_b);
  std::uint32_t seq_a = 1;
  std::uint32_t seq_b = 1;
  send_message(*socket_a, mir2::client_v1::ClientHello{}, seq_a);
  send_message(*socket_b, mir2::client_v1::ClientHello{}, seq_b);
  send_message(*socket_a, mir2::client_v1::EnterWorldRequest{token_a, 1, 1}, seq_a);
  send_message(*socket_b, mir2::client_v1::EnterWorldRequest{token_b, 1, 1}, seq_b);

  if (!reader_a.wait_for_matching<mir2::client_v1::EnterWorldResult>(
          [](const auto& result) { return result.character_name == "HeroA"; }) ||
      !reader_b.wait_for_matching<mir2::client_v1::EnterWorldResult>(
          [](const auto& result) { return result.character_name == "HeroB"; }) ||
      !reader_a.wait_for_message<mir2::client_v1::BagSnapshot>() ||
      !reader_b.wait_for_message<mir2::client_v1::BagSnapshot>()) {
    stop_services();
    return fail("enter world");
  }

  send_message(*socket_b, mir2::client_v1::GroupModeRequest{true}, seq_b);
  if (!reader_b.wait_for_matching<mir2::client_v1::GroupState>(
          [](const auto& state) { return state.visible && state.allow_group; })) {
    stop_services();
    return fail("group mode");
  }
  send_message(*socket_a, mir2::client_v1::GroupCreateRequest{"HeroB"}, seq_a);
  const auto group_a = reader_a.wait_for_matching<mir2::client_v1::GroupState>(
      [](const auto& state) { return has_member(state, "HeroA") && has_member(state, "HeroB"); });
  const auto group_b = reader_b.wait_for_matching<mir2::client_v1::GroupState>(
      [](const auto& state) { return has_member(state, "HeroA") && has_member(state, "HeroB"); });
  if (!group_a.has_value() || !group_b.has_value()) {
    stop_services();
    return fail("group create");
  }
  send_message(*socket_a, mir2::client_v1::GroupRemoveMemberRequest{"HeroB"}, seq_a);
  if (!reader_a.wait_for_matching<mir2::client_v1::GroupState>(
          [](const auto& state) { return !state.visible; }) ||
      !reader_b.wait_for_matching<mir2::client_v1::GroupState>(
          [](const auto& state) { return !state.visible; })) {
    stop_services();
    return fail("group remove");
  }

  send_message(*socket_a, mir2::client_v1::TradeTryRequest{"HeroB"}, seq_a);
  if (!reader_a.wait_for_matching<mir2::client_v1::TradeState>(
          [](const auto& state) { return state.visible && state.remote_name == "HeroB"; }) ||
      !reader_b.wait_for_matching<mir2::client_v1::TradeState>(
          [](const auto& state) { return state.visible && state.remote_name == "HeroA"; })) {
    stop_services();
    return fail("trade open");
  }
  send_message(*socket_a, mir2::client_v1::TradeAddItemRequest{1001, "Ruby"}, seq_a);
  if (!reader_a.wait_for_matching<mir2::client_v1::TradeState>(
          [](const auto& state) {
            return !state.local_items.empty() && state.local_items.front().item.make_index == 1001;
          }) ||
      !reader_b.wait_for_matching<mir2::client_v1::TradeState>(
          [](const auto& state) {
            return !state.remote_items.empty() && state.remote_items.front().item.make_index == 1001;
          }) ||
      !reader_a.wait_for_matching<mir2::client_v1::InventoryRemove>(
          [](const auto& remove) { return remove.slot >= 0; })) {
    stop_services();
    return fail("trade add item");
  }
  send_message(*socket_b, mir2::client_v1::TradeSetGoldRequest{7}, seq_b);
  if (!reader_a.wait_for_matching<mir2::client_v1::TradeState>(
          [](const auto& state) { return state.remote_gold == 7; }) ||
      !reader_b.wait_for_matching<mir2::client_v1::TradeState>(
          [](const auto& state) { return state.local_gold == 7; })) {
    stop_services();
    return fail("trade gold");
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));
  send_message(*socket_a, mir2::client_v1::TradeAcceptRequest{}, seq_a);
  send_message(*socket_b, mir2::client_v1::TradeAcceptRequest{}, seq_b);
  if (!reader_b.wait_for_matching<mir2::client_v1::InventoryAdd>(
          [](const auto& add) { return add.entry.item.make_index == 1001; }) ||
      !reader_a.wait_for_matching<mir2::client_v1::SelfAbility>(
          [](const auto& ability) { return ability.gold == 107; }) ||
      !reader_b.wait_for_matching<mir2::client_v1::SelfAbility>(
          [](const auto& ability) { return ability.gold == 43; }) ||
      !reader_a.wait_for_matching<mir2::client_v1::TradeState>(
          [](const auto& state) { return !state.visible; }) ||
      !reader_b.wait_for_matching<mir2::client_v1::TradeState>(
          [](const auto& state) { return !state.visible; })) {
    stop_services();
    return fail("trade commit");
  }

  send_message(*socket_a, mir2::client_v1::GuildOpenRequest{}, seq_a);
  if (!reader_a.wait_for_matching<mir2::client_v1::GuildState>(
          [](const auto& state) { return state.guild_name == "Guild" && has_guild_member(state, "HeroA"); })) {
    stop_services();
    return fail("guild open");
  }
  send_message(*socket_a, mir2::client_v1::GuildAddMemberRequest{"HeroB"}, seq_a);
  if (!reader_a.wait_for_matching<mir2::client_v1::GuildState>(
          [](const auto& state) { return has_guild_member(state, "HeroB"); })) {
    stop_services();
    return fail("guild add");
  }
  send_message(*socket_b, mir2::client_v1::GuildOpenRequest{}, seq_b);
  if (!reader_b.wait_for_matching<mir2::client_v1::GuildState>(
          [](const auto& state) { return state.guild_name == "Guild"; })) {
    stop_services();
    return fail("guild target open");
  }
  send_message(*socket_a, mir2::client_v1::GuildUpdateNoticeRequest{"New notice"}, seq_a);
  if (!reader_a.wait_for_matching<mir2::client_v1::GuildState>(
          [](const auto& state) { return state.notice == "New notice"; })) {
    stop_services();
    return fail("guild notice");
  }
  send_message(*socket_a, mir2::client_v1::GuildUpdateGradeRequest{"Leader/Member"}, seq_a);
  if (!reader_a.wait_for_matching<mir2::client_v1::GuildState>(
          [](const auto& state) { return state.ranks.size() == 2; })) {
    stop_services();
    return fail("guild ranks");
  }
  send_message(*socket_a, mir2::client_v1::GuildRemoveMemberRequest{"HeroB"}, seq_a);
  if (!reader_a.wait_for_matching<mir2::client_v1::GuildState>(
          [](const auto& state) { return !has_guild_member(state, "HeroB"); }) ||
      !reader_b.wait_for_matching<mir2::client_v1::GuildState>(
          [](const auto& state) { return !state.visible; })) {
    stop_services();
    return fail("guild remove");
  }

  stop_services();
  return 0;
}
