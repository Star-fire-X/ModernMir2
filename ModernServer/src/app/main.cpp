#include <atomic>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "config/config_loader.hpp"
#include "core/default_modules.hpp"
#include "core/host_runtime.hpp"
#include "importer/legacy_character_importer.hpp"
#include "importer/legacy_importer.hpp"
#include "util/logger.hpp"

namespace {

std::atomic_bool g_should_stop{false};

void on_signal(int) { g_should_stop.store(true, std::memory_order_relaxed); }

struct Options {
  std::filesystem::path config_root{};
  std::filesystem::path legacy_root{};
  std::filesystem::path legacy_hum_db{};
  std::filesystem::path legacy_mir_db{};
  std::string legacy_import_default_password{"pass"};
  int run_seconds{0};
  bool import_only{false};
  bool import_legacy_characters_only{false};
};

Options parse_options(int argc, char** argv, const std::filesystem::path& root_dir) {
  Options options;
  options.config_root = root_dir / "config";

  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--config-root" && index + 1 < argc) {
      options.config_root = argv[++index];
    } else if (argument == "--legacy-root" && index + 1 < argc) {
      options.legacy_root = argv[++index];
    } else if (argument == "--legacy-hum-db" && index + 1 < argc) {
      options.legacy_hum_db = argv[++index];
    } else if (argument == "--legacy-mir-db" && index + 1 < argc) {
      options.legacy_mir_db = argv[++index];
    } else if (argument == "--legacy-import-default-password" && index + 1 < argc) {
      options.legacy_import_default_password = argv[++index];
    } else if (argument == "--run-seconds" && index + 1 < argc) {
      options.run_seconds = std::stoi(argv[++index]);
    } else if (argument == "--import-only") {
      options.import_only = true;
    } else if (argument == "--import-legacy-characters-only") {
      options.import_legacy_characters_only = true;
    }
  }

  if (options.config_root.is_relative()) {
    options.config_root = root_dir / options.config_root;
  }
  if (!options.legacy_root.empty() && options.legacy_root.is_relative()) {
    options.legacy_root = root_dir / options.legacy_root;
  }
  if (!options.legacy_hum_db.empty() && options.legacy_hum_db.is_relative()) {
    options.legacy_hum_db = root_dir / options.legacy_hum_db;
  }
  if (!options.legacy_mir_db.empty() && options.legacy_mir_db.is_relative()) {
    options.legacy_mir_db = root_dir / options.legacy_mir_db;
  }
  return options;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    const auto executable = std::filesystem::absolute(argv[0]);
    const auto root_dir = executable.parent_path().parent_path();
    const auto options = parse_options(argc, argv, root_dir);

    if (!options.legacy_root.empty()) {
      mir2::LegacyImporter importer;
      const auto report = importer.import_tree(options.legacy_root, options.config_root);
      std::cout << "Imported legacy assets: maps=" << report.map_count
                << " spawns=" << report.spawn_count << " npcs=" << report.npc_count << '\n';
      if (options.import_only) {
        return 0;
      }
    }

    mir2::ConfigLoader config_loader;
    auto config = config_loader.load(options.config_root);
    auto logger = mir2::util::make_logger(root_dir, config.runtime.log_dir);

    if (!options.legacy_mir_db.empty() || options.import_legacy_characters_only) {
      mir2::Repository repository(root_dir / config.runtime.data_dir / "mir2.sqlite");
      repository.ensure_schema(root_dir / "schema" / "mir2.sql");
      mir2::LegacyCharacterImporter importer;
      mir2::LegacyCharacterImportOptions import_options;
      import_options.hum_db_path = options.legacy_hum_db;
      import_options.mir_db_path = options.legacy_mir_db;
      import_options.default_password = options.legacy_import_default_password;
      import_options.config = &config;
      const auto report = importer.import_characters(import_options, repository);
      std::cout << "Imported legacy characters: scanned=" << report.scanned
                << " imported=" << report.imported << " skipped=" << report.skipped
                << " failed=" << report.failed << " warnings=" << report.warnings.size()
                << '\n';
      for (const auto& warning : report.warnings) {
        std::cout << "warning " << warning.character_name << " " << warning.kind << "="
                  << warning.value << '\n';
      }
      if (options.import_legacy_characters_only) {
        return report.failed == 0 ? 0 : 1;
      }
    }

    mir2::HostRuntime runtime(root_dir, std::move(config), logger);
    mir2::register_default_modules(runtime);

    runtime.start_all();

    const auto started_at = std::chrono::steady_clock::now();
    while (!g_should_stop.load(std::memory_order_relaxed)) {
      runtime.write_status_snapshot();
      std::this_thread::sleep_for(std::chrono::seconds(1));
      if (options.run_seconds > 0) {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() -
                                                             started_at)
                .count();
        if (elapsed >= options.run_seconds) {
          break;
        }
      }
    }

    runtime.stop_all();
    runtime.join_all();
    runtime.write_status_snapshot();
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "mir2_host failed: " << ex.what() << '\n';
    return 1;
  }
}
