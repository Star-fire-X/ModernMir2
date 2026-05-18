#include "app/startup_resource_check.hpp"

#include <algorithm>
#include <initializer_list>
#include <sstream>
#include <utility>

#include "scene/character_select_state.hpp"

namespace mir2::client {

namespace {

constexpr int kLoginBackgroundIndex = 22;
constexpr int kLoginDialogIndex = 60;
constexpr int kLoginSubmitButtonIndex = 62;
constexpr int kLoginCloseButtonIndex = 64;
constexpr int kSelectBackgroundIndex = 65;
constexpr int kServerSelectDialogIndex = 256;
constexpr int kServerSelectDialogTwoColumnIndex = 4;
constexpr int kServerSelectDialogThreeColumnIndex = 5;
constexpr int kServerSelectButtonIndex = 2;
constexpr int kSelectLeftButtonIndex = 66;
constexpr int kSelectRightButtonIndex = 67;
constexpr int kSelectStartButtonIndex = 68;
constexpr int kSelectNewButtonIndex = 69;
constexpr int kSelectEraseButtonIndex = 70;
constexpr int kMessageDialogIndex = 360;
constexpr int kMessageOkButtonIndex = 361;
constexpr int kMessageYesButtonIndex = 363;
constexpr int kMessageCancelButtonIndex = 365;
constexpr int kMessageNoButtonIndex = 367;
constexpr int kSelectEffectFirstIndex = 4;
constexpr int kSelectIdleFirstIndex = 40;
constexpr int kSelectFreezeFirstIndex = 60;

std::wstring archive_name(const ArchiveId archive) {
  switch (archive) {
    case ArchiveId::prguse:
      return L"Prguse.wil";
    case ArchiveId::prguse2:
      return L"Prguse2.wil";
    case ArchiveId::chr_sel:
      return L"ChrSel.wil";
    default:
      return L"unknown.wil";
  }
}

void add_issue(StartupResourceCheckResult& result, const StartupResourceSeverity severity,
               std::wstring archive, const int frame_index, std::wstring message) {
  StartupResourceIssue issue{severity, std::move(archive), frame_index, std::move(message)};
  if (severity == StartupResourceSeverity::fatal) {
    result.fatal_issues.push_back(std::move(issue));
    return;
  }
  result.warning_issues.push_back(std::move(issue));
}

bool has_visible_pixel(const SpriteFrame& frame) {
  return std::any_of(frame.pixels.begin(), frame.pixels.end(), [](const auto pixel) {
    return ((pixel >> 24U) & 0xFFU) != 0U;
  });
}

void require_frame(StartupResourceCheckResult& result, AssetManager& assets,
                   const ArchiveId archive, const int frame_index) {
  const auto frame = assets.get_frame(archive, frame_index);
  if (frame == nullptr) {
    add_issue(result, StartupResourceSeverity::fatal, archive_name(archive), frame_index,
              L"missing or unreadable frame");
    return;
  }
  if (frame->empty()) {
    add_issue(result, StartupResourceSeverity::fatal, archive_name(archive), frame_index,
              L"empty frame");
    return;
  }
  if (frame->pixels.size() !=
      static_cast<std::size_t>(frame->width) * static_cast<std::size_t>(frame->height)) {
    add_issue(result, StartupResourceSeverity::fatal, archive_name(archive), frame_index,
              L"bad pixel count");
    return;
  }
  if (!has_visible_pixel(*frame)) {
    add_issue(result, StartupResourceSeverity::fatal, archive_name(archive), frame_index,
              L"blank frame");
  }
}

void require_frames(StartupResourceCheckResult& result, AssetManager& assets,
                    const ArchiveId archive, const std::initializer_list<int> frames) {
  for (const auto frame : frames) {
    require_frame(result, assets, archive, frame);
  }
}

void require_character_select_frames(StartupResourceCheckResult& result, AssetManager& assets) {
  require_frame(result, assets, ArchiveId::chr_sel, kLoginBackgroundIndex);
  for (int frame = 0; frame < kCharacterSelectEffectFrameCount; ++frame) {
    require_frame(result, assets, ArchiveId::chr_sel, kSelectEffectFirstIndex + frame);
  }
  for (int sex = 0; sex < 2; ++sex) {
    for (int job = 0; job < 3; ++job) {
      for (int frame = 0; frame < kCharacterSelectSelectedFrameCount; ++frame) {
        require_frame(result, assets, ArchiveId::chr_sel,
                      kSelectIdleFirstIndex + job * 40 + frame + sex * 120);
      }
      for (int frame = 0; frame < kCharacterSelectFreezeFrameCount; ++frame) {
        require_frame(result, assets, ArchiveId::chr_sel,
                      kSelectFreezeFirstIndex + job * 40 + frame + sex * 120);
      }
    }
  }
}

void append_issues(std::wstringstream& out, const wchar_t* title,
                   const std::vector<StartupResourceIssue>& issues) {
  if (issues.empty()) {
    return;
  }
  out << title << L":\n";
  for (const auto& issue : issues) {
    out << L"- ";
    if (!issue.archive.empty()) {
      out << issue.archive;
      if (issue.frame_index >= 0) {
        out << L"[" << issue.frame_index << L"]";
      }
      out << L": ";
    }
    out << issue.message << L"\n";
  }
}

}  // namespace

StartupResourceCheckResult check_startup_asset_root(const std::filesystem::path& root_path) {
  StartupResourceCheckResult result;
  if (root_path.empty()) {
    add_issue(result, StartupResourceSeverity::fatal, {}, -1, L"asset_root is empty");
    return result;
  }
  if (!std::filesystem::exists(root_path)) {
    add_issue(result, StartupResourceSeverity::fatal, {}, -1, L"asset_root does not exist");
  }
  if (!std::filesystem::exists(root_path / "Data")) {
    add_issue(result, StartupResourceSeverity::fatal, {}, -1, L"missing Data directory");
  }
  if (!std::filesystem::exists(root_path / "Map")) {
    add_issue(result, StartupResourceSeverity::fatal, {}, -1, L"missing Map directory");
  }
  return result;
}

StartupResourceCheckResult check_startup_auth_resources(AssetManager& assets) {
  StartupResourceCheckResult result;
  require_frames(result, assets, ArchiveId::prguse,
                 {kLoginDialogIndex, kLoginSubmitButtonIndex, kLoginCloseButtonIndex,
                  kSelectBackgroundIndex, kServerSelectDialogIndex, kSelectLeftButtonIndex,
                  kSelectRightButtonIndex, kSelectStartButtonIndex, kSelectNewButtonIndex,
                  kSelectEraseButtonIndex, kMessageDialogIndex, kMessageOkButtonIndex,
                  kMessageYesButtonIndex, kMessageCancelButtonIndex, kMessageNoButtonIndex});
  require_frames(result, assets, ArchiveId::prguse2,
                 {kServerSelectButtonIndex, kServerSelectDialogTwoColumnIndex,
                  kServerSelectDialogThreeColumnIndex});
  require_character_select_frames(result, assets);
  return result;
}

std::wstring format_startup_resource_report(const StartupResourceCheckResult& result,
                                            const std::filesystem::path& root_path) {
  std::wstringstream out;
  out << (result.ok() ? L"Startup resource check passed." : L"Startup resource check failed.")
      << L"\nResource root: " << root_path.wstring() << L"\n";
  append_issues(out, L"Fatal issues", result.fatal_issues);
  append_issues(out, L"Warnings", result.warning_issues);
  return out.str();
}

}  // namespace mir2::client
