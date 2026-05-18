#include "assets/asset_manager.hpp"
#include "delphi_ui_manifest_helpers.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using mir2::client::ArchiveId;
using mir2::client::AssetManager;
using mir2::client::RectI;
using mir2::client::SoftwareSurface;
using mir2::client::SpriteFrame;
using mir2::client::tests::checksum_fnv1a64;
using mir2::client::tests::json_direct_rect_after;
using mir2::client::tests::json_rect_after;
using mir2::client::tests::manifest_slice;
using mir2::client::tests::pixel_at;
using mir2::client::tests::read_text_file;

struct SceneCapture {
  std::string name;
  SoftwareSurface surface{800, 600};
  std::vector<std::pair<int, int>> probes{};
};

std::filesystem::path resolve_root() {
  const auto local = std::filesystem::absolute("Legend of Mir");
  if (std::filesystem::exists(local / "Data")) {
    return local;
  }
  return LR"(F:\mir2\Legend of Mir)";
}

void blit_frame(SoftwareSurface& surface, AssetManager& assets, const ArchiveId archive,
                const int index, const RectI rect) {
  const auto frame = assets.get_frame(archive, index);
  if (frame == nullptr || frame->empty()) {
    std::cerr << "missing_frame=" << index << '\n';
    std::exit(1);
  }
  surface.blit_rgba(rect.x, rect.y, frame->width, frame->height, frame->pixels.data());
}

std::string line_for(const SceneCapture& capture) {
  std::ostringstream out;
  out << capture.name << " hash=0x" << std::hex << std::setw(16) << std::setfill('0')
      << checksum_fnv1a64(capture.surface);
  for (const auto [x, y] : capture.probes) {
    out << " p" << std::dec << x << ',' << y << "=0x" << std::hex << std::setw(8)
        << std::setfill('0') << pixel_at(capture.surface, x, y);
  }
  return out.str();
}

std::string render_real_asset_golden_text(const std::string& manifest, AssetManager& assets) {
  const auto login = manifest_slice(manifest, "\"login\": {", 9000);
  const auto server_select = manifest_slice(manifest, "\"server_select\": {", 65000);
  const auto layouts = manifest_slice(manifest, "\"layouts\": {", 120000);
  const auto character = manifest_slice(layouts, "\"character_select\": {", 16000);
  const auto modal = manifest_slice(manifest, "\"message_modal\": {", 11000);
  const auto modal_size1 = manifest_slice(modal, "\"dialog_size_1\": {", 5000);

  const auto draw_login = [&assets, &login](SceneCapture& scene) {
    scene.surface.clear(0xFF000000U);
    blit_frame(scene.surface, assets, ArchiveId::prguse, 60,
               json_direct_rect_after(login, "\"dialog\": {"));
    blit_frame(scene.surface, assets, ArchiveId::prguse, 61,
               json_rect_after(login, "\"create_account_button\""));
    blit_frame(scene.surface, assets, ArchiveId::prguse, 53,
               json_rect_after(login, "\"change_password_button\""));
    blit_frame(scene.surface, assets, ArchiveId::prguse, 62,
               json_rect_after(login, "\"login_button\""));
    blit_frame(scene.surface, assets, ArchiveId::prguse, 64,
               json_rect_after(login, "\"close_button\""));
    scene.probes = {{350, 259}, {391, 325}, {472, 188}};
  };

  const auto draw_modal = [&assets, &modal_size1](SceneCapture& scene) {
    blit_frame(scene.surface, assets, ArchiveId::prguse, 360,
               json_direct_rect_after(modal_size1, "\"dialog\": {"));
    blit_frame(scene.surface, assets, ArchiveId::prguse, 361,
               json_direct_rect_after(modal_size1, "\"ok_button\": {"));
    scene.probes.emplace_back(356, 336);
  };

  const auto draw_server_select_24 = [&assets, &server_select](SceneCapture& scene) {
    const auto section = manifest_slice(server_select, "\"24\": {", 24000);
    scene.surface.clear(0xFF000000U);
    blit_frame(scene.surface, assets, ArchiveId::prguse2, 5,
               json_direct_rect_after(section, "\"dialog\": {"));
    blit_frame(scene.surface, assets, ArchiveId::prguse, 64,
               json_direct_rect_after(section, "\"close_button\": {"));
    for (int index = 1; index <= 24; ++index) {
      blit_frame(scene.surface, assets, ArchiveId::prguse2, 2,
                 json_rect_after(section, "\"name\": \"DSServer" + std::to_string(index) + "\""));
    }
    scene.probes = {{108, 120}, {133, 187}, {473, 481}};
  };

  const auto draw_character_select = [&assets, &character](SceneCapture& scene) {
    scene.surface.clear(0xFF000000U);
    blit_frame(scene.surface, assets, ArchiveId::prguse, 65,
               json_direct_rect_after(character, "\"dialog\": {"));
    blit_frame(scene.surface, assets, ArchiveId::prguse, 66,
               json_rect_after(character, "\"left_button\""));
    blit_frame(scene.surface, assets, ArchiveId::prguse, 67,
               json_rect_after(character, "\"right_button\""));
    blit_frame(scene.surface, assets, ArchiveId::prguse, 68,
               json_rect_after(character, "\"start_button\""));
    blit_frame(scene.surface, assets, ArchiveId::prguse, 69,
               json_rect_after(character, "\"new_button\""));
    blit_frame(scene.surface, assets, ArchiveId::prguse, 70,
               json_rect_after(character, "\"erase_button\""));
    scene.probes = {{385, 456}, {133, 453}, {685, 454}};
  };

  std::vector<SceneCapture> scenes;
  scenes.push_back(SceneCapture{"login_idle"});
  draw_login(scenes.back());

  scenes.push_back(SceneCapture{"login_error_modal"});
  draw_login(scenes.back());
  draw_modal(scenes.back());

  scenes.push_back(SceneCapture{"server_select_24"});
  draw_server_select_24(scenes.back());

  scenes.push_back(SceneCapture{"character_select_empty"});
  draw_character_select(scenes.back());

  scenes.push_back(SceneCapture{"login_notice_ok"});
  scenes.back().surface.clear(0xFF000000U);
  draw_modal(scenes.back());

  std::ostringstream out;
  out << "# local real-resource Delphi auth UI render\n";
  for (const auto& scene : scenes) {
    out << line_for(scene) << '\n';
  }
  return out.str();
}

}  // namespace

int main() {
  AssetManager assets;
  if (!assets.initialize(resolve_root())) {
    std::cerr << "asset_root_not_found\n";
    return 1;
  }

  const auto source_dir = std::filesystem::path{MIR2_CLIENT_SOURCE_DIR};
  const auto manifest =
      read_text_file(source_dir / "tests" / "golden" / "delphi_ui" / "auth_ui_manifest.json");
  const auto actual = render_real_asset_golden_text(manifest, assets);
  const auto golden_path =
      source_dir / "tests" / "golden" / "delphi_ui" / "auth_ui_real_asset_screenshot_golden.txt";
  if (!std::filesystem::exists(golden_path)) {
    std::cerr << actual;
    return 1;
  }
  const auto expected = read_text_file(golden_path);
  if (actual != expected) {
    std::cerr << actual;
    return 1;
  }
  return 0;
}
