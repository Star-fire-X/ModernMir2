#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "asio.hpp"
#include "client_v1_test_utils.hpp"
#include "config/models.hpp"
#include "core/local_bus.hpp"
#include "core/metrics_registry.hpp"
#include "core/shutdown_token.hpp"
#include "importer/legacy_character_importer.hpp"
#include "legacy_character_fixture.hpp"
#include "services/client_v1_admission_registry.hpp"
#include "services/client_v1_game_gateway_service.hpp"
#include "services/persistence_service.hpp"
#include "services/world_service.hpp"
#include "shared/legacy/action_ids.hpp"
#include "shared/protocol/client_v1/protocol.hpp"
#include "spdlog/logger.h"
#include "spdlog/sinks/null_sink.h"
#include "storage/repository.hpp"

namespace {

int fail(const char* stage) {
  std::cerr << "client_v1_world_min_loop_smoke failed at " << stage << '\n';
  return 1;
}

mir2::MagicConfig make_fireball() {
  mir2::MagicConfig magic;
  magic.id = 1;
  magic.name = "Fireball";
  magic.mp_cost = 4;
  magic.power = 8;
  magic.legacy.legacy_present = true;
  magic.legacy.effect_type = 1;
  magic.legacy.effect = 1;
  magic.legacy.spell = 4;
  magic.legacy.min_power = 5;
  magic.legacy.max_power = 10;
  magic.legacy.job = 2;
  magic.legacy.max_train_level = 3;
  magic.legacy.delay_time = 100;
  return magic;
}

mir2::HostConfig make_config(const std::filesystem::path& temp_root) {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 1;
  config.runtime.data_dir = temp_root / "data";
  config.runtime.log_dir = temp_root / "logs";
  config.runtime.status_file = temp_root / "runtime" / "status.json";
  config.runtime.default_queue_capacity = 256;
  config.runtime.io_threads = 2;
  config.runtime.castle_context_refresh_ms = 0;
  config.maps.push_back(mir2::MapConfig{"0", "LoopMap", {}, 700, 700, 330, 270});
  config.spawns.push_back(
      mir2::SpawnConfig{"0", "monster", "Scarecrow", 124, 234, 30000, 1, 60, 1, 0, 0, 20});
  mir2::ItemConfig sword{1, "Sword", 1, 100, 5, 0, 10, 1000, 1, 0, 0};
  sword.dc = mir2::make_word(10, 10);
  config.items.push_back(sword);
  config.magics.push_back(make_fireball());
  config.ports.client_v1_game_gateway.address = "127.0.0.1";
  config.ports.client_v1_game_gateway.port = 7125;
  return config;
}

bool has_bag_item(const mir2::CharacterRecord& character, std::int32_t make_index) {
  return std::any_of(character.bag_items.begin(), character.bag_items.end(),
                     [&](const mir2::LegacyUserItem& item) {
                       return item.make_index == make_index;
                     });
}

bool has_equipped_item(const mir2::CharacterRecord& character, std::int32_t make_index) {
  return std::any_of(character.equipped_items.begin(), character.equipped_items.end(),
                     [&](const mir2::LegacyUserItem& item) {
                       return item.make_index == make_index;
                     });
}

bool has_magic(const mir2::CharacterRecord& character, std::uint16_t magic_id) {
  return std::any_of(character.magics.begin(), character.magics.end(),
                     [&](const mir2::LegacyUseMagicInfo& magic) {
                       return magic.magic_id == magic_id;
                     });
}

std::optional<mir2::CharacterRecord> wait_for_saved_character(
    const std::filesystem::path& source_root, const std::filesystem::path& database_path) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
  while (std::chrono::steady_clock::now() < deadline) {
    try {
      mir2::Repository repository(database_path);
      repository.ensure_schema(source_root / "schema" / "mir2.sql");
      const auto character = repository.load_character_by_name("LegacyHero");
      if (character.has_value() && character->x == 123 && character->y == 235 &&
          has_bag_item(*character, 1002) && has_equipped_item(*character, 1001) &&
          has_magic(*character, 1) && character->ability.mp < 44) {
        return character;
      }
    } catch (...) {
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return std::nullopt;
}

bool has_snapshot_item(const mir2::client_v1::BagSnapshot& snapshot, std::int32_t make_index) {
  return std::any_of(snapshot.items.begin(), snapshot.items.end(),
                     [&](const mir2::client_v1::ItemSlotState& item) {
                       return item.item.make_index == make_index;
                     });
}

bool has_snapshot_item(const mir2::client_v1::EquipmentSnapshot& snapshot,
                       std::int32_t make_index) {
  return std::any_of(snapshot.items.begin(), snapshot.items.end(),
                     [&](const mir2::client_v1::ItemSlotState& item) {
                       return item.item.make_index == make_index;
                     });
}

bool has_snapshot_magic(const mir2::client_v1::MagicList& list, std::uint16_t magic_id) {
  return std::any_of(list.magics.begin(), list.magics.end(),
                     [&](const mir2::client_v1::MagicEntry& magic) {
                       return magic.magic_id == magic_id;
                     });
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto temp_root =
      std::filesystem::temp_directory_path() / "mir2_client_v1_world_min_loop_smoke";
  const auto database_path = temp_root / "data" / "mir2.sqlite";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root / "data", ignored);

  const auto config = make_config(temp_root);
  const auto fixture = mir2::tests::write_legacy_character_fixture(temp_root / "legacy");
  {
    mir2::Repository repository(database_path);
    repository.ensure_schema(source_root / "schema" / "mir2.sql");
    mir2::LegacyCharacterImporter importer;
    mir2::LegacyCharacterImportOptions options;
    options.hum_db_path = fixture.hum_db;
    options.mir_db_path = fixture.mir_db;
    options.config = &config;
    const auto report = importer.import_characters(options, repository);
    if (report.imported != 1 || report.failed != 0) {
      return fail("import");
    }
  }

  mir2::LocalBus bus;
  mir2::MetricsRegistry metrics;
  mir2::ShutdownToken shutdown;
  auto logger = std::make_shared<spdlog::logger>(
      "client_v1_world_min_loop_smoke", std::make_shared<spdlog::sinks::null_sink_mt>());

  mir2::HostContext context;
  context.root_dir = source_root;
  context.config = config;
  context.bus = &bus;
  context.metrics = &metrics;
  context.shutdown = &shutdown;
  context.logger = logger;

  auto admissions = std::make_shared<mir2::ClientV1AdmissionRegistry>();
  mir2::PersistenceService persistence_service;
  mir2::WorldService world_service;
  mir2::ClientV1GameGatewayService game_gateway(admissions);
  persistence_service.start(context);
  world_service.start(context);
  game_gateway.start(context);

  const auto stop_services = [&] {
    game_gateway.stop();
    world_service.stop();
    persistence_service.stop();
    game_gateway.join();
    world_service.join();
    persistence_service.join();
    std::filesystem::remove_all(temp_root, ignored);
  };

  asio::io_context io_context;
  const auto token = admissions->issue("legacyacct", "LegacyHero");
  auto game_socket = mir2::tests::connect_socket(io_context, "127.0.0.1", 7125);
  if (!game_socket.has_value()) {
    stop_services();
    return fail("game connect");
  }
  mir2::tests::ClientV1SocketReader reader(*game_socket);
  std::uint32_t sequence = 1;
  mir2::tests::send_client_v1_message(*game_socket, mir2::client_v1::ClientHello{}, sequence);
  mir2::tests::send_client_v1_message(
      *game_socket, mir2::client_v1::EnterWorldRequest{token, 1, 1}, sequence);

  const auto enter_world = reader.wait_for_message<mir2::client_v1::EnterWorldResult>();
  if (!enter_world.has_value() || !enter_world->success ||
      enter_world->character_name != "LegacyHero" || enter_world->x != 123 ||
      enter_world->y != 234) {
    stop_services();
    return fail("enter world");
  }
  if (!reader.wait_for_message<mir2::client_v1::WorldSnapshot>().has_value()) {
    stop_services();
    return fail("world snapshot");
  }
  const auto monster = reader.wait_for_matching<mir2::client_v1::ActorUpsert>(
      [](const mir2::client_v1::ActorUpsert& upsert) {
        return upsert.actor.actor_type == mir2::client_v1::ActorType::monster &&
               upsert.actor.name == "Scarecrow";
      },
      std::chrono::seconds(5));
  if (!monster.has_value()) {
    stop_services();
    return fail("monster upsert");
  }

  const auto equipment = reader.wait_for_matching<mir2::client_v1::EquipmentSnapshot>(
      [](const mir2::client_v1::EquipmentSnapshot& snapshot) {
        return has_snapshot_item(snapshot, 1001);
      });
  const auto bag = reader.wait_for_matching<mir2::client_v1::BagSnapshot>(
      [](const mir2::client_v1::BagSnapshot& snapshot) { return has_snapshot_item(snapshot, 1002); });
  const auto magic = reader.wait_for_matching<mir2::client_v1::MagicList>(
      [](const mir2::client_v1::MagicList& list) { return has_snapshot_magic(list, 1); });
  if (!equipment.has_value() || !bag.has_value() || !magic.has_value()) {
    stop_services();
    return fail("initial imported payloads");
  }

  mir2::tests::send_client_v1_message(
      *game_socket, mir2::client_v1::MoveIntent{123, 235, mir2::client_v1::MoveMode::walk},
      sequence);
  const auto move_ack = reader.wait_for_matching<mir2::client_v1::ActionAck>(
      [](const mir2::client_v1::ActionAck& ack) { return ack.ok; });
  const auto move_delta = reader.wait_for_matching<mir2::client_v1::ActorStateDelta>(
      [&](const mir2::client_v1::ActorStateDelta& delta) {
        return delta.actor_id == enter_world->self_actor_id && delta.x == 123 && delta.y == 235;
      });
  if (!move_ack.has_value() || !move_delta.has_value()) {
    stop_services();
    return fail("move");
  }

  mir2::client_v1::ActionIntent attack;
  attack.kind = mir2::client_v1::WorldActionKind::attack;
  attack.x = monster->actor.x;
  attack.y = monster->actor.y;
  attack.dir = 1;
  attack.target_actor_id = monster->actor.actor_id;
  mir2::tests::send_client_v1_message(*game_socket, attack, sequence);
  const auto attack_ack = reader.wait_for_matching<mir2::client_v1::ActionAck>(
      [](const mir2::client_v1::ActionAck& ack) { return ack.ok; });
  const auto attack_action = reader.wait_for_matching<mir2::client_v1::ActorAction>(
      [&](const mir2::client_v1::ActorAction& action) {
        return action.actor_id == enter_world->self_actor_id &&
               action.kind == mir2::client_v1::ActorActionKind::hit;
      });
  const auto attack_vitals = reader.wait_for_matching<mir2::client_v1::ActorVitals>(
      [&](const mir2::client_v1::ActorVitals& vitals) {
        return vitals.actor_id == monster->actor.actor_id && vitals.damage > 0;
      });
  if (!attack_ack.has_value() || !attack_action.has_value() || !attack_vitals.has_value()) {
    stop_services();
    return fail("attack");
  }
  if (attack_action->legacy_ident != mir2::legacy::kSmHit) {
    stop_services();
    return fail("attack legacy ident");
  }

  mir2::tests::send_client_v1_message(
      *game_socket,
      mir2::client_v1::SpellIntent{monster->actor.x, monster->actor.y, 1,
                                   monster->actor.actor_id, 1},
      sequence);
  const auto spell_ack = reader.wait_for_matching<mir2::client_v1::ActionAck>(
      [](const mir2::client_v1::ActionAck& ack) { return ack.ok; },
      std::chrono::seconds(5));
  const auto spell_vitals = reader.wait_for_matching<mir2::client_v1::ActorVitals>(
      [&](const mir2::client_v1::ActorVitals& vitals) {
        return vitals.actor_id == monster->actor.actor_id && vitals.damage > 0 && vitals.magic;
      },
      std::chrono::seconds(5));
  if (!spell_ack.has_value() || !spell_vitals.has_value()) {
    stop_services();
    return fail("spell");
  }

  mir2::tests::send_client_v1_message(*game_socket,
                                      mir2::client_v1::DropItemRequest{1002, "Sword"},
                                      sequence);
  const auto drop_remove = reader.wait_for_matching<mir2::client_v1::InventoryRemove>(
      [](const mir2::client_v1::InventoryRemove& remove) { return remove.slot >= 0; });
  const auto ground_add = reader.wait_for_matching<mir2::client_v1::GroundItemAdd>(
      [](const mir2::client_v1::GroundItemAdd& add) { return add.item.name == "Sword"; });
  if (!drop_remove.has_value() || !ground_add.has_value()) {
    stop_services();
    return fail("drop");
  }

  mir2::tests::send_client_v1_message(
      *game_socket, mir2::client_v1::PickupIntent{ground_add->item.x, ground_add->item.y},
      sequence);
  const auto pickup_add = reader.wait_for_matching<mir2::client_v1::InventoryAdd>(
      [](const mir2::client_v1::InventoryAdd& add) {
        return add.entry.item.make_index == 1002;
      });
  const auto ground_remove = reader.wait_for_matching<mir2::client_v1::GroundItemRemove>(
      [&](const mir2::client_v1::GroundItemRemove& remove) {
        return remove.object_id == ground_add->item.object_id;
      });
  if (!pickup_add.has_value() || !ground_remove.has_value()) {
    stop_services();
    return fail("pickup");
  }

  game_socket->close(ignored);
  const auto saved = wait_for_saved_character(source_root, database_path);
  if (!saved.has_value()) {
    stop_services();
    return fail("disconnect save");
  }

  const auto reconnect_token = admissions->issue("legacyacct", "LegacyHero");
  auto reconnect_socket = mir2::tests::connect_socket(io_context, "127.0.0.1", 7125);
  if (!reconnect_socket.has_value()) {
    stop_services();
    return fail("reconnect");
  }
  mir2::tests::ClientV1SocketReader reconnect_reader(*reconnect_socket);
  std::uint32_t reconnect_sequence = 1;
  mir2::tests::send_client_v1_message(*reconnect_socket, mir2::client_v1::ClientHello{},
                                      reconnect_sequence);
  mir2::tests::send_client_v1_message(
      *reconnect_socket,
      mir2::client_v1::EnterWorldRequest{reconnect_token, 1, 1},
      reconnect_sequence);
  const auto restored = reconnect_reader.wait_for_message<mir2::client_v1::EnterWorldResult>();
  if (!restored.has_value() || !restored->success || restored->x != 123 || restored->y != 235) {
    stop_services();
    return fail("reconnect enter");
  }
  const auto restored_equipment =
      reconnect_reader.wait_for_matching<mir2::client_v1::EquipmentSnapshot>(
          [](const mir2::client_v1::EquipmentSnapshot& snapshot) {
            return has_snapshot_item(snapshot, 1001);
          });
  const auto restored_bag = reconnect_reader.wait_for_matching<mir2::client_v1::BagSnapshot>(
      [](const mir2::client_v1::BagSnapshot& snapshot) { return has_snapshot_item(snapshot, 1002); });
  const auto restored_magic = reconnect_reader.wait_for_matching<mir2::client_v1::MagicList>(
      [](const mir2::client_v1::MagicList& list) { return has_snapshot_magic(list, 1); });
  if (!restored_equipment.has_value() || !restored_bag.has_value() ||
      !restored_magic.has_value()) {
    stop_services();
    return fail("reconnect payloads");
  }

  stop_services();
  return 0;
}
