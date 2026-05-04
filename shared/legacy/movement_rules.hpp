// ============================================================
// Mir2 旧版移动规则
// 职责：定义经典客户端的 8 方向移动系统，包含方向增量、
//       朝向计算、边界检测、行走/跑步目标计算
// ============================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>

namespace mir2::legacy {

/// 8 方向枚举（与经典 Delph 客户端一致）
enum Direction : std::uint8_t {
  kDirUp = 0,
  kDirUpRight = 1,
  kDirRight = 2,
  kDirDownRight = 3,
  kDirDown = 4,
  kDirDownLeft = 5,
  kDirLeft = 6,
  kDirUpLeft = 7
};

/// 步进增量：每个方向对应的瓦片坐标变化
struct StepDelta {
  int dx{0};
  int dy{0};
};

/// 移动目标：坐标 + 方向
struct MoveTarget {
  int x{0};
  int y{0};
  std::uint8_t dir{kDirDown};
};

/// 根据方向返回瓦片坐标增量
inline StepDelta direction_delta(std::uint8_t dir) {
  switch (dir) {
    case kDirUp:        return {0, -1};
    case kDirUpRight:   return {1, -1};
    case kDirRight:     return {1, 0};
    case kDirDownRight: return {1, 1};
    case kDirDown:      return {0, 1};
    case kDirDownLeft:  return {-1, 1};
    case kDirLeft:      return {-1, 0};
    case kDirUpLeft:    return {-1, -1};
    default:            return {0, 0};
  }
}

/// 计算从源点 (sx, sy) 到目标点 (dx, dy) 的朝向
/// 规则：
/// - 水平/垂直差判断主方向
/// - 对角线方向需要 xy 均有差
/// - 当某轴差大于 2 时强制对齐到该轴方向
inline std::uint8_t next_direction(int sx, int sy, int dx, int dy) {
  int flag_x = 0;
  if (sx < dx) {
    flag_x = 1;
  } else if (sx > dx) {
    flag_x = -1;
  }
  if (std::abs(sy - dy) > 2 && sx >= dx - 1 && sx <= dx + 1) {
    flag_x = 0;
  }

  int flag_y = 0;
  if (sy < dy) {
    flag_y = 1;
  } else if (sy > dy) {
    flag_y = -1;
  }
  if (std::abs(sx - dx) > 2 && sy > dy - 1 && sy <= dy + 1) {
    flag_y = 0;
  }

  if (flag_x == 0 && flag_y == -1) return kDirUp;
  if (flag_x == 1 && flag_y == -1) return kDirUpRight;
  if (flag_x == 1 && flag_y == 0)  return kDirRight;
  if (flag_x == 1 && flag_y == 1)  return kDirDownRight;
  if (flag_x == 0 && flag_y == 1)  return kDirDown;
  if (flag_x == -1 && flag_y == 1) return kDirDownLeft;
  if (flag_x == -1 && flag_y == 0) return kDirLeft;
  if (flag_x == -1 && flag_y == -1) return kDirUpLeft;
  return kDirDown;
}

/// 检查坐标是否在地图边界内
inline bool in_bounds(int width, int height, int x, int y) {
  if (width <= 0 || height <= 0) {
    return true;  // 无效尺寸默认通过
  }
  return x >= 0 && y >= 0 && x < width && y < height;
}

/// 计算沿指定方向移动 distance 步后的目标
inline std::optional<MoveTarget> step_target(int width, int height, int sx, int sy,
                                             std::uint8_t dir, int distance) {
  const auto delta = direction_delta(dir);
  const auto x = sx + delta.dx * distance;
  const auto y = sy + delta.dy * distance;
  if (x == sx && y == sy) {
    return std::nullopt;
  }
  if (!in_bounds(width, height, x, y)) {
    return std::nullopt;
  }
  return MoveTarget{x, y, dir};
}

/// 计算走向目标点的行走目标（1 步）
inline std::optional<MoveTarget> requested_walk_target(int width, int height, int sx, int sy,
                                                       int requested_x, int requested_y) {
  const auto dir = next_direction(sx, sy, requested_x, requested_y);
  return step_target(width, height, sx, sy, dir, 1);
}

/// 计算走向目标点的跑步目标（2 步）
inline std::optional<MoveTarget> requested_run_target(int width, int height, int sx, int sy,
                                                      int requested_x, int requested_y) {
  const auto dir = next_direction(sx, sy, requested_x, requested_y);
  return step_target(width, height, sx, sy, dir, 2);
}

/// 计算从当前位置沿指定方向移动后的下一位置（带边界安全检测）
inline std::optional<MoveTarget> next_position(int width, int height, int sx, int sy,
                                               std::uint8_t dir, int distance) {
  int x = sx;
  int y = sy;
  switch (dir) {
    case kDirUp:
      if (y > distance - 1)         y -= distance;
      break;
    case kDirDown:
      if (height <= 0 || y < height - distance) y += distance;
      break;
    case kDirLeft:
      if (x > distance - 1)         x -= distance;
      break;
    case kDirRight:
      if (width <= 0 || x < width - distance) x += distance;
      break;
    case kDirUpLeft:
      if (x > distance - 1 && y > distance - 1) { x -= distance; y -= distance; }
      break;
    case kDirUpRight:
      if ((width <= 0 || x < width - distance) && y > distance - 1) { x += distance; y -= distance; }
      break;
    case kDirDownLeft:
      if (x > distance - 1 && (height <= 0 || y < height - distance)) { x -= distance; y += distance; }
      break;
    case kDirDownRight:
      if ((width <= 0 || x < width - distance) && (height <= 0 || y < height - distance)) { x += distance; y += distance; }
      break;
    default:
      break;
  }
  if (x == sx && y == sy) {
    return std::nullopt;
  }
  return MoveTarget{x, y, dir};
}

}  // namespace mir2::legacy
