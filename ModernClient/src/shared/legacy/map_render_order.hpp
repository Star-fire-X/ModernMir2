#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace mir2::legacy {

enum class LegacyMapDrawLayer : std::uint8_t {
  background_tiles,
  middle_tiles,
  small_objects,
  ground_effects,
  large_object,
  ground_item,
  actor,
  fly_effect,
  selection_blend,
  actor_overlay,
  debug_overlay,
  overlay_effects,
  actor_screen_overlay,
};

constexpr std::array<LegacyMapDrawLayer, 4> kLegacyMapRowDrawOrder{
    LegacyMapDrawLayer::large_object,
    LegacyMapDrawLayer::ground_item,
    LegacyMapDrawLayer::actor,
    LegacyMapDrawLayer::fly_effect,
};

inline std::string_view legacy_map_draw_layer_name(const LegacyMapDrawLayer layer) {
  switch (layer) {
    case LegacyMapDrawLayer::background_tiles:
      return "background_tiles";
    case LegacyMapDrawLayer::middle_tiles:
      return "middle_tiles";
    case LegacyMapDrawLayer::small_objects:
      return "small_objects";
    case LegacyMapDrawLayer::ground_effects:
      return "ground_effects";
    case LegacyMapDrawLayer::large_object:
      return "large_object";
    case LegacyMapDrawLayer::ground_item:
      return "ground_item";
    case LegacyMapDrawLayer::actor:
      return "actor";
    case LegacyMapDrawLayer::actor_overlay:
      return "actor_overlay";
    case LegacyMapDrawLayer::fly_effect:
      return "fly_effect";
    case LegacyMapDrawLayer::selection_blend:
      return "selection_blend";
    case LegacyMapDrawLayer::debug_overlay:
      return "debug_overlay";
    case LegacyMapDrawLayer::overlay_effects:
      return "overlay_effects";
    case LegacyMapDrawLayer::actor_screen_overlay:
      return "actor_screen_overlay";
  }
  return "unknown";
}

inline int legacy_map_draw_layer_rank(const LegacyMapDrawLayer layer) {
  switch (layer) {
    case LegacyMapDrawLayer::background_tiles:
      return 0;
    case LegacyMapDrawLayer::middle_tiles:
      return 1;
    case LegacyMapDrawLayer::small_objects:
      return 2;
    case LegacyMapDrawLayer::ground_effects:
      return 3;
    case LegacyMapDrawLayer::large_object:
      return 4;
    case LegacyMapDrawLayer::ground_item:
      return 5;
    case LegacyMapDrawLayer::actor:
      return 6;
    case LegacyMapDrawLayer::fly_effect:
      return 7;
    case LegacyMapDrawLayer::selection_blend:
      return 8;
    case LegacyMapDrawLayer::actor_overlay:
      return 9;
    case LegacyMapDrawLayer::debug_overlay:
      return 10;
    case LegacyMapDrawLayer::overlay_effects:
      return 11;
    case LegacyMapDrawLayer::actor_screen_overlay:
      return 12;
  }
  return -1;
}

}  // namespace mir2::legacy
