#include "assets/asset_manager.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using mir2::client::ArchiveId;
using mir2::client::AssetManager;
using mir2::client::SpriteFrame;

std::filesystem::path resolve_root() {
  const auto local = std::filesystem::absolute("Legend of Mir");
  if (std::filesystem::exists(local / "Data")) {
    return local;
  }
  return LR"(F:\mir2\Legend of Mir)";
}

std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  assert(input);
  std::ostringstream out;
  out << input.rdbuf();
  return out.str();
}

std::uint64_t frame_hash(const SpriteFrame& frame) {
  auto hash = 1469598103934665603ULL;
  const auto mix = [&hash](const std::uint32_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
  };
  mix(static_cast<std::uint32_t>(frame.width));
  mix(static_cast<std::uint32_t>(frame.height));
  mix(static_cast<std::uint32_t>(frame.hotspot_x));
  mix(static_cast<std::uint32_t>(frame.hotspot_y));
  for (const auto pixel : frame.pixels) {
    mix(pixel);
  }
  return hash;
}

std::pair<int, std::uint32_t> first_visible_pixel(const SpriteFrame& frame) {
  for (std::size_t index = 0; index < frame.pixels.size(); ++index) {
    if (((frame.pixels[index] >> 24U) & 0xFFU) != 0U) {
      return {static_cast<int>(index), frame.pixels[index]};
    }
  }
  return {-1, 0U};
}

void append_frame_line(std::ostringstream& out, AssetManager& assets, const std::string& label,
                       const int index) {
  const auto frame = assets.get_frame(ArchiveId::chr_sel, index);
  if (frame == nullptr || frame->empty()) {
    std::cerr << "missing_chrsel_frame=" << index << '\n';
    std::exit(1);
  }
  const auto [first_index, first_pixel] = first_visible_pixel(*frame);
  if (first_index < 0) {
    std::cerr << "blank_chrsel_frame=" << index << '\n';
    std::exit(1);
  }
  out << label << " index=" << index << " size=" << frame->width << 'x' << frame->height
      << " hotspot=" << frame->hotspot_x << ',' << frame->hotspot_y << " hash=0x"
      << std::hex << std::setw(16) << std::setfill('0') << frame_hash(*frame) << std::dec
      << " first_visible=" << first_index << ":0x" << std::hex << std::setw(8)
      << std::setfill('0') << first_pixel << std::dec << '\n';
}

std::string render_real_asset_frame_golden(AssetManager& assets) {
  std::ostringstream out;
  out << "# local real-resource Delphi auth animation frames\n";
  append_frame_line(out, assets, "login_background", 22);
  for (int frame = 0; frame < 10; ++frame) {
    append_frame_line(out, assets, "login_door_" + std::to_string(frame), 23 + frame);
  }
  for (int frame = 0; frame < 14; ++frame) {
    append_frame_line(out, assets, "effect_" + std::to_string(frame), 4 + frame);
  }
  for (int sex = 0; sex < 2; ++sex) {
    for (int job = 0; job < 3; ++job) {
      for (int frame = 0; frame < 16; ++frame) {
        append_frame_line(out, assets,
                          "idle_job" + std::to_string(job) + "_sex" +
                              std::to_string(sex) + "_" + std::to_string(frame),
                          40 + job * 40 + sex * 120 + frame);
      }
      for (int frame = 0; frame < 13; ++frame) {
        append_frame_line(out, assets,
                          "freeze_job" + std::to_string(job) + "_sex" +
                              std::to_string(sex) + "_" + std::to_string(frame),
                          60 + job * 40 + sex * 120 + frame);
      }
    }
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
  const auto actual = render_real_asset_frame_golden(assets);
  const auto golden_path =
      source_dir / "tests" / "golden" / "delphi_ui" / "auth_animation_real_asset_frame_golden.txt";
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
