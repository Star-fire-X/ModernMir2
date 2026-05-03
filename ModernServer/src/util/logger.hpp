#pragma once

#include <filesystem>
#include <memory>

#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"

namespace mir2::util {

inline std::shared_ptr<spdlog::logger> make_logger(const std::filesystem::path& root_dir,
                                                   const std::filesystem::path& log_dir) {
  const auto full_log_dir = root_dir / log_dir;
  std::filesystem::create_directories(full_log_dir);

  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  auto file_sink =
      std::make_shared<spdlog::sinks::basic_file_sink_mt>((full_log_dir / "mir2_host.log").string(),
                                                          true);

  std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
  auto logger = std::make_shared<spdlog::logger>("mir2_host", sinks.begin(), sinks.end());
  logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
  logger->set_level(spdlog::level::info);
  spdlog::register_logger(logger);
  return logger;
}

}  // namespace mir2::util
