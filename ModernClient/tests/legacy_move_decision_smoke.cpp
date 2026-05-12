#include "shared/legacy/movement_rules.hpp"

#include <cassert>
#include <set>
#include <utility>

namespace {

using Point = std::pair<int, int>;

auto map_checker(const std::set<Point>& blocked) {
  return [&blocked](const int x, const int y) { return blocked.count({x, y}) == 0; };
}

auto crash_checker(const std::set<Point>& actors) {
  return [&actors](const int x, const int y) { return actors.count({x, y}) != 0; };
}

}  // namespace

int main() {
  using namespace mir2::legacy;

  const std::set<Point> none;
  auto decision = resolve_legacy_walk(100, 100, 10, 10, 12, 10, kDirDown,
                                      map_checker(none), crash_checker(none));
  assert(decision.kind == LegacyMoveDecisionKind::walk);
  assert(decision.x == 11 && decision.y == 10 && decision.dir == kDirRight);

  const std::set<Point> forward_blocked{{11, 10}};
  decision = resolve_legacy_walk(100, 100, 10, 10, 12, 10, kDirDown,
                                 map_checker(forward_blocked), crash_checker(none));
  assert(decision.kind == LegacyMoveDecisionKind::walk);
  assert(decision.x == 11 && decision.y == 9 && decision.dir == kDirUpRight);

  const std::set<Point> left_and_forward_blocked{{11, 10}, {11, 9}};
  decision = resolve_legacy_walk(100, 100, 10, 10, 12, 10, kDirDown,
                                 map_checker(left_and_forward_blocked), crash_checker(none));
  assert(decision.kind == LegacyMoveDecisionKind::walk);
  assert(decision.x == 11 && decision.y == 11 && decision.dir == kDirDownRight);

  const std::set<Point> actor_blocking_forward{{11, 10}};
  decision = resolve_legacy_walk(100, 100, 10, 10, 12, 10, kDirDown,
                                 map_checker(none), crash_checker(actor_blocking_forward));
  assert(decision.kind == LegacyMoveDecisionKind::turn);
  assert(decision.x == 10 && decision.y == 10 && decision.dir == kDirRight);

  assert(legacy_can_run(100, 100, 10, 10, 12, 10, map_checker(none)));
  assert(!legacy_can_run(100, 100, 10, 10, 12, 10, map_checker(forward_blocked)));
  const std::set<Point> endpoint_blocked{{12, 10}};
  assert(!legacy_can_run(100, 100, 10, 10, 12, 10, map_checker(endpoint_blocked)));
  return 0;
}
