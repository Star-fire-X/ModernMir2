#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

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
#include "shared/legacy/action_ids.hpp"
#include "shared/protocol/client_v1/protocol.hpp"
#include "spdlog/logger.h"
#include "spdlog/sinks/null_sink.h"
#include "storage/repository.hpp"

namespace {

mir2::CharacterRecord make_character() {
  mir2::CharacterRecord character;
  character.account_id = "bridge";
  character.character_name = "BridgeHero";
  character.map_id = "0";
  character.x = 10;
  character.y = 10;
  character.dir = 0;
  character.ability.level = 1;
  character.ability.hp = 15;
  character.ability.max_hp = 15;
  character.ability.mp = 15;
  character.ability.max_mp = 15;
  character.ability.max_exp = 100;
  character.gold = 500;
  return character;
}

mir2::CharacterRecord make_trade_target() {
  auto character = make_character();
  character.account_id = "bridge_target";
  character.character_name = "Other";
  character.x = 11;
  return character;
}

void seed_database(const std::filesystem::path& source_root,
                   const std::filesystem::path& database_path) {
  mir2::Repository repository(database_path);
  repository.ensure_schema(source_root / "schema" / "mir2.sql");
  repository.seed_runtime();

  mir2::AccountRecord account;
  account.account_id = "bridge";
  account.password = "pw";
  account.display_name = "Bridge";
  static_cast<void>(repository.create_account(account));
  static_cast<void>(repository.create_character(make_character()));

  mir2::AccountRecord target_account;
  target_account.account_id = "bridge_target";
  target_account.password = "pw";
  target_account.display_name = "BridgeTarget";
  static_cast<void>(repository.create_account(target_account));
  static_cast<void>(repository.create_character(make_trade_target()));
}

mir2::LegacyPacket make_deal_menu_packet(std::uint64_t session_id,
                                         std::string_view peer_name) {
  return mir2::make_legacy_game_packet(
      session_id, 0, 0, mir2::make_default_message(mir2::kSmDealMenu, 0, 0, 0, 0),
      mir2::legacy_encode_string(peer_name));
}

std::optional<asio::ip::tcp::socket> connect_game(asio::io_context& io_context) {
  return mir2::tests::connect_socket(io_context, "127.0.0.1", 7119);
}

template <typename T>
void send_client_v1_message_with_sequence(asio::ip::tcp::socket& socket, const T& message,
                                          std::uint32_t sequence) {
  const auto bytes = mir2::client_v1::encode_frame(mir2::client_v1::make_frame(message, sequence));
  asio::write(socket, asio::buffer(bytes));
}

template <typename T>
std::vector<std::uint8_t> encode_client_v1_message_with_sequence(const T& message,
                                                                 std::uint32_t sequence) {
  return mir2::client_v1::encode_frame(mir2::client_v1::make_frame(message, sequence));
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

bool wait_for_no_logic(const std::shared_ptr<mir2::LocalBus::Endpoint>& endpoint,
                       std::chrono::milliseconds timeout = std::chrono::milliseconds(250)) {
  return !wait_for_logic(endpoint, timeout).has_value();
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

int fail(const char* stage) {
  std::cerr << "client_v1_game_command_bridge_smoke failed at " << stage << '\n';
  return 1;
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto temp_root =
      std::filesystem::temp_directory_path() / "mir2_client_v1_game_command_bridge_smoke";
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
  config.ports.client_v1_game_gateway.port = 7119;

  seed_database(source_root, temp_root / "data" / "mir2.sqlite");

  mir2::LocalBus bus;
  auto world_endpoint = bus.register_endpoint("world_service", 512);
  mir2::MetricsRegistry metrics;
  mir2::ShutdownToken shutdown;
  auto logger = std::make_shared<spdlog::logger>(
      "client_v1_game_command_bridge_smoke", std::make_shared<spdlog::sinks::null_sink_mt>());

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
  auto socket = connect_game(io_context);
  if (!socket.has_value()) {
    stop_services();
    return fail("connect");
  }

  mir2::tests::ClientV1SocketReader reader(*socket);
  std::uint32_t sequence = 1;
  mir2::tests::send_client_v1_message(*socket, mir2::client_v1::ClientHello{}, sequence);
  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::MoveIntent{11, 10, mir2::client_v1::MoveMode::walk},
      sequence);
  if (!wait_for_no_logic(world_endpoint)) {
    stop_services();
    return fail("pre-enter command ignored");
  }

  const auto token = admissions->issue("bridge", "BridgeHero");
  const auto enter_sequence = sequence;
  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::EnterWorldRequest{token, 1, 1}, sequence);
  const auto enter_command =
      wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::enter_world);
  if (!enter_command.has_value() || enter_command->account_id != "bridge" ||
      enter_command->character_name != "BridgeHero" || enter_command->map_id != "0" ||
      enter_command->x != 10 || enter_command->y != 10 ||
      enter_command->session_seq != enter_sequence) {
    stop_services();
    return fail("enter world command");
  }
  const auto session_id = enter_command->session_id;

  auto target_socket = connect_game(io_context);
  if (!target_socket.has_value()) {
    stop_services();
    return fail("target connect");
  }
  mir2::tests::ClientV1SocketReader target_reader(*target_socket);
  std::uint32_t target_sequence = 1;
  mir2::tests::send_client_v1_message(*target_socket, mir2::client_v1::ClientHello{},
                                      target_sequence);
  const auto target_token = admissions->issue("bridge_target", "Other");
  mir2::tests::send_client_v1_message(
      *target_socket, mir2::client_v1::EnterWorldRequest{target_token, 1, 1},
      target_sequence);
  const auto target_enter_command =
      wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::enter_world);
  if (!target_enter_command.has_value() || target_enter_command->account_id != "bridge_target" ||
      target_enter_command->character_name != "Other") {
    stop_services();
    return fail("target enter world command");
  }

  const auto batch_walk_sequence = sequence++;
  const auto batch_attack_sequence = sequence++;
  const auto batch_pickup_sequence = sequence++;
  auto batch = encode_client_v1_message_with_sequence(
      mir2::client_v1::MoveIntent{11, 10, mir2::client_v1::MoveMode::walk},
      batch_walk_sequence);
  auto attack_frame = encode_client_v1_message_with_sequence(
      mir2::client_v1::ActionIntent{mir2::client_v1::WorldActionKind::attack, 12, 10, 4,
                                    77, 0},
      batch_attack_sequence);
  batch.insert(batch.end(), attack_frame.begin(), attack_frame.end());
  auto pickup_frame = encode_client_v1_message_with_sequence(
      mir2::client_v1::PickupIntent{14, 15}, batch_pickup_sequence);
  batch.insert(batch.end(), pickup_frame.begin(), pickup_frame.end());
  asio::write(*socket, asio::buffer(batch));

  auto first_batch_command = wait_for_logic(world_endpoint);
  auto second_batch_command = wait_for_logic(world_endpoint);
  auto third_batch_command = wait_for_logic(world_endpoint);
  if (!first_batch_command.has_value() || !second_batch_command.has_value() ||
      !third_batch_command.has_value() ||
      first_batch_command->kind != mir2::LogicCommandKind::walk ||
      second_batch_command->kind != mir2::LogicCommandKind::attack ||
      third_batch_command->kind != mir2::LogicCommandKind::pickup_item ||
      first_batch_command->session_seq != batch_walk_sequence ||
      second_batch_command->session_seq != batch_attack_sequence ||
      third_batch_command->session_seq != batch_pickup_sequence ||
      first_batch_command->session_id != session_id ||
      second_batch_command->session_id != session_id ||
      third_batch_command->session_id != session_id) {
    stop_services();
    return fail("batched frame fifo");
  }

  const auto split_walk_sequence = sequence++;
  const auto split_pickup_sequence = sequence++;
  auto split_walk_frame = encode_client_v1_message_with_sequence(
      mir2::client_v1::MoveIntent{12, 10, mir2::client_v1::MoveMode::walk},
      split_walk_sequence);
  auto split_pickup_frame = encode_client_v1_message_with_sequence(
      mir2::client_v1::PickupIntent{13, 14}, split_pickup_sequence);
  const auto split_point = split_walk_frame.size() / 2U;
  asio::write(*socket, asio::buffer(split_walk_frame.data(), split_point));
  if (!wait_for_no_logic(world_endpoint)) {
    stop_services();
    return fail("partial frame held");
  }
  asio::write(*socket, asio::buffer(split_walk_frame.data() + split_point,
                                    split_walk_frame.size() - split_point));
  asio::write(*socket, asio::buffer(split_pickup_frame));

  auto first_split_command = wait_for_logic(world_endpoint);
  auto second_split_command = wait_for_logic(world_endpoint);
  if (!first_split_command.has_value() || !second_split_command.has_value() ||
      first_split_command->kind != mir2::LogicCommandKind::walk ||
      second_split_command->kind != mir2::LogicCommandKind::pickup_item ||
      first_split_command->session_seq != split_walk_sequence ||
      second_split_command->session_seq != split_pickup_sequence ||
      first_split_command->session_id != session_id ||
      second_split_command->session_id != session_id) {
    stop_services();
    return fail("split frame fifo");
  }

  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::MoveIntent{11, 10, mir2::client_v1::MoveMode::walk},
      sequence);
  auto command = wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::walk);
  if (!command.has_value() || command->session_id != session_id || command->x != 11 ||
      command->y != 10) {
    stop_services();
    return fail("move command");
  }

  mir2::tests::send_client_v1_message(
      *socket,
      mir2::client_v1::ActionIntent{mir2::client_v1::WorldActionKind::turn, 10, 10, 3, 0, 0},
      sequence);
  command = wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::turn);
  if (!command.has_value() || command->session_id != session_id || command->dir != 3) {
    stop_services();
    return fail("turn command");
  }

  mir2::tests::send_client_v1_message(
      *socket,
      mir2::client_v1::ActionIntent{mir2::client_v1::WorldActionKind::attack, 12, 10, 4,
                                    77, 0},
      sequence);
  command = wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::attack);
  if (!command.has_value() || command->session_id != session_id ||
      command->target_actor_id != 77) {
    stop_services();
    return fail("attack command");
  }
  if (command->game_message.ident != mir2::legacy::kCmHit) {
    stop_services();
    return fail("attack command cm hit");
  }

  mir2::tests::send_client_v1_message(
      *socket,
      mir2::client_v1::ActionIntent{mir2::client_v1::WorldActionKind::attack, 12, 10, 4,
                                    77, mir2::legacy::kSmPowerHit},
      sequence);
  command = wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::attack);
  if (!command.has_value() || command->game_message.ident != mir2::legacy::kCmPowerHit) {
    stop_services();
    return fail("attack command sm power");
  }

  mir2::tests::send_client_v1_message(
      *socket,
      mir2::client_v1::ActionIntent{mir2::client_v1::WorldActionKind::attack, 12, 10, 4,
                                    77, mir2::legacy::kCmFireHit},
      sequence);
  command = wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::attack);
  if (!command.has_value() || command->game_message.ident != mir2::legacy::kCmFireHit) {
    stop_services();
    return fail("attack command cm fire");
  }

  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::SpellIntent{13, 14, 5, 77, 9}, sequence);
  command = wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::spell);
  if (!command.has_value() || command->session_id != session_id || command->text != "9") {
    stop_services();
    return fail("spell command");
  }

  mir2::tests::send_client_v1_message(*socket, mir2::client_v1::PickupIntent{14, 15},
                                      sequence);
  command = wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::pickup_item);
  if (!command.has_value() || command->session_id != session_id || command->x != 14 ||
      command->y != 15) {
    stop_services();
    return fail("pickup command");
  }

  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::UseItemIntent{1001, 3, "Potion"}, sequence);
  command = wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::eat_item);
  if (!command.has_value() || command->session_id != session_id ||
      command->item_make_index != 1001 || command->item_slot != 3 ||
      command->text != "Potion") {
    stop_services();
    return fail("use item command");
  }

  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::EquipItemRequest{1, 1002, "Sword"}, sequence);
  command = wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::take_on_item);
  if (!command.has_value() || command->session_id != session_id ||
      command->item_make_index != 1002 || command->item_slot != 1 ||
      command->text != "Sword") {
    stop_services();
    return fail("equip command");
  }

  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::UnequipItemRequest{1, 1002, "Sword"}, sequence);
  command = wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::take_off_item);
  if (!command.has_value() || command->session_id != session_id ||
      command->item_make_index != 1002 || command->item_slot != 1) {
    stop_services();
    return fail("unequip command");
  }

  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::DropItemRequest{1003, "Ore"}, sequence);
  command = wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::drop_item);
  if (!command.has_value() || command->session_id != session_id ||
      command->item_make_index != 1003 || command->text != "Ore") {
    stop_services();
    return fail("drop item command");
  }

  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::DropGoldRequest{250}, sequence);
  command = wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::drop_gold);
  if (!command.has_value() || command->session_id != session_id ||
      command->amount != 250) {
    stop_services();
    return fail("drop gold command");
  }

  mir2::tests::send_client_v1_message(*socket, mir2::client_v1::ReviveRequest{}, sequence);
  command = wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::revive);
  if (!command.has_value() || command->session_id != session_id) {
    stop_services();
    return fail("revive command");
  }

  const auto replay_sequence = sequence;
  send_client_v1_message_with_sequence(*socket, mir2::client_v1::NpcClickRequest{42},
                                       replay_sequence);
  send_client_v1_message_with_sequence(*socket, mir2::client_v1::NpcClickRequest{42},
                                       replay_sequence);
  sequence = replay_sequence + 1;
  command = wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::click_npc);
  if (!command.has_value() || command->session_id != session_id ||
      command->target_actor_id != 42 || command->session_seq != replay_sequence) {
    stop_services();
    return fail("npc click command");
  }
  if (!wait_for_no_logic(world_endpoint)) {
    stop_services();
    return fail("duplicate client frame sequence ignored");
  }

  const auto select_sequence = sequence;
  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::NpcDialogSelectRequest{0, "@buy"}, sequence);
  command = wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::merchant_select);
  if (!command.has_value() || command->session_id != session_id ||
      command->target_actor_id != 42 || command->text != "@buy" ||
      command->session_seq != select_sequence) {
    stop_services();
    return fail("npc select command");
  }

  const auto buy_sequence = sequence;
  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::MerchantBuyRequest{0, 555, "Drug"}, sequence);
  command = wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::buy_item);
  if (!command.has_value() || command->session_id != session_id ||
      command->target_actor_id != 42 || command->item_make_index != 555 ||
      command->text != "Drug" || command->session_seq != buy_sequence) {
    stop_services();
    return fail("merchant buy command");
  }

  const auto sell_sequence = sequence;
  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::MerchantSellRequest{0, 1004, "Ruby"}, sequence);
  command = wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::sell_item);
  if (!command.has_value() || command->session_id != session_id ||
      command->target_actor_id != 42 || command->item_make_index != 1004 ||
      command->text != "Ruby" || command->session_seq != sell_sequence) {
    stop_services();
    return fail("merchant sell command");
  }

  const auto sell_price_sequence = sequence;
  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::MerchantSellPriceRequest{0, 1004, "Ruby"}, sequence);
  command = wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::query_sell_price);
  if (!command.has_value() || command->session_id != session_id ||
      command->target_actor_id != 42 || command->item_make_index != 1004 ||
      command->text != "Ruby" || command->session_seq != sell_price_sequence) {
    stop_services();
    return fail("merchant sell price command");
  }

  const auto repair_price_sequence = sequence;
  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::MerchantRepairPriceRequest{0, 1005, "Sword"}, sequence);
  command = wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::query_repair_cost);
  if (!command.has_value() || command->session_id != session_id ||
      command->target_actor_id != 42 || command->item_make_index != 1005 ||
      command->text != "Sword" || command->session_seq != repair_price_sequence) {
    stop_services();
    return fail("merchant repair price command");
  }

  const auto repair_sequence = sequence;
  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::MerchantRepairRequest{0, 1005, "Sword"}, sequence);
  command = wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::repair_item);
  if (!command.has_value() || command->session_id != session_id ||
      command->target_actor_id != 42 || command->item_make_index != 1005 ||
      command->text != "Sword" || command->session_seq != repair_sequence) {
    stop_services();
    return fail("merchant repair command");
  }

  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::StorageDepositRequest{0, 1006, "Ring"}, sequence);
  command = wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::storage_item);
  if (!command.has_value() || command->session_id != session_id ||
      command->target_actor_id != 42 || command->item_make_index != 1006 ||
      command->text != "Ring") {
    stop_services();
    return fail("storage deposit command");
  }

  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::StorageWithdrawRequest{0, 1007, "Ring"}, sequence);
  command =
      wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::take_back_storage_item);
  if (!command.has_value() || command->session_id != session_id ||
      command->target_actor_id != 42 || command->item_make_index != 1007 ||
      command->text != "Ring") {
    stop_services();
    return fail("storage withdraw command");
  }

  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::TradeTryRequest{"Other"}, sequence);
  command = wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::trade_try);
  if (!command.has_value() || command->session_id != session_id ||
      command->text != "Other") {
    stop_services();
    return fail("trade try command");
  }
  bus.post("client_v1_game_gateway",
           mir2::SessionEvent{mir2::SessionEventKind::send_packet, "client_v1_game_gateway",
                              session_id, {}, make_deal_menu_packet(session_id, "Other"),
                              {}});
  if (!reader.wait_for_matching<mir2::client_v1::TradeState>(
          [](const auto& state) { return state.visible && state.remote_name == "Other"; })) {
    stop_services();
    return fail("trade menu state");
  }

  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::TradeAddItemRequest{1008, "Gem"}, sequence);
  command = wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::trade_add_item);
  if (!command.has_value() || command->session_id != session_id ||
      command->item_make_index != 1008 || command->text != "Gem") {
    stop_services();
    return fail("trade add item command");
  }

  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::TradeRemoveItemRequest{1008, "Gem"}, sequence);
  command = wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::trade_remove_item);
  if (!command.has_value() || command->session_id != session_id ||
      command->item_make_index != 1008 || command->text != "Gem") {
    stop_services();
    return fail("trade remove item command");
  }

  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::TradeSetGoldRequest{123}, sequence);
  command = wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::trade_set_gold);
  if (!command.has_value() || command->session_id != session_id ||
      command->amount != 123) {
    stop_services();
    return fail("trade set gold command");
  }

  mir2::tests::send_client_v1_message(*socket, mir2::client_v1::TradeAcceptRequest{},
                                      sequence);
  command = wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::trade_accept);
  if (!command.has_value() || command->session_id != session_id) {
    stop_services();
    return fail("trade accept command");
  }

  mir2::tests::send_client_v1_message(*socket, mir2::client_v1::TradeCancelRequest{},
                                      sequence);
  command = wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::trade_cancel);
  if (!command.has_value() || command->session_id != session_id) {
    stop_services();
    return fail("trade cancel command");
  }

  mir2::tests::send_client_v1_message(*socket, mir2::client_v1::ChatSend{"hello"}, sequence);
  command = wait_for_logic_kind(world_endpoint, mir2::LogicCommandKind::say);
  if (!command.has_value() || command->session_id != session_id ||
      command->text != "hello") {
    stop_services();
    return fail("chat command");
  }

  mir2::tests::send_client_v1_message(*socket, mir2::client_v1::Ping{12345}, sequence);
  const auto pong = reader.wait_for_message<mir2::client_v1::Pong>();
  if (!pong.has_value() || pong->client_time_ms != 12345 || pong->server_time_ms == 0) {
    stop_services();
    return fail("ping pong");
  }

  stop_services();
  return 0;
}
