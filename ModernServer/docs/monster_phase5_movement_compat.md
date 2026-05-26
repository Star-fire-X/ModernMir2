# Monster Phase 5 Movement Compatibility

## Scope and Non-Goals

PR-5 fixes the ordinary monster movement compatibility boundary only. It keeps
the existing `Monster`, `MapActor`, and `LegacyMapEnvironment` runtime shape and
does not add a parallel movement service.

Out of scope:

- target search order and Delphi `VisibleActors` parity,
- attack damage, hit resolution, skill probabilities, or special Race AI,
- monster death, drop, task, script, or broadcast protocol redesign,
- async movement, A*, path queues, behavior trees, or ECS scheduling.

## Delphi Evidence Baseline

The local Delphi tree is not available in this worktree, so this PR uses the
phase-1 trace as the source evidence baseline:

- `ModernServer/docs/monster_phase1_delphi_trace.md:327` records
  `TMonster.Run` calling `GotoTargetXY` when `TargetX <> -1`.
- `ModernServer/docs/monster_phase1_delphi_trace.md:328` records `Wondering`
  as the fallback when there is no target coordinate.
- `ModernServer/docs/monster_phase1_delphi_trace.md:432` records
  `Source/M2Server/ObjBase.pas:8325` as the `GotoTargetXY` implementation.
- `ModernServer/docs/monster_phase1_delphi_trace.md:312` through
  `ModernServer/docs/monster_phase1_delphi_trace.md:315` record
  `WalkTime := GetCurrentTime` before `AttackTarget`, `GotoTargetXY`, or
  `Wondering`.
- `ModernServer/docs/monster_phase1_delphi_trace.md:436` records
  `WalkTo(wantdir, FALSE)`.
- `ModernServer/docs/monster_phase1_delphi_trace.md:441` records
  `Wondering`.
- `ModernServer/docs/monster_phase1_delphi_trace.md:442` records the
  `Random(20) = 0` wandering gate.
- `ModernServer/docs/monster_phase1_delphi_trace.md:448` records `WalkTo`.
- `ModernServer/docs/monster_phase1_delphi_trace.md:458` records
  `PEnvir.MoveToMovingObject(..., allowdup)`.

The exact Delphi side-probe direction priority after the first blocked
`wantdir` still needs source confirmation. PR-5 therefore preserves the current
local side-probe implementation and only locks the confirmed first-direction,
walkability, collision, RNG gate, and pre-action walk cadence boundaries.

## C++ Anchors

- `ModernServer/src/world/map_actor_monster.hpp` owns ordinary monster movement:
  `legacy_goto_target_xy`, `legacy_wondering`, `legacy_try_monster_walk`, and
  the movement portion of `handle_monster_ai`.
- `ModernServer/src/world/legacy_map_environment.cpp` owns the map walkability
  and moving-object occupancy checks used by `move_to_moving_object`.
- `ModernServer/src/world/game_object.cpp` owns the observable runtime timers:
  `walk_time_ms`, `walk_wait_mode`, `target_x`, `target_y`, and home-area state.

## Compatibility Rules Locked By PR-5

- A monster walk is committed only after `move_to_moving_object(..., false)`
  succeeds.
- Failed movement does not update position and does not emit `SM_WALK`.
- `walk_time_ms` and walk-step cadence are still consumed before
  `AttackTarget`, `GotoTargetXY`, or `Wondering`, matching the phase-1 Delphi
  evidence.
- Normal monsters use `allow_dup = false`; occupied moving-object cells and
  blocked static map cells stop the movement attempt.
- `Wondering` keeps the legacy `Random(20) == 0` gate. A non-zero gate result
  does not move, turn, or broadcast, but the monster has already consumed this
  walk cadence slice.
- An ordinary non-special monster outside its configured home area clears the
  temporary target and returns through the same `GotoTargetXY -> Walk` path. It
  does not attack a nearby player while the forced return is active.
- Special Race movement remains on its existing special AI path.
- Movement broadcasts remain synchronous with the monster phase dispatch:
  successful walk first updates the monster, then queues the existing `SM_WALK`
  packet, then runs visibility sync.

## Tests

- `mir2_monster_movement_legacy_smoke`
  - direct target-coordinate chase moves one tile in the expected first
    direction and emits one `SM_WALK`;
  - all-neighbor static blocking prevents movement, emits no `SM_WALK`, and
    still applies the attempted direction;
  - `allow_dup = false` blocks an occupied first tile when all side tiles are
    statically blocked;
  - fixed legacy RNG seed `2` verifies `Random(20) != 0` wandering no-op;
  - fixed legacy RNG seed `1` verifies `Random(20) == 0` wandering walk.
- `mir2_monster_home_leash_smoke` now asserts forced return clears the player
  target and preserves the home target coordinate after the first return step.

## Confirmed Unknowns

- Exact Delphi side-probe order after a blocked `GotoTargetXY` first direction:
  needs source confirmation before changing the current C++ side-probe loop.
- Special Race movement overrides remain PR-8 scope.
- Full nine-grid visibility resync ordering remains PR-10 scope.
