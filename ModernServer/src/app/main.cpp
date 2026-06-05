/**
 * @file main.cpp
 * @brief 应用程序入口点
 * @details mir2_host 服务端主程序入口。负责：
 *          - 解析命令行选项
 *          - 导入遗留资产（地图、刷怪、NPC 脚本等）
 *          - 导入遗留角色数据（Hum.DB / Mir.DB）
 *          - 加载服务端配置
 *          - 初始化日志系统
 *          - 创建并启动 HostRuntime 生命周期
 *          - 处理 SIGINT/SIGTERM 信号以实现优雅关闭
 * @note 程序分为六个明确的阶段执行：
 *       阶段 1：导入遗留资产 -> 阶段 2：加载配置 -> 阶段 3：导入角色数据
 *       -> 阶段 4：启动运行时 -> 阶段 5：主循环 -> 阶段 6：优雅关闭
 * @see HostRuntime 运行时生命周期管理类
 */

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

/**
 * @brief 全局停止标志
 * @details 原子布尔变量，在收到 SIGINT 或 SIGTERM 信号时被设置为 true。
 *          主循环检查此标志以决定是否退出。
 * @note 使用 std::memory_order_relaxed 即可满足需求，因为该标志仅用于
 *       主循环的退出判断，不需要严格的跨线程同步顺序保证。
 */
std::atomic_bool g_should_stop{false};

/**
 * @brief 信号处理函数
 * @details 当进程收到 SIGINT（Ctrl+C）或 SIGTERM 时调用，
 *          将全局停止标志设置为 true。
 * @param signal_number 信号编号（函数体内未使用，仅用于匹配 signal() 的函数签名）
 * @warning 信号处理函数中只能执行异步信号安全（async-signal-safe）的操作，
 *          例如对 std::atomic 的 store。不应在此函数中调用 malloc、printf
 *          等非安全函数。此函数仅做原子存储，符合安全要求。
 */
void on_signal(int /*signal_number*/) { g_should_stop.store(true, std::memory_order_relaxed); }

/**
 * @brief 命令行选项结构体
 * @details 存储通过命令行参数解析得到的各项配置选项，
 *          包括路径、导入模式、运行时长等。
 * @note 默认密码为 "pass"，可通过 --legacy-import-default-password 覆盖。
 */
struct Options {
  std::filesystem::path config_root{};          ///< 配置根目录路径
  std::filesystem::path legacy_root{};           ///< 遗留客户端资源根目录路径
  std::filesystem::path legacy_hum_db{};         ///< 遗留角色数据库路径（Hum.DB）
  std::filesystem::path legacy_mir_db{};         ///< 遗留镜像数据库路径（Mir.DB）
  std::string legacy_import_default_password{"pass"};  ///< 遗留角色导入默认密码
  int run_seconds{0};                            ///< 运行时长（秒），0 表示持续运行直到收到信号
  bool import_only{false};                       ///< 仅导入资源模式（导入后直接退出）
  bool import_legacy_characters_only{false};      ///< 仅导入遗留角色模式（导入后直接退出）
};

/**
 * @brief 解析命令行参数
 * @details 支持的参数列表：
 *          --config-root <path>             配置根目录
 *          --legacy-root <path>             遗留客户端资源根目录
 *          --legacy-hum-db <path>           遗留 Hum.DB 路径
 *          --legacy-mir-db <path>           遗留 Mir.DB 路径
 *          --legacy-import-default-password  导入默认密码
 *          --run-seconds <N>                运行 N 秒后自动退出
 *          --import-only                    仅导入资产后退出
 *          --import-legacy-characters-only   仅导入角色后退出
 * @param argc    命令行参数个数
 * @param argv    命令行参数数组
 * @param root_dir 可执行文件所在目录的父目录的父目录（项目根目录）
 * @return 解析后的 Options 结构体
 * @note 相对路径参数会自动拼接 root_dir 转换为绝对路径。
 *       config_root 默认值为 root_dir / "config"。
 */
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

  // 将相对路径转换为相对于项目根目录的绝对路径
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

/**
 * @brief 程序入口函数
 * @details 主程序按六个阶段顺序执行：
 *
 *          阶段 1 - 导入遗留资产：
 *          如果提供了 --legacy-root 参数，使用 LegacyImporter 导入地图、
 *          刷怪信息和 NPC 脚本到配置目录。如果是 --import-only 模式，完成后立即退出。
 *
 *          阶段 2 - 加载配置：
 *          使用 ConfigLoader 加载 TOML 配置文件，然后创建 spdlog 日志记录器。
 *
 *          阶段 3 - 导入遗留角色数据：
 *          如果提供了 --legacy-mir-db 或 --import-legacy-characters-only，
 *          使用 LegacyCharacterImporter 从 Hum.DB / Mir.DB 导入角色数据到 SQLite 数据库。
 *
 *          阶段 4 - 创建运行时环境：
 *          创建 HostRuntime 实例，注册默认模块，然后启动所有模块。
 *
 *          阶段 5 - 主循环：
 *          每秒执行一次 write_status_snapshot()，检查 g_should_stop 信号标志
 *          和 --run-seconds 设定的运行时上限。
 *
 *          阶段 6 - 优雅关闭：
 *          依次调用 stop_all()、join_all() 停止所有模块线程，
 *          最后写入一次状态快照后退出。
 *
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return int 0 表示正常退出，1 表示异常退出（捕获到 std::exception）
 * @warning 信号处理器使用 std::signal 注册，在不同平台上行为可能略有差异。
 *          在 Windows 上 SIGINT 和 SIGTERM 均能正常工作，但 SIGTERM
 *          在 Windows 上的支持有限。
 */
int main(int argc, char** argv) {
  try {
    // 注册信号处理器以实现优雅关闭
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    // 确定项目根目录（可执行文件所在目录的父目录的父目录）
    // 例如：/usr/local/bin/mir2_host -> 根目录为 /usr/local
    const auto executable = std::filesystem::absolute(argv[0]);
    const auto root_dir = executable.parent_path().parent_path();
    const auto options = parse_options(argc, argv, root_dir);

    // 阶段 1：导入遗留资产（地图、刷怪、NPC 等）
    if (!options.legacy_root.empty()) {
      mir2::LegacyImporter importer;
      const auto report = importer.import_tree(options.legacy_root, options.config_root);
      std::cout << "Imported legacy assets: maps=" << report.map_count
                << " spawns=" << report.spawn_count << " npcs=" << report.npc_count << '\n';
      if (options.import_only) {
        return 0;
      }
    }

    // 阶段 2：加载服务端配置
    mir2::ConfigLoader config_loader;
    auto config = config_loader.load(options.config_root);
    // 创建日志记录器，后续阶段的日志通过 spdlog 输出
    auto logger = mir2::util::make_logger(root_dir, config.runtime.log_dir);

    // 阶段 3：导入遗留角色数据（Hum.DB / Mir.DB）
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

    // 阶段 4：创建运行时环境并启动所有模块
    mir2::HostRuntime runtime(root_dir, std::move(config), logger);
    mir2::register_default_modules(runtime);

    runtime.start_all();

    // 阶段 5：主循环——每秒写入状态快照，检查退出条件
    const auto started_at = std::chrono::steady_clock::now();
    while (!g_should_stop.load(std::memory_order_relaxed)) {
      runtime.write_status_snapshot();
      // 每秒轮询一次，降低 CPU 占用
      std::this_thread::sleep_for(std::chrono::seconds(1));
      // 如果指定了运行时长，检查是否已达到上限
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

    // 阶段 6：优雅关闭
    runtime.stop_all();   // 通知所有模块停止
    runtime.join_all();   // 等待所有模块线程结束
    runtime.write_status_snapshot();  // 写入最终状态快照
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "mir2_host failed: " << ex.what() << '\n';
    return 1;
  }
}
