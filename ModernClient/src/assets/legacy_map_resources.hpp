/**
 * @file legacy_map_resources.hpp
 * @brief 旧版地图资源查找 —— 根据地图单元格数据确定对应的精灵归档和帧索引
 *
 * @details 提供根据地图区域编号（area）确定 Objects 归档、根据背景图/中间层图
 *          索引查找瓦片帧、根据物件帧索引查找物件精灵的函数。
 *          所有查找逻辑与 Delphi 客户端的资源加载行为一致。
 */

#pragma once

#include <cstdint>
#include <optional>

#include "assets/asset_manager.hpp"
#include "shared/legacy/map_render_math.hpp"

namespace mir2::client {

[[nodiscard]] inline ArchiveId legacy_object_archive_for_area(const int area) {
  switch (area) {
    case 0:
      return ArchiveId::objects1;
    case 1:
      return ArchiveId::objects2;
    case 2:
      return ArchiveId::objects3;
    case 3:
      return ArchiveId::objects4;
    case 4:
      return ArchiveId::objects5;
    case 5:
      return ArchiveId::objects6;
    case 6:
      return ArchiveId::objects7;
    default:
      return ArchiveId::objects1;
  }
}

struct LegacyArchiveFrame {
  ArchiveId archive{ArchiveId::tiles};
  int index{-1};
};

[[nodiscard]] inline std::optional<LegacyArchiveFrame> legacy_ground_tile_resource(
    const int map_x, const int map_y, const std::uint16_t bk_img) {
  const auto index = legacy::legacy_ground_tile_frame_index(map_x, map_y, bk_img);
  if (index < 0) {
    return std::nullopt;
  }
  return LegacyArchiveFrame{ArchiveId::tiles, index};
}

[[nodiscard]] inline std::optional<LegacyArchiveFrame> legacy_small_tile_resource(
    const std::uint16_t mid_img) {
  const auto index = legacy::legacy_small_tile_frame_index(mid_img);
  if (index < 0) {
    return std::nullopt;
  }
  return LegacyArchiveFrame{ArchiveId::sm_tiles, index};
}

[[nodiscard]] inline std::optional<LegacyArchiveFrame> legacy_map_object_resource(
    const int area, const int frame_index) {
  if (frame_index < 0) {
    return std::nullopt;
  }
  return LegacyArchiveFrame{legacy_object_archive_for_area(area), frame_index};
}

}  // namespace mir2::client
