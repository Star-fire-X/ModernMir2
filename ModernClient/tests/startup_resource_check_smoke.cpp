#include <cassert>
#include <filesystem>
#include <string>

#include "app/startup_resource_check.hpp"
#include "assets/asset_manager.hpp"

namespace {

bool contains(const std::wstring& text, const std::wstring& expected) {
  return text.find(expected) != std::wstring::npos;
}

void test_asset_root_failures(const std::filesystem::path& root) {
  auto result = mir2::client::check_startup_asset_root({});
  assert(!result.ok());
  auto report = mir2::client::format_startup_resource_report(result, {});
  assert(contains(report, L"Resource root:"));
  assert(contains(report, L"asset_root is empty"));

  const auto missing_root = root / "missing";
  result = mir2::client::check_startup_asset_root(missing_root);
  assert(!result.ok());
  report = mir2::client::format_startup_resource_report(result, missing_root);
  assert(contains(report, L"Resource root:"));
  assert(contains(report, L"asset_root does not exist"));
  assert(contains(report, L"missing Data directory"));
  assert(contains(report, L"missing Map directory"));

  const auto map_only = root / "map_only";
  std::filesystem::create_directories(map_only / "Map");
  result = mir2::client::check_startup_asset_root(map_only);
  assert(!result.ok());
  report = mir2::client::format_startup_resource_report(result, map_only);
  assert(contains(report, L"missing Data directory"));
  assert(!contains(report, L"missing Map directory"));

  const auto data_only = root / "data_only";
  std::filesystem::create_directories(data_only / "Data");
  result = mir2::client::check_startup_asset_root(data_only);
  assert(!result.ok());
  report = mir2::client::format_startup_resource_report(result, data_only);
  assert(!contains(report, L"missing Data directory"));
  assert(contains(report, L"missing Map directory"));
}

void test_auth_resource_failures(const std::filesystem::path& root) {
  const auto asset_root = root / "empty_assets";
  std::filesystem::create_directories(asset_root / "Data");
  std::filesystem::create_directories(asset_root / "Map");

  mir2::client::AssetManager assets;
  assert(assets.initialize(asset_root));

  const auto result = mir2::client::check_startup_auth_resources(assets);
  assert(!result.ok());
  assert(result.fatal_issues.size() >= 3);
  assert(result.fatal_issues.front().archive == L"Prguse.wil");
  assert(result.fatal_issues.front().frame_index == 60);

  const auto report = mir2::client::format_startup_resource_report(result, asset_root);
  assert(contains(report, L"Resource root:"));
  assert(contains(report, L"Fatal issues:"));
  assert(contains(report, L"Prguse.wil[60]"));
  assert(contains(report, L"Prguse2.wil[2]"));
  assert(contains(report, L"ChrSel.wil[22]"));

  const auto fatal_pos = report.find(L"Fatal issues:");
  const auto prguse_pos = report.find(L"Prguse.wil[60]");
  const auto prguse2_pos = report.find(L"Prguse2.wil[2]");
  const auto chrsel_pos = report.find(L"ChrSel.wil[22]");
  assert(fatal_pos < prguse_pos);
  assert(prguse_pos < prguse2_pos);
  assert(prguse2_pos < chrsel_pos);
}

}  // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() / "mir2_startup_resource_check_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  std::filesystem::create_directories(root);

  test_asset_root_failures(root);
  test_auth_resource_failures(root);

  std::filesystem::remove_all(root, ignored);
  return 0;
}
