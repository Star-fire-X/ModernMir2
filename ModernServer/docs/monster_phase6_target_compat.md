# Monster Phase 6 Target Compatibility

Generated for PR-6.

## Scope and Non-Goals

PR-6 fixes the ordinary monster target-search and target-invalidity baseline.
It keeps the existing `Monster` and `MapActor` runtime shape and does not add a
parallel target-selector service.

Out of scope:

- movement, attack damage, skill probability, or special Race AI changes,
- death, drop, task, script, or protocol ordering changes,
- GM/supervisor state plumbing into `MapActor`,
- full Delphi `RefObjCount` reverse-view bookkeeping.

## Delphi Evidence Baseline

The clean PR-6 worktree does not contain the Delphi `Source` tree, so this PR
uses the phase-1 trace plus read-only local source evidence as the baseline.

- `Source/M2Server/ObjBase.pas:1690` implements `TCreature.SearchViewRange`.
- `Source/M2Server/ObjBase.pas:1734` and `Source/M2Server/ObjBase.pas:1735`
  scan `x` outside `y`.
- `Source/M2Server/ObjBase.pas:1741` through
  `Source/M2Server/ObjBase.pas:1743` scan each cell `ObjList` from index `0`
  upward.
- `Source/M2Server/ObjBase.pas:1607` implements `UpdateVisibleGay`.
- `Source/M2Server/ObjBase.pas:1615` through
  `Source/M2Server/ObjBase.pas:1621` keep an existing visible actor in place.
- `Source/M2Server/ObjBase.pas:1627` through
  `Source/M2Server/ObjBase.pas:1629` append a newly visible actor.
- `Source/M2Server/ObjBase.pas:1847` through
  `Source/M2Server/ObjBase.pas:1862` remove actors not checked by the latest
  view scan.
- `Source/M2Server/ObjBase.pas:8277` implements `TAnimal.MonsterNormalAttack`.
- `Source/M2Server/ObjBase.pas:8285` through
  `Source/M2Server/ObjBase.pas:8292` iterate `VisibleActors`, filter death,
  `IsProperTarget`, and hidden humans unless `BoViewFixedHide`, then choose the
  nearest Manhattan-distance target with strict `d < dis`.
- `Source/M2Server/ObjBase.pas:7970` implements `_IsProperTarget`.
- `Source/M2Server/ObjBase.pas:7983` through
  `Source/M2Server/ObjBase.pas:8000` show ordinary unowned monsters targeting
  humans and master-owned creatures, while `BoSysopMode` and `BoStoneMode`
  remain future state-plumbing gaps.
- `Source/M2Server/ObjMon.pas:535` implements `TATMonster.Run`.
- `Source/M2Server/ObjMon.pas:540` calls `MonsterNormalAttack`.

## C++ Anchors

- `ModernServer/src/world/game_object.hpp` owns the observable
  `MonsterSnapshot` and the new legacy visible actor cache.
- `ModernServer/src/world/map_actor.cpp` owns the
  `legacy_process_monster` `RunNextTick` and `SearchRate` boundary.
- `ModernServer/src/world/map_actor_monster.hpp` owns ordinary active search,
  proper-target filtering, and current-target invalidation.
- `ModernServer/src/world/legacy_map_environment.hpp` owns cell `obj_list`
  order.

## Compatibility Rules Locked By PR-6

- `SearchTime` and visible actors refresh only inside a due monster run:
  `mark_legacy_run_time -> mark_legacy_search_time -> refresh visible actors
  -> handle_monster_ai`.
- Status-only monster ticks do not refresh `SearchTime` or visible actors.
- Visible scanning uses Delphi-style `x` outer, `y` inner, and per-cell
  `obj_list` forward order.
- Ordinary monster visible scanning uses Delphi `TMonster.ViewRange = 5`;
  special Race-specific view ranges remain PR-8 scope.
- Existing visible actor ids keep their order; newly seen actor ids append in
  the latest scan order.
- `MonsterNormalAttack` no longer depends on `objects_` / `unordered_map`
  iteration order.
- Ordinary target ranking uses Manhattan distance and strict `<`, so equal
  distance keeps the earlier visible actor.
- Ordinary monsters can select live players and player-owned summons/slaves.
  They do not select themselves, wild monsters, same-master monsters, dead
  targets, ghost targets, safe-zone players, or transparent players.
- Current target invalidity clears `target_actor_id`, `target_x`, and
  `target_y`.
- Damage retaliation still selects an attacker only when the source is a proper
  player target or a live player-owned summon/slave under the same target
  filters.

## Confirmed Unknowns

- `BoSysopMode` / `BoStoneMode` are not yet represented in `MapActor` moving
  state; PR-6 documents the Delphi branch but does not add GM state plumbing.
- Full Delphi `RefObjCount` reverse-visible bookkeeping is not replicated.
  PR-6 refreshes the monster-side visible cache on the `SearchRate` due
  boundary only.
- `MonsterDetecterAttack`, `BoViewFixedHide`, guards, and special Race hidden
  target rules remain PR-8 scope.
- Special Race-specific `ViewRange` overrides remain PR-8 scope. PR-6 locks
  the ordinary `TMonster` baseline only.

## Tests

- `mir2_monster_target_selection_legacy_smoke`
  - verifies visible cache preserve/append order,
  - verifies equal-distance tie keeps the earlier visible actor,
  - verifies a later closer target wins on the next eligible search,
  - verifies safe-zone, transparent, dead, and disconnected targets clear or
    fail selection,
  - verifies ordinary monsters choose player-owned summons and ignore wild
    monsters,
  - verifies status-only ticks do not refresh visible actors or `SearchTime`.
- Existing `mir2_monster_legacy_tick_ai_smoke` expectations that require a
  1001ms active search now specify an explicit short `SearchRate`, matching the
  PR-6 SearchTime boundary.
