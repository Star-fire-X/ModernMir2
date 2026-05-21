#include <filesystem>
#include <iostream>
#include <algorithm>
#include <cstdlib>
#include <vector>

#include "assets/asset_manager.hpp"
#include "scene/character_select_state.hpp"

namespace {

std::filesystem::path resolve_root() {
  const std::filesystem::path configured = LR"(F:\mir2\Legend of Mir)";
  if (std::filesystem::exists(configured / "Data")) {
    return configured;
  }
  const auto local = std::filesystem::absolute("Legend of Mir");
  if (std::filesystem::exists(local / "Data")) {
    return local;
  }
  return configured;
}

bool has_visible_pixel(const mir2::client::SpriteFrame& frame) {
  return std::any_of(frame.pixels.begin(), frame.pixels.end(), [](const auto pixel) {
    return ((pixel >> 24U) & 0xFFU) != 0U;
  });
}

bool has_transparent_pixel(const mir2::client::SpriteFrame& frame) {
  return std::any_of(frame.pixels.begin(), frame.pixels.end(), [](const auto pixel) {
    return ((pixel >> 24U) & 0xFFU) == 0U;
  });
}

struct ExpectedFrame {
  int index{0};
  int min_width{1};
  int min_height{1};
};

struct GoldenFrame {
  int index{0};
  int width{0};
  int height{0};
  std::uint64_t hash{0};
};

void assert_visible_frame(mir2::client::AssetManager& assets,
                          const mir2::client::ArchiveId archive,
                          const int index,
                          const char* label) {
  const auto frame = assets.get_frame(archive, index);
  if (frame == nullptr || frame->empty() || !has_visible_pixel(*frame)) {
    std::cerr << "missing_" << label << "_frame=" << index << '\n';
    std::exit(1);
  }
  if (frame->pixels.size() !=
      static_cast<std::size_t>(frame->width) * static_cast<std::size_t>(frame->height)) {
    std::cerr << "bad_" << label << "_pixel_count=" << index << '\n';
    std::exit(1);
  }
}

std::uint64_t frame_hash(const mir2::client::SpriteFrame& frame) {
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

}  // namespace

int main() {
  mir2::client::AssetManager assets;
  if (!assets.initialize(resolve_root())) {
    std::cerr << "asset_root_not_found\n";
    return 1;
  }

  const auto map = assets.load_map("0");
  const auto tile = assets.get_frame(mir2::client::ArchiveId::tiles, 0);
  const auto hum = assets.get_frame(mir2::client::ArchiveId::hum, 0);
  if (map == nullptr || tile == nullptr || hum == nullptr) {
    std::cerr << "asset_decode_failed\n";
    return 1;
  }

  const std::vector<int> required_prguse_frames{
      1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 15, 16, 17, 18, 19, 26, 29, 64,
      128, 130, 132, 134, 136, 138, 140, 229, 230, 232, 234, 236, 238,
      240, 242, 244, 246, 248, 249, 250, 251, 252, 253, 254, 255, 360,
      361, 363, 365, 367, 370, 371, 372, 373, 376, 377, 382, 383, 384, 385,
      386, 387, 388, 392, 393, 394, 396, 398};
  auto transparent_frame_count = 0;
  for (const auto index : required_prguse_frames) {
    const auto frame = assets.get_frame(mir2::client::ArchiveId::prguse, index);
    if (frame == nullptr || frame->empty()) {
      std::cerr << "missing_prguse_frame=" << index << '\n';
      return 1;
    }
    if (frame->pixels.size() !=
        static_cast<std::size_t>(frame->width) * static_cast<std::size_t>(frame->height)) {
      std::cerr << "bad_prguse_pixel_count=" << index << '\n';
      return 1;
    }
    if (!has_visible_pixel(*frame)) {
      std::cerr << "blank_prguse_frame=" << index << '\n';
      return 1;
    }
    if (has_transparent_pixel(*frame)) {
      ++transparent_frame_count;
    }
    const auto hotspot_limit = std::max(frame->width, frame->height) * 4;
    if (std::abs(frame->hotspot_x) > hotspot_limit || std::abs(frame->hotspot_y) > hotspot_limit) {
      std::cerr << "bad_prguse_hotspot=" << index << ':' << frame->hotspot_x << ','
                << frame->hotspot_y << '\n';
      return 1;
    }
  }
  if (transparent_frame_count == 0) {
    std::cerr << "no_transparent_prguse_frames\n";
    return 1;
  }
  const std::vector<ExpectedFrame> expected_windows{
      {1, 760, 120}, {3, 300, 200}, {229, 200, 120}, {360, 320, 150},
      {370, 230, 290}, {384, 380, 160}, {385, 290, 180}, {392, 120, 150},
      {394, 16, 16}};
  for (const auto expected : expected_windows) {
    const auto frame = assets.get_frame(mir2::client::ArchiveId::prguse, expected.index);
    if (frame == nullptr || frame->width < expected.min_width ||
        frame->height < expected.min_height) {
      std::cerr << "small_prguse_frame=" << expected.index << ':' 
                << (frame != nullptr ? frame->width : 0) << 'x'
                << (frame != nullptr ? frame->height : 0) << '\n';
      return 1;
    }
  }
  const std::vector<GoldenFrame> golden_frames{
      {1, 800, 251, 1203241275029048329ULL},
      {3, 336, 270, 12094342348025902453ULL},
      {229, 376, 179, 8412673154157397999ULL},
      {360, 452, 179, 1738951298605886050ULL},
      {370, 232, 325, 2335147248382999935ULL},
      {384, 416, 176, 14798837373431186768ULL},
      {385, 308, 205, 6078884276987409690ULL},
      {392, 140, 181, 12446451569727182208ULL},
      {394, 400, 200, 16581603697875060504ULL}};
  for (const auto expected : golden_frames) {
    const auto frame = assets.get_frame(mir2::client::ArchiveId::prguse, expected.index);
    if (frame == nullptr || frame->width != expected.width || frame->height != expected.height ||
        frame_hash(*frame) != expected.hash) {
      std::cerr << "golden_prguse_mismatch=" << expected.index << '\n';
      return 1;
    }
  }
  for (const auto index : {2, 4, 5}) {
    assert_visible_frame(assets, mir2::client::ArchiveId::prguse2, index, "prguse2");
  }
  assert_visible_frame(assets, mir2::client::ArchiveId::chr_sel, 22, "chrsel");
  for (int frame = 0; frame < mir2::client::kCharacterSelectEffectFrameCount; ++frame) {
    assert_visible_frame(assets, mir2::client::ArchiveId::chr_sel, 4 + frame, "chrsel_effect");
  }
  for (int sex = 0; sex < 2; ++sex) {
    for (int job = 0; job < 3; ++job) {
      for (int frame = 0; frame < mir2::client::kCharacterSelectSelectedFrameCount; ++frame) {
        assert_visible_frame(assets, mir2::client::ArchiveId::chr_sel,
                             40 + job * 40 + frame + sex * 120, "chrsel_idle");
      }
      for (int frame = 0; frame < mir2::client::kCharacterSelectFreezeFrameCount; ++frame) {
        assert_visible_frame(assets, mir2::client::ArchiveId::chr_sel,
                             60 + job * 40 + frame + sex * 120, "chrsel_freeze");
      }
    }
  }
  const auto magic_icon = assets.get_frame(mir2::client::ArchiveId::mag_icon, 0);
  if (magic_icon == nullptr || magic_icon->empty()) {
    std::cerr << "missing_magicon_frame=0\n";
    return 1;
  }
  const std::vector<std::pair<mir2::client::ArchiveId, const char*>> item_archives{
      {mir2::client::ArchiveId::items, "items"},
      {mir2::client::ArchiveId::state_item, "state_item"},
      {mir2::client::ArchiveId::dn_items, "dn_items"}};
  for (const auto& [archive, name] : item_archives) {
    const auto frame = assets.get_frame(archive, 1);
    if (frame == nullptr || frame->empty() || !has_visible_pixel(*frame)) {
      std::cerr << "missing_item_ui_frame=" << name << ":1\n";
      return 1;
    }
  }

  std::cout << "map=" << map->width << "x" << map->height
            << " tile=" << tile->width << "x" << tile->height
            << " hum=" << hum->width << "x" << hum->height
            << " px=" << hum->hotspot_x << "," << hum->hotspot_y << '\n';
  return 0;
}
