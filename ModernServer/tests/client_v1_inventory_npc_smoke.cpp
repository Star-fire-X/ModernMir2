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
  std::optional<T> wait_for_matching(Predicate predicate,
                                     std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
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
                                                    std::uint16_t port) {
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

bool has_item(const mir2::client_v1::EquipmentSnapshot& snapshot, std::int32_t make_index) {
  for (const auto& entry : snapshot.items) {
    if (entry.item.make_index == make_index) {
      return true;
    }
  }
  return false;
}

int fail(const char* stage) {
  std::cerr << "client_v1_inventory_npc_smoke failed at " << stage << '\n';
  return 1;
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto temp_root = std::filesystem::temp_directory_path() / "mir2_client_v1_inventory_npc_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root / "data", ignored);

  mir2::HostConfig config;
  config.runtime.data_dir = temp_root / "data";
  config.runtime.log_dir = temp_root / "logs";
  config.runtime.status_file = temp_root / "runtime" / "status.json";
  config.runtime.default_queue_capacity = 256;
  config.runtime.io_threads = 2;
  config.maps.push_back(mir2::MapConfig{"0", "InventoryMap", {}, 700, 700, 10, 10});
  config.items.push_back(mir2::ItemConfig{1, "Wooden Sword", 3, 100, 5, 1, 1, 1000, 1, 0, 0});
  config.items.push_back(mir2::ItemConfig{2, "Potion", 1, 30, 0, 0, 2, 1, -1, 10, 0});
  config.npcs.push_back(
      mir2::NpcConfig{"merchant_1", "0", "Trader", 11, 10, "", "sell_repair", {1}});
  config.ports.client_v1_game_gateway.address = "127.0.0.1";
  config.ports.client_v1_game_gateway.port = 7115;

  {
    mir2::Repository repository(temp_root / "data" / "mir2.sqlite");
    repository.ensure_schema(source_root / "schema" / "mir2.sql");
    repository.seed_runtime();

    mir2::AccountRecord account;
    account.account_id = "guest";
    account.password = "pass";
    account.display_name = "guest";
    static_cast<void>(repository.create_account(account));

    mir2::CharacterRecord hero;
    hero.account_id = "guest";
    hero.character_name = "V1Hero";
    hero.map_id = "0";
    hero.x = 10;
    hero.y = 10;
    hero.ability.level = 1;
    hero.ability.hp = 15;
    hero.ability.max_hp = 15;
    hero.ability.mp = 15;
    hero.ability.max_mp = 15;
    hero.ability.max_exp = 100;
    hero.ability.max_weight = 30;
    hero.ability.max_wear_weight = 100;
    hero.ability.max_hand_weight = 100;
    hero.bag_items[0].index = 1;
    hero.bag_items[0].make_index = 1001;
    hero.bag_items[0].dura = 600;
    hero.bag_items[0].dura_max = 1000;
    hero.bag_items[1].index = 2;
    hero.bag_items[1].make_index = 1002;
    hero.bag_items[1].dura = 1;
    hero.bag_items[1].dura_max = 1;
    if (!repository.create_character(hero)) {
      return fail("create character");
    }
  }

  mir2::LocalBus bus;
  mir2::MetricsRegistry metrics;
  mir2::ShutdownToken shutdown;
  auto logger = std::make_shared<spdlog::logger>(
      "client_v1_inventory_npc_smoke", std::make_shared<spdlog::sinks::null_sink_mt>());

  mir2::HostContext context;
  context.root_dir = source_root;
  context.config = config;
  context.bus = &bus;
  context.metrics = &metrics;
  context.shutdown = &shutdown;
  context.logger = logger;

  auto admissions = std::make_shared<mir2::ClientV1AdmissionRegistry>();
  const auto token = admissions->issue("guest", "V1Hero");
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
  auto socket = connect_socket(io_context, "127.0.0.1", 7115);
  if (!socket.has_value()) {
    stop_services();
    return fail("connect");
  }

  SocketReader reader(*socket);
  std::uint32_t sequence = 1;
  send_message(*socket, mir2::client_v1::ClientHello{}, sequence);
  send_message(*socket, mir2::client_v1::EnterWorldRequest{token, 1, 1}, sequence);

  const auto enter_result = reader.wait_for_message<mir2::client_v1::EnterWorldResult>();
  if (!enter_result.has_value() || !enter_result->success || enter_result->character_name != "V1Hero") {
    stop_services();
    return fail("enter world");
  }
  if (!reader.wait_for_message<mir2::client_v1::WorldSnapshot>().has_value()) {
    stop_services();
    return fail("world snapshot");
  }
  if (!reader.wait_for_message<mir2::client_v1::EquipmentSnapshot>().has_value()) {
    stop_services();
    return fail("initial equipment");
  }
  const auto bag = reader.wait_for_matching<mir2::client_v1::BagSnapshot>(
      [](const mir2::client_v1::BagSnapshot& snapshot) { return snapshot.items.size() == 2; });
  if (!bag.has_value() || bag->items.front().item.make_index != 1001) {
    stop_services();
    return fail("bag snapshot");
  }

  send_message(*socket, mir2::client_v1::EquipItemRequest{1, 1001, "Wooden Sword"}, sequence);
  const auto equip_remove = reader.wait_for_message<mir2::client_v1::InventoryRemove>();
  if (!equip_remove.has_value() || equip_remove->slot != bag->items.front().slot) {
    stop_services();
    return fail("equip inventory remove");
  }
  const auto equipped = reader.wait_for_matching<mir2::client_v1::EquipmentSnapshot>(
      [](const mir2::client_v1::EquipmentSnapshot& snapshot) { return has_item(snapshot, 1001); });
  if (!equipped.has_value()) {
    stop_services();
    return fail("equip snapshot");
  }

  send_message(*socket, mir2::client_v1::UnequipItemRequest{1, 1001, "Wooden Sword"}, sequence);
  const auto unequip_add = reader.wait_for_matching<mir2::client_v1::InventoryAdd>(
      [](const mir2::client_v1::InventoryAdd& add) { return add.entry.item.make_index == 1001; });
  if (!unequip_add.has_value()) {
    stop_services();
    return fail("unequip inventory add");
  }
  const auto unequipped = reader.wait_for_matching<mir2::client_v1::EquipmentSnapshot>(
      [](const mir2::client_v1::EquipmentSnapshot& snapshot) { return !has_item(snapshot, 1001); });
  if (!unequipped.has_value()) {
    stop_services();
    return fail("unequip snapshot");
  }

  send_message(*socket, mir2::client_v1::DropItemRequest{1001, "Wooden Sword"}, sequence);
  const auto drop_remove = reader.wait_for_matching<mir2::client_v1::InventoryRemove>(
      [&](const mir2::client_v1::InventoryRemove& remove) { return remove.slot == unequip_add->entry.slot; });
  const auto ground_add = reader.wait_for_matching<mir2::client_v1::GroundItemAdd>(
      [](const mir2::client_v1::GroundItemAdd& add) { return add.item.name == "Wooden Sword"; });
  if (!drop_remove.has_value() || !ground_add.has_value()) {
    stop_services();
    return fail("drop item");
  }

  send_message(*socket, mir2::client_v1::NpcClickRequest{1}, sequence);
  const auto dialog = reader.wait_for_matching<mir2::client_v1::NpcDialog>(
      [](const mir2::client_v1::NpcDialog& message) {
        return message.merchant_id == 1 && message.text.find("Trader/") == 0 &&
               message.text.find("<Buy/@buy>") != std::string::npos;
      });
  if (!dialog.has_value()) {
    stop_services();
    return fail("npc dialog");
  }
  send_message(*socket, mir2::client_v1::NpcDialogSelectRequest{1, "@exit"}, sequence);
  const auto close = reader.wait_for_matching<mir2::client_v1::NpcDialogClose>(
      [](const mir2::client_v1::NpcDialogClose& message) { return message.merchant_id == 1; });
  if (!close.has_value()) {
    stop_services();
    return fail("npc close");
  }

  stop_services();
  return 0;
}
