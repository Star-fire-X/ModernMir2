#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

#include "core/local_bus.hpp"
#include "core/metrics_registry.hpp"
#include "core/shutdown_token.hpp"
#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "services/persistence_service.hpp"
#include "services/world_service.hpp"
#include "storage/repository.hpp"

namespace {

using namespace std::chrono_literals;

mir2::CharacterRecord make_character(std::string account_id, std::string character_name,
                                     std::string map_id, std::int32_t x, std::int32_t y) {
  mir2::CharacterRecord record;
  record.account_id = std::move(account_id);
  record.character_name = std::move(character_name);
  record.map_id = std::move(map_id);
  record.x = x;
  record.y = y;
  record.gold = 100000;
  record.ability.level = 1;
  record.ability.hp = 15;
  record.ability.mp = 15;
  record.ability.max_hp = 15;
  record.ability.max_mp = 15;
  record.ability.max_exp = 100;
  record.ability.max_weight = 30;
  record.ability.max_wear_weight = 100;
  record.ability.max_hand_weight = 100;
  return record;
}

bool wait_until(std::function<bool()> predicate, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(20ms);
  }
  return predicate();
}

mir2::SessionEvent make_text_event(std::uint64_t session_id, std::string text) {
  mir2::LegacyPacket packet;
  packet.body.assign(text.begin(), text.end());
  return mir2::SessionEvent{mir2::SessionEventKind::packet_received, "game_gateway", session_id,
                            "127.0.0.1", std::move(packet), {}};
}

mir2::SessionEvent make_say_event(std::uint64_t session_id, std::string text) {
  return mir2::SessionEvent{
      mir2::SessionEventKind::packet_received,
      "game_gateway",
      session_id,
      "127.0.0.1",
      mir2::make_legacy_game_packet(
          session_id, 0, 0, mir2::make_default_message(mir2::kCmSay, 0, 0, 0, 0),
          mir2::legacy_encode_string(text)),
      {}};
}

void copy_schema_tree(const std::filesystem::path& source_root, const std::filesystem::path& root) {
  std::filesystem::create_directories(root / "schema");
  std::filesystem::copy_file(source_root / "schema" / "mir2.sql", root / "schema" / "mir2.sql",
                             std::filesystem::copy_options::overwrite_existing);
}

const mir2::GuildState* find_guild_state(const mir2::GuildCastleSnapshot& snapshot,
                                         std::string_view guild_name) {
  for (const auto& guild : snapshot.guilds) {
    if (guild.guild_name == guild_name) {
      return &guild;
    }
  }
  return nullptr;
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto root =
      std::filesystem::temp_directory_path() / "mir2_world_guild_cross_map_sync_smoke";
  const auto database_path = root / "data" / "mir2.sqlite";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(database_path.parent_path());
  copy_schema_tree(source_root, root);

  {
    mir2::Repository repository(database_path);
    repository.ensure_schema(root / "schema" / "mir2.sql");

    auto lord = make_character("guildsync", "GuildLord", "0", 10, 10);
    lord.guild_name = "DragonSlayers";
    lord.guild_title = "Lord";
    repository.save_character(lord);

    auto ally = make_character("guildsync", "Ally", "1", 20, 20);
    ally.guild_name = "DragonSlayers";
    ally.guild_title = "Member";
    repository.save_character(ally);

    auto visitor = make_character("guildsync", "Visitor", "1", 21, 20);
    repository.save_character(visitor);

    mir2::GuildState guild_state;
    guild_state.guild_name = "DragonSlayers";
    guild_state.lord = "GuildLord";
    guild_state.members = {"GuildLord", "Ally"};
    guild_state.applicants = {"Visitor"};
    repository.save_guild_state(guild_state);

    repository.save_castle_state(
        "Sabuk",
        "{\"owner_guild\":\"DragonSlayers\",\"castle_war_date\":\"2026-05-01 20:00\","
        "\"list_of_war\":\"No active wars.\",\"guild_war_fee\":45000,"
        "\"upgrade_weapon_fee\":15000}");
  }

  mir2::HostConfig config;
  config.runtime.data_dir = "data";
  config.runtime.castle_context_refresh_ms = 60000;
  config.maps.push_back(mir2::MapConfig{"0", "CastleMap", {}, 0, 0, 10, 10});
  config.maps.push_back(mir2::MapConfig{"1", "ForestMap", {}, 0, 0, 20, 20});

  mir2::LocalBus bus;
  mir2::MetricsRegistry metrics;
  mir2::ShutdownToken shutdown;
  static_cast<void>(bus.register_endpoint("log_service", 128));
  static_cast<void>(bus.register_endpoint("game_gateway", 1024));

  mir2::HostContext context;
  context.root_dir = root;
  context.config = config;
  context.bus = &bus;
  context.metrics = &metrics;
  context.shutdown = &shutdown;

  mir2::PersistenceService persistence;
  mir2::WorldService world;
  persistence.start(context);
  world.start(context);

  mir2::Repository verify_repository(database_path);
  const auto cleanup = [&]() {
    world.stop();
    persistence.stop();
    bus.close_all();
    world.join();
    persistence.join();
    std::filesystem::remove_all(root, ec);
  };

  if (!wait_until(
          [&]() {
            const auto snapshot = world.snapshot();
            const auto lord = snapshot.find("castle_lord");
            return lord != snapshot.end() && lord->second == "GuildLord";
          },
          2s)) {
    std::cerr << "initial_castle_snapshot\n";
    cleanup();
    return 1;
  }

  if (!bus.post("world_service", make_text_event(77, "ENTER guildsync GuildLord")) ||
      !bus.post("world_service", make_text_event(78, "ENTER guildsync Ally")) ||
      !bus.post("world_service", make_text_event(79, "ENTER guildsync Visitor"))) {
    std::cerr << "post_enter\n";
    cleanup();
    return 1;
  }

  if (!wait_until(
          [&]() {
            const auto snapshot = world.snapshot();
            const auto sessions = snapshot.find("sessions");
            return sessions != snapshot.end() && sessions->second == "3";
          },
          2s)) {
    std::cerr << "enter_world\n";
    cleanup();
    return 1;
  }
  if (!wait_until(
          [&]() {
            static_cast<void>(bus.post("world_service", make_say_event(77, "@guild approve Visitor")));
            const auto visitor = verify_repository.load_character_by_name("Visitor");
            const auto snapshot = verify_repository.load_guild_castle_snapshot();
            const auto* guild = find_guild_state(snapshot, "DragonSlayers");
            return visitor.has_value() && visitor->guild_name == "DragonSlayers" &&
                   visitor->guild_title == "Member" && guild != nullptr &&
                   guild->members.size() == 3 && guild->applicants.empty();
          },
          2s)) {
    std::cerr << "approve_sync\n";
    cleanup();
    return 1;
  }

  if (!wait_until(
          [&]() {
            static_cast<void>(bus.post("world_service", make_say_event(79, "@guild leave")));
            const auto visitor = verify_repository.load_character_by_name("Visitor");
            const auto snapshot = verify_repository.load_guild_castle_snapshot();
            const auto* guild = find_guild_state(snapshot, "DragonSlayers");
            return visitor.has_value() && visitor->guild_name.empty() && guild != nullptr &&
                   guild->members.size() == 2 &&
                   std::find(guild->members.begin(), guild->members.end(), "Visitor") ==
                       guild->members.end();
          },
          2s)) {
    std::cerr << "remote_leave_sync\n";
    cleanup();
    return 1;
  }

  if (!wait_until(
          [&]() {
            static_cast<void>(bus.post("world_service",
                                       make_say_event(79, "@guild apply DragonSlayers")));
            const auto snapshot = verify_repository.load_guild_castle_snapshot();
            const auto* guild = find_guild_state(snapshot, "DragonSlayers");
            return guild != nullptr &&
                   std::find(guild->applicants.begin(), guild->applicants.end(), "Visitor") !=
                       guild->applicants.end();
          },
          2s)) {
    std::cerr << "remote_apply_sync\n";
    cleanup();
    return 1;
  }

  if (!wait_until(
          [&]() {
            static_cast<void>(bus.post("world_service", make_say_event(77, "@guild approve Visitor")));
            const auto visitor = verify_repository.load_character_by_name("Visitor");
            const auto snapshot = verify_repository.load_guild_castle_snapshot();
            const auto* guild = find_guild_state(snapshot, "DragonSlayers");
            return visitor.has_value() && visitor->guild_name == "DragonSlayers" &&
                   visitor->guild_title == "Member" && guild != nullptr &&
                   guild->members.size() == 3 && guild->applicants.empty();
          },
          2s)) {
    std::cerr << "reapprove_sync\n";
    cleanup();
    return 1;
  }

  if (!wait_until(
          [&]() {
            static_cast<void>(bus.post("world_service", make_say_event(77, "@guild transfer Visitor")));
            const auto visitor = verify_repository.load_character_by_name("Visitor");
            const auto lord = verify_repository.load_character_by_name("GuildLord");
            const auto snapshot = verify_repository.load_guild_castle_snapshot();
            const auto* guild = find_guild_state(snapshot, "DragonSlayers");
            const auto world_snapshot = world.snapshot();
            const auto castle_lord = world_snapshot.find("castle_lord");
            return visitor.has_value() && lord.has_value() && guild != nullptr &&
                   visitor->guild_title == "Lord" && lord->guild_title == "Member" &&
                   guild->lord == "Visitor" && castle_lord != world_snapshot.end() &&
                   castle_lord->second == "Visitor";
          },
          2s)) {
    std::cerr << "transfer_sync\n";
    cleanup();
    return 1;
  }

  if (!wait_until(
          [&]() {
            static_cast<void>(bus.post("world_service",
                                       make_say_event(79, "@guild title GuildLord Captain")));
            const auto lord = verify_repository.load_character_by_name("GuildLord");
            return lord.has_value() && lord->guild_name == "DragonSlayers" &&
                   lord->guild_title == "Captain";
          },
          2s)) {
    std::cerr << "remote_lord_sync\n";
    cleanup();
    return 1;
  }

  cleanup();
  return 0;
}
