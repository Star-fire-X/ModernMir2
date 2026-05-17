#include "ui/legacy_ui.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

namespace {

using mir2::client::RectI;
namespace ui = mir2::client::ui;

constexpr std::uint32_t kBackground = 0xFF000000U;
constexpr std::uint32_t kWindow = 0xFF202020U;
constexpr std::uint32_t kTop = 0xFF404040U;
constexpr std::uint32_t kHint = 0xFF606060U;
constexpr std::uint32_t kMoving = 0xFF808080U;

class SolidNode final : public ui::UiNode {
 public:
  SolidNode(const RectI bounds, const std::uint32_t color) : ui::UiNode(bounds), color_(color) {}

  void paint(mir2::client::SoftwareRenderer& renderer) override {
    renderer.fill_rect(resolved_bounds(), color_);
    ui::UiNode::paint(renderer);
  }

 private:
  std::uint32_t color_{0};
};

std::map<std::string, std::string> read_golden_pixels(const std::filesystem::path& path) {
  std::ifstream input(path);
  assert(input);

  std::map<std::string, std::string> values;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty() || line.front() == '#') {
      continue;
    }
    const auto separator = line.find('=');
    assert(separator != std::string::npos);
    values[line.substr(0, separator)] = line.substr(separator + 1);
  }
  return values;
}

std::uint32_t parse_u32(const std::string& value) {
  return static_cast<std::uint32_t>(std::stoul(value, nullptr, 16));
}

std::uint64_t parse_u64(const std::string& value) {
  return static_cast<std::uint64_t>(std::stoull(value, nullptr, 16));
}

std::uint32_t pixel_at(const mir2::client::SoftwareSurface& surface, const int x, const int y) {
  return surface.data()[static_cast<std::size_t>(y) *
                            static_cast<std::size_t>(surface.width()) +
                        static_cast<std::size_t>(x)];
}

std::uint64_t checksum_fnv1a64(const mir2::client::SoftwareSurface& surface) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (int y = 0; y < surface.height(); ++y) {
    for (int x = 0; x < surface.width(); ++x) {
      hash ^= pixel_at(surface, x, y);
      hash *= 1099511628211ULL;
    }
  }
  return hash;
}

void assert_pixel(const std::map<std::string, std::string>& golden, const std::string& key,
                  const mir2::client::SoftwareSurface& surface, const int x, const int y) {
  assert(pixel_at(surface, x, y) == parse_u32(golden.at(key)));
}

}  // namespace

int main() {
  const auto source_dir = std::filesystem::path{MIR2_CLIENT_SOURCE_DIR};
  const auto golden = read_golden_pixels(
      source_dir / "tests" / "golden" / "legacy_ui_golden_pixels.txt");

  mir2::client::SoftwareRenderer renderer;
  renderer.surface().resize(ui::kLegacyUiScreenWidth, ui::kLegacyUiScreenHeight);
  renderer.begin_frame(kBackground);

  ui::UiTree tree;
  auto* root = tree.set_root<ui::UiNode>(
      RectI{0, 0, ui::kLegacyUiScreenWidth, ui::kLegacyUiScreenHeight});
  root->emplace_child<SolidNode>(RectI{100, 100, 180, 120}, kWindow);
  auto* top = root->emplace_child<SolidNode>(RectI{130, 130, 150, 120}, kTop);
  top->set_paint_layer(ui::UiPaintLayer::top);
  auto* hint = root->emplace_child<SolidNode>(RectI{160, 160, 120, 100}, kHint);
  hint->set_paint_layer(ui::UiPaintLayer::hint);
  auto* moving = root->emplace_child<SolidNode>(RectI{190, 190, 90, 80}, kMoving);
  moving->set_paint_layer(ui::UiPaintLayer::moving_item);

  tree.paint(renderer);
  assert(pixel_at(renderer.surface(), 200, 200) == kWindow);

  tree.paint_layer(renderer, ui::UiPaintLayer::top);
  tree.paint_layer(renderer, ui::UiPaintLayer::hint);
  tree.paint_layer(renderer, ui::UiPaintLayer::moving_item);

  assert_pixel(golden, "background_10_10", renderer.surface(), 10, 10);
  assert_pixel(golden, "window_110_110", renderer.surface(), 110, 110);
  assert_pixel(golden, "top_140_140", renderer.surface(), 140, 140);
  assert_pixel(golden, "hint_170_170", renderer.surface(), 170, 170);
  assert_pixel(golden, "moving_200_200", renderer.surface(), 200, 200);
  assert_pixel(golden, "background_300_300", renderer.surface(), 300, 300);
  assert(checksum_fnv1a64(renderer.surface()) == parse_u64(golden.at("checksum_fnv1a64")));
  return 0;
}
