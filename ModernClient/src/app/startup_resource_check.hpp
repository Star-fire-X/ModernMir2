#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "assets/asset_manager.hpp"

namespace mir2::client {

enum class StartupResourceSeverity {
  fatal,
  warning,
};

struct StartupResourceIssue {
  StartupResourceSeverity severity{StartupResourceSeverity::fatal};
  std::wstring archive{};
  int frame_index{-1};
  std::wstring message{};
};

struct StartupResourceCheckResult {
  std::vector<StartupResourceIssue> fatal_issues{};
  std::vector<StartupResourceIssue> warning_issues{};

  [[nodiscard]] bool ok() const { return fatal_issues.empty(); }
};

[[nodiscard]] StartupResourceCheckResult check_startup_asset_root(
    const std::filesystem::path& root_path);
[[nodiscard]] StartupResourceCheckResult check_startup_auth_resources(AssetManager& assets);
[[nodiscard]] std::wstring format_startup_resource_report(
    const StartupResourceCheckResult& result, const std::filesystem::path& root_path);

}  // namespace mir2::client
