// ============================================================
// Mir2 legacy map render math
// Shared Delphi-compatible viewport and coordinate helpers.
// ============================================================
#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>

namespace mir2::legacy {

constexpr int kLegacyLogicalMapUnit = 40;
constexpr int kLegacyUnitX = 48;
constexpr int kLegacyUnitY = 32;
constexpr int kLegacyHalfX = 24;
constexpr int kLegacyHalfY = 16;
constexpr int kLegacyViewHalfWidth = 9;
constexpr int kLegacyViewTopRows = 9;
constexpr int kLegacyViewBottomRows = 8;
constexpr int kLegacyLongHeightRows = 35;
constexpr int kLegacyDrawOriginX = -66;
constexpr int kLegacyDrawOriginY = -64;
constexpr int kLegacyMouseCenterX = 364;
constexpr int kLegacyMouseCenterY = 192;

struct LegacyMapViewport {
  int rx{0};
  int ry{0};
  int shift_x{0};
  int shift_y{0};
  int left{0};
  int top{0};
  int right{0};
  int bottom{0};
  int draw_origin_x{kLegacyDrawOriginX};
  int draw_origin_y{kLegacyDrawOriginY};
};

inline int legacy_up_int(const double value) {
  const auto truncated = static_cast<int>(value);
  return value > static_cast<double>(truncated) ? truncated + 1 : truncated;
}

inline LegacyMapViewport make_legacy_map_viewport(const int rx, const int ry,
                                                  const int shift_x = 0,
                                                  const int shift_y = 0) {
  LegacyMapViewport viewport;
  viewport.rx = rx;
  viewport.ry = ry;
  viewport.shift_x = shift_x;
  viewport.shift_y = shift_y;
  viewport.left = rx - kLegacyViewHalfWidth;
  viewport.top = ry - kLegacyViewTopRows;
  viewport.right = rx + kLegacyViewHalfWidth;
  viewport.bottom = ry + kLegacyViewBottomRows;
  viewport.draw_origin_x = kLegacyDrawOriginX - shift_x;
  viewport.draw_origin_y = kLegacyDrawOriginY - shift_y;
  return viewport;
}

inline std::pair<int, int> legacy_screen_from_map(const LegacyMapViewport& viewport,
                                                  const int map_x, const int map_y) {
  return {(map_x - viewport.rx) * kLegacyUnitX + kLegacyMouseCenterX + kLegacyHalfX -
              viewport.shift_x,
          (map_y - viewport.ry) * kLegacyUnitY + kLegacyMouseCenterY + kLegacyHalfY -
              viewport.shift_y};
}

inline std::pair<int, int> legacy_mouse_to_map(const LegacyMapViewport& viewport,
                                               const int mouse_x, const int mouse_y) {
  const auto map_x =
      legacy_up_int((mouse_x - kLegacyMouseCenterX + viewport.shift_x - kLegacyUnitX) /
                    static_cast<double>(kLegacyUnitX)) +
      viewport.rx;
  const auto map_y =
      legacy_up_int((mouse_y - kLegacyMouseCenterY + viewport.shift_y - kLegacyUnitY) /
                    static_cast<double>(kLegacyUnitY)) +
      viewport.ry;
  return {map_x, map_y};
}

inline std::pair<int, int> legacy_mouse_to_map_clamped(const LegacyMapViewport& viewport,
                                                       const int mouse_x, const int mouse_y,
                                                       const int map_width,
                                                       const int map_height) {
  auto [x, y] = legacy_mouse_to_map(viewport, mouse_x, mouse_y);
  x = std::clamp(x, 0, std::max(0, map_width - 1));
  y = std::clamp(y, 0, std::max(0, map_height - 1));
  return {x, y};
}

inline int legacy_tile_draw_x(const LegacyMapViewport& viewport, const int map_x) {
  return (map_x - viewport.left) * kLegacyUnitX + viewport.draw_origin_x;
}

inline int legacy_ground_back_y(const LegacyMapViewport& viewport, const int map_y) {
  return (map_y - viewport.top) * kLegacyUnitY + viewport.draw_origin_y - kLegacyUnitY;
}

inline int legacy_ground_mid_y(const LegacyMapViewport& viewport, const int map_y) {
  return (map_y - viewport.top) * kLegacyUnitY + viewport.draw_origin_y;
}

inline int legacy_object_row_y(const LegacyMapViewport& viewport, const int map_y) {
  return legacy_ground_back_y(viewport, map_y);
}

inline int legacy_actor_base_x(const LegacyMapViewport& viewport, const int actor_rx,
                               const int actor_shift_x) {
  return legacy_tile_draw_x(viewport, actor_rx) + actor_shift_x;
}

inline int legacy_actor_base_y(const LegacyMapViewport& viewport, const int actor_ry,
                               const int actor_shift_y) {
  return legacy_object_row_y(viewport, actor_ry) + actor_shift_y;
}

inline int legacy_actor_draw_row(const int actor_ry, const int down_draw_level) {
  return actor_ry - down_draw_level;
}

inline int legacy_ground_item_draw_x(const LegacyMapViewport& viewport, const int map_x,
                                     const int frame_width) {
  return legacy_tile_draw_x(viewport, map_x) + kLegacyHalfX - frame_width / 2;
}

inline int legacy_ground_item_draw_y(const LegacyMapViewport& viewport, const int map_y,
                                     const int frame_height) {
  return legacy_object_row_y(viewport, map_y) + kLegacyHalfY - frame_height / 2;
}

inline int legacy_ground_tile_frame_index(const int map_x, const int map_y,
                                          const std::uint16_t bk_img) {
  const auto index = static_cast<int>(bk_img & 0x7FFFU);
  if (index <= 0 || (map_x % 2) != 0 || (map_y % 2) != 0) {
    return -1;
  }
  return index - 1;
}

inline int legacy_small_tile_frame_index(const std::uint16_t mid_img) {
  return mid_img == 0U ? -1 : static_cast<int>(mid_img) - 1;
}

}  // namespace mir2::legacy
