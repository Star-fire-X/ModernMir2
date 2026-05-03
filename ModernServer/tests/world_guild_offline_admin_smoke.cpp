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

mir2::CharacterRecord make_character(std::string account_id, std::string character_name) {
  mir2::CharacterRecord record;
  record.account_id = std::move(account_id);
  record.character_name = std::move(character_name);
  record.map_id = "0";
  record.x = 10;
  record.y = 10;
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

std::uint64_t snapshot_refresh_count(mir2::WorldService& world) {
  const auto snapshot = world.snapshot();
  const auto refreshes = snapshot.find("castle_refreshes");
  if (refreshes == snapshot.end()) {
    return 0;
  }
  return static_cast<std::uint64_t>(std::stoull(refreshes->second));
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto root =
      std::filesystem::temp_directory_path() / "mir2_world_guild_offline_admin_smoke";
  const auto database_path = root / "data" / "mir2.sqlite";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(database_path.parent_path());
  copy_schema_tree(source_root, root);

  {
    mir2::Repository repository(database_path);
    repository.ensure_schema(root / "schema" / "mir2.sql");

    auto hero = make_character("guildhost", "GuildLord");
    hero.guild_name = "DragonSlayers";
    hero.guild_title = "Lord";
    repository.save_character(hero);

    auto ally = make_character("guildhost", "Ally");
    ally.guild_name = "DragonSlayers";
    ally.guild_title = "Member";
    repository.save_character(ally);

    auto visitor = make_character("guildhost", "Visitor");
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

  mir2::LocalBus bus;
  mir2::MetricsRegistry metrics;
  mir2::ShutdownToken shutdown;
  static_cast<void>(bus.register_endpoint("log_service", 128));
  static_cast<void>(bus.register_endpoint("game_gateway", 512));

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
            const auto owner = snapshot.find("castle_owner_guild");
            const auto lord = snapshot.find("castle_lord");
            const auto refreshes = snapshot.find("castle_refreshes");
            return owner != snapshot.end() && lord != snapshot.end() &&
                   refreshes != snapshot.end() && owner->second == "DragonSlayers" &&
                   lord->second == "GuildLord" && std::stoull(refreshes->second) >= 1;
          },
          2s)) {
    std::cerr << "initial_snapshot_refresh\n";
    cleanup();
    return 1;
  }

  if (!bus.post("world_service", make_text_event(77, "ENTER guildhost GuildLord"))) {
    std::cerr << "post_enter\n";
    cleanup();
    return 1;
  }

  if (!wait_until(
          [&]() {
            const auto snapshot = world.snapshot();
            const auto sessions = snapshot.find("sessions");
            return sessions != snapshot.end() && sessions->second == "1";
          },
          2s)) {
    std::cerr << "enter_world\n";
    cleanup();
    return 1;
  }

  auto refresh_before = snapshot_refresh_count(world);
  if (!bus.post("world_service", make_say_event(77, "@guild approve Visitor"))) {
    std::cerr << "post_approve\n";
    cleanup();
    return 1;
  }

  if (!wait_until(
          [&]() {
            const auto visitor = verify_repository.load_character_by_name("Visitor");
            const auto snapshot = verify_repository.load_guild_castle_snapshot();
            const auto* guild = find_guild_state(snapshot, "DragonSlayers");
            return visitor.has_value() && visitor->guild_name == "DragonSlayers" &&
                   visitor->guild_title == "Member" && guild != nullptr &&
                   guild->members.size() == 3 && guild->applicants.empty() &&
                   snapshot_refresh_count(world) > refresh_before;
          },
          2s)) {
    const auto visitor = verify_repository.load_character_by_name("Visitor");
    const auto snapshot = verify_repository.load_guild_castle_snapshot();
    const auto* guild = find_guild_state(snapshot, "DragonSlayers");
    const auto world_snapshot = world.snapshot();
    const auto offline_results = world_snapshot.find("offline_guild_results");
    const auto offline_routes = world_snapshot.find("offline_guild_routes");
    const auto offline_errors = world_snapshot.find("offline_guild_errors");
    std::cerr << "offline_approve"
              << " visitor_guild=" << (visitor.has_value() ? visitor->guild_name : "<missing>")
              << " visitor_title=" << (visitor.has_value() ? visitor->guild_title : "<missing>")
              << " guild_members=" << (guild != nullptr ? std::to_string(guild->members.size()) : "<missing>")
              << " guild_applicants="
              << (guild != nullptr ? std::to_string(guild->applicants.size()) : "<missing>")
              << " refresh_before=" << refresh_before
              << " refresh_now=" << snapshot_refresh_count(world)
              << " persist_handled=" << persistence.snapshot().at("handled_requests")
              << " persist_last_kind=" << persistence.snapshot().at("last_request_kind")
              << " persist_last_reply_to=" << persistence.snapshot().at("last_request_reply_to")
              << " persist_last_id="
              << persistence.snapshot().at("last_request_id")
              << " offline_results="
              << (offline_results != world_snapshot.end() ? offline_results->second : "<missing>")
              << " offline_routes="
              << (offline_routes != world_snapshot.end() ? offline_routes->second : "<missing>")
              << " offline_errors="
              << (offline_errors != world_snapshot.end() ? offline_errors->second : "<missing>")
              << '\n';
    cleanup();
    return 1;
  }

  if (!bus.post("world_service", make_say_event(77, "@guild title Ally Vanguard"))) {
    std::cerr << "post_title\n";
    cleanup();
    return 1;
  }

  if (!wait_until(
          [&]() {
            const auto ally = verify_repository.load_character_by_name("Ally");
            return ally.has_value() && ally->guild_name == "DragonSlayers" &&
                   ally->guild_title == "Vanguard";
          },
          2s)) {
    std::cerr << "offline_title\n";
    cleanup();
    return 1;
  }

  refresh_before = snapshot_refresh_count(world);
  if (!bus.post("world_service", make_say_event(77, "@guild kick Ally"))) {
    std::cerr << "post_kick\n";
    cleanup();
    return 1;
  }

  if (!wait_until(
          [&]() {
            const auto ally = verify_repository.load_character_by_name("Ally");
            const auto snapshot = verify_repository.load_guild_castle_snapshot();
            const auto* guild = find_guild_state(snapshot, "DragonSlayers");
            const auto ally_missing =
                guild != nullptr &&
                std::find(guild->members.begin(), guild->members.end(), "Ally") == guild->members.end();
            return ally.has_value() && ally->guild_name.empty() && ally->guild_title.empty() &&
                   ally_missing && snapshot_refresh_count(world) > refresh_before;
          },
          2s)) {
    std::cerr << "offline_kick\n";
    cleanup();
    return 1;
  }

  refresh_before = snapshot_refresh_count(world);
  if (!bus.post("world_service", make_say_event(77, "@guild transfer Visitor"))) {
    std::cerr << "post_transfer\n";
    cleanup();
    return 1;
  }

  if (!wait_until(
          [&]() {
            const auto hero = verify_repository.load_character_by_name("GuildLord");
            const auto visitor = verify_repository.load_character_by_name("Visitor");
            const auto snapshot = verify_repository.load_guild_castle_snapshot();
            const auto* guild = find_guild_state(snapshot, "DragonSlayers");
            const auto world_snapshot = world.snapshot();
            const auto lord = world_snapshot.find("castle_lord");
            return hero.has_value() && visitor.has_value() && guild != nullptr &&
                   hero->guild_name == "DragonSlayers" && hero->guild_title == "Member" &&
                   visitor->guild_name == "DragonSlayers" && visitor->guild_title == "Lord" &&
                   guild->lord == "Visitor" && lord != world_snapshot.end() &&
                   lord->second == "Visitor" && snapshot_refresh_count(world) > refresh_before;
          },
          2s)) {
    std::cerr << "offline_transfer\n";
    cleanup();
    return 1;
  }

  cleanup();
  return 0;
}
