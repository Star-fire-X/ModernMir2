/**
 * @file logger.hpp
 * @brief 日志记录器工厂
 * @details 基于 spdlog 库创建日志记录器的工厂函数。
 *          同时创建控制台彩色输出和文件日志两个 sink，
 *          用于服务端运行时的日志记录。
 * @note spdlog 是一个高性能的 C++ 日志库，支持异步日志、多线程安全、
 *       格式化输出和分级日志控制。详见 https://github.com/gabime/spdlog
 * @see make_logger() 是创建日志记录器的唯一入口函数
 */

#pragma once

#include <filesystem>
#include <memory>

#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"

namespace mir2::util {

/**
 * @brief 创建并注册 mir2_host 日志记录器
 * @details 创建包含两个 sink 的日志记录器：
 *          - stdout_color_sink_mt：控制台彩色输出，支持按日志级别着色（如 error 为红色）
 *          - basic_file_sink_mt：文件日志，追加写入到 root_dir/log_dir/mir2_host.log
 *          日志格式：[时间] [级别] [线程ID] 消息
 *          默认日志级别为 info（记录 info、warn、error、critical 级别）。
 * @param root_dir 项目根目录路径
 * @param log_dir  日志目录的相对路径（相对于 root_dir）
 * @return 指向已注册的 spdlog 日志记录器的共享指针
 * @note stdout_color_sink_mt 中的 _mt 后缀表示多线程安全（multi-threaded），
 *       支持多个线程同时写入日志而无需外部加锁。
 * @note 日志文件以追加模式打开（第二个参数为 true），不会覆盖已有日志。
 * @warning 此函数每次调用都会创建一个新的日志记录器并注册到 spdlog 全局管理器中。
 *          重复注册同名记录器（"mir2_host"）会导致 spdlog 抛出异常，
 *          应确保在程序生命周期中只调用一次。
 */
inline std::shared_ptr<spdlog::logger> make_logger(const std::filesystem::path& root_dir,
                                                   const std::filesystem::path& log_dir) {
  const auto full_log_dir = root_dir / log_dir;
  // 确保日志目录存在，防止文件 sink 因目录不存在而创建失败
  std::filesystem::create_directories(full_log_dir);

  // 创建控制台彩色输出 sink
  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  // 创建文件输出 sink（追加模式），日志文件路径为 root_dir/log_dir/mir2_host.log
  auto file_sink =
      std::make_shared<spdlog::sinks::basic_file_sink_mt>((full_log_dir / "mir2_host.log").string(),
                                                          true);

  // 组合 sinks 并创建日志记录器
  std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
  auto logger = std::make_shared<spdlog::logger>("mir2_host", sinks.begin(), sinks.end());
  // 设置日志格式：[2026-06-04 10:30:00.123] [info] [12345] 消息内容
  // %Y-%m-%d 日期，%H:%M:%S.%e 时间（含毫秒），%^%l%$ 带颜色级别，%t 线程ID，%v 消息体
  logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
  // 设置日志级别为 info（仅记录 info 及以上级别：info, warn, error, critical）
  logger->set_level(spdlog::level::info);
  // 注册到 spdlog 全局管理器，以便其他模块通过 spdlog::get("mir2_host") 获取
  spdlog::register_logger(logger);
  return logger;
}

}  // namespace mir2::util
