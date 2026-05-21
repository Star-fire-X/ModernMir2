#include "delphi_ui_manifest_helpers.hpp"

#include <cassert>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace {

using mir2::client::RectI;
using mir2::client::SoftwareSurface;
using mir2::client::tests::checksum_fnv1a64;
using mir2::client::tests::json_direct_rect_after;
using mir2::client::tests::json_rect_after;
using mir2::client::tests::manifest_slice;
using mir2::client::tests::pixel_at;
using mir2::client::tests::read_text_file;

constexpr auto kBackground = 0xFF080A0CU;
constexpr auto kPanel = 0xFF26333DU;
constexpr auto kEdit = 0xFF3C4F5DU;
constexpr auto kButton = 0xFF647985U;
constexpr auto kButtonHot = 0xFF8D9486U;
constexpr auto kModal = 0xFF5B3C31U;
constexpr auto kText = 0xFFC4BCA4U;
constexpr auto kLeftCharacter = 0xFF733D8EU;
constexpr auto kRightCharacter = 0xFF957234U;

struct SceneCapture {
  std::string name;
  SoftwareSurface surface{800, 600};
  std::vector<std::pair<int, int>> probes{};
};

void mark(SoftwareSurface& surface, const RectI point, const std::uint32_t color) {
  surface.fill_rect(RectI{point.x, point.y, point.w == 0 ? 5 : point.w,
                          point.h == 0 ? 5 : point.h},
                    color);
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

std::string render_golden_text(const std::string& manifest) {
  const auto login = manifest_slice(manifest, "\"login\": {", 9000);
  const auto server_select = manifest_slice(manifest, "\"server_select\": {", 65000);
  const auto layouts = manifest_slice(manifest, "\"layouts\": {", 120000);
  const auto character = manifest_slice(layouts, "\"character_select\": {", 16000);
  const auto modal = manifest_slice(manifest, "\"message_modal\": {", 11000);
  const auto modal_size1 = manifest_slice(modal, "\"dialog_size_1\": {", 5000);

  const auto draw_login = [&login](SceneCapture& scene) {
    scene.surface.clear(kBackground);
    scene.surface.fill_rect(json_direct_rect_after(login, "\"dialog\": {"), kPanel);
    scene.surface.fill_rect(json_rect_after(login, "\"account_edit\""), kEdit);
    scene.surface.fill_rect(json_rect_after(login, "\"password_edit\""), kEdit);
    scene.surface.fill_rect(json_rect_after(login, "\"create_account_button\""), kButton);
    scene.surface.fill_rect(json_rect_after(login, "\"change_password_button\""), kButton);
    scene.surface.fill_rect(json_rect_after(login, "\"login_button\""), kButtonHot);
    scene.surface.fill_rect(json_rect_after(login, "\"close_button\""), kButton);
    scene.probes = {{350, 259}, {391, 325}, {472, 188}};
  };

  const auto draw_modal = [&modal_size1](SceneCapture& scene) {
    scene.surface.fill_rect(json_direct_rect_after(modal_size1, "\"dialog\": {"), kModal);
    mark(scene.surface, json_direct_rect_after(modal_size1, "\"text_origin\": {"), kText);
    scene.surface.fill_rect(json_direct_rect_after(modal_size1, "\"ok_button\": {"), kButtonHot);
    scene.probes.emplace_back(259, 248);
    scene.probes.emplace_back(356, 336);
  };

  const auto draw_server_select = [&server_select](SceneCapture& scene, const int count) {
    const auto section = manifest_slice(server_select, "\"" + std::to_string(count) + "\": {",
                                        count == 24 ? 24000 : 18000);
    scene.surface.clear(kBackground);
    scene.surface.fill_rect(json_direct_rect_after(section, "\"dialog\": {"), kPanel);
    scene.surface.fill_rect(json_direct_rect_after(section, "\"close_button\": {"), kButton);
    for (int index = 1; index <= count; ++index) {
      scene.surface.fill_rect(json_rect_after(section, "\"name\": \"DSServer" +
                                                        std::to_string(index) + "\""),
                              index == 1 ? kButtonHot : kButton);
    }
    scene.probes = {{json_direct_rect_after(section, "\"dialog\": {").x,
                     json_direct_rect_after(section, "\"dialog\": {").y},
                    {json_rect_after(section, "\"name\": \"DSServer1\"").x,
                     json_rect_after(section, "\"name\": \"DSServer1\"").y}};
  };

  const auto draw_character_select = [&character](SceneCapture& scene, const int slots) {
    scene.surface.clear(kBackground);
    scene.surface.fill_rect(json_direct_rect_after(character, "\"dialog\": {"), kPanel);
    scene.surface.fill_rect(json_rect_after(character, "\"left_button\""), kButton);
    scene.surface.fill_rect(json_rect_after(character, "\"right_button\""), kButton);
    scene.surface.fill_rect(json_rect_after(character, "\"start_button\""), kButtonHot);
    scene.surface.fill_rect(json_rect_after(character, "\"new_button\""), kButton);
    scene.surface.fill_rect(json_rect_after(character, "\"erase_button\""), kButton);
    mark(scene.surface, json_direct_rect_after(character, "\"server_name_text\": {"), kText);
    if (slots >= 1) {
      scene.surface.fill_rect(RectI{120, 150, 120, 220}, kLeftCharacter);
      mark(scene.surface, json_direct_rect_after(character, "\"left_name_text\": {"), kText);
    }
    if (slots >= 2) {
      scene.surface.fill_rect(RectI{460, 152, 120, 220}, kRightCharacter);
      mark(scene.surface, json_direct_rect_after(character, "\"right_name_text\": {"), kText);
    }
    scene.probes = {{385, 456}, {400, 8}, {120, 150}, {460, 152}};
  };

  std::vector<SceneCapture> scenes;
  scenes.push_back(SceneCapture{"login_idle"});
  draw_login(scenes.back());

  scenes.push_back(SceneCapture{"login_error_modal"});
  draw_login(scenes.back());
  draw_modal(scenes.back());

  for (const auto count : {1, 8, 16, 24}) {
    scenes.push_back(SceneCapture{"server_select_" + std::to_string(count)});
    draw_server_select(scenes.back(), count);
  }

  for (const auto slots : {0, 1, 2}) {
    scenes.push_back(SceneCapture{"character_select_" +
                                  std::string(slots == 0 ? "empty" : slots == 1 ? "one"
                                                                                  : "two")});
    draw_character_select(scenes.back(), slots);
  }

  scenes.push_back(SceneCapture{"login_notice_ok"});
  scenes.back().surface.clear(kBackground);
  draw_modal(scenes.back());

  std::ostringstream out;
  out << "# asset-free Delphi auth UI deterministic render\n";
  for (const auto& scene : scenes) {
    out << line_for(scene) << '\n';
  }
  return out.str();
}

}  // namespace

int main() {
  const auto source_dir = std::filesystem::path{MIR2_CLIENT_SOURCE_DIR};
  const auto manifest =
      read_text_file(source_dir / "tests" / "golden" / "delphi_ui" / "auth_ui_manifest.json");
  const auto expected = read_text_file(
      source_dir / "tests" / "golden" / "delphi_ui" / "auth_ui_screenshot_golden.txt");
  assert(render_golden_text(manifest) == expected);
  return 0;
}
