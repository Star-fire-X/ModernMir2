# Monster Phase 4 Runtime AI Compatibility

Generated for PR-4.

This document records the ordinary monster runtime-state and base AI boundary
after PR-4. It is a compatibility baseline for later movement, target
selection, special Race, death/drop, and broadcast PRs; it is not a new monster
architecture.

## Scope

PR-4 keeps the current C++ `Monster` object as the monster runtime state. It
does not introduce a parallel `MonsterRuntimeState` service or class.

PR-4 only makes the legacy timing state observable through `MonsterSnapshot`,
aligns the C++ `RunTime` update boundary with Delphi `ProcessMonsters`, and
locks the current ordinary `TMonster` / `TATMonster`-style AI order with smoke
coverage.

Out of scope:

- Delphi `VisibleActors` list-order target selection.
- Complete local message queue ordering from `TCreature.Run`.
- Initial `RunTime := GetCurrentTime + Random(1500)` RNG parity.
- Special Race behavior.
- Death, drop, task, script, and broadcast ordering.

## Delphi Runtime Timing Evidence

`Source/M2Server/UsrEngn.pas:2295` gates each monster with:

```text
tcount - cret.RunTime > cret.RunNextTick
```

`Source/M2Server/UsrEngn.pas:2296` updates `cret.RunTime := tcount` before
calling `cret.Run`.

`Source/M2Server/UsrEngn.pas:2297` checks:

```text
GetTickCount > cret.SearchRate + cret.SearchTime
```

`Source/M2Server/UsrEngn.pas:2298` updates `cret.SearchTime` when that strict
boundary is crossed.

`Source/M2Server/ObjBase.pas:1116` initializes ordinary creature `RunTime` as
`GetCurrentTime + Random(1500)`. This remains a confirmed gap because changing
it in PR-4 would alter legacy RNG consumption order before the target-selection
and spawn PRs are ready to audit the cascade.

`Source/M2Server/ObjBase.pas:1117` sets `RunNextTick := 250`.
`Source/M2Server/ObjBase.pas:1118` sets the base creature `SearchRate`.
`Source/M2Server/ObjBase.pas:1119` initializes `SearchTime := GetTickCount`.

`Source/M2Server/ObjMon.pas:275` sets monster `RunNextTick := 250`.
`Source/M2Server/ObjMon.pas:276` sets normal monster `SearchRate`.
`Source/M2Server/ObjMon.pas:277` initializes monster `SearchTime`.

## C++ Runtime State Boundary

`ModernServer/src/world/game_object.hpp:596` defines `MonsterSnapshot`. PR-4
adds these observable timing fields:

| Snapshot field | C++ source field | Delphi meaning |
| --- | --- | --- |
| `legacy_run_time_ms` | `Monster::run_time_ms_` | `RunTime` |
| `legacy_run_next_tick_ms` | `Monster::run_next_tick_ms_` | `RunNextTick` |
| `legacy_search_time_ms` | `Monster::search_time_ms_` | `SearchTime` |
| `legacy_search_rate_ms` | `Monster::search_rate_ms_` | `SearchRate` |

`ModernServer/src/world/game_object.cpp:2166` fills `MonsterSnapshot`.

`ModernServer/src/world/game_object.cpp:2793` initializes legacy AI timers for
spawned monsters. PR-4 sets `search_time_ms_ = now_ms` there so a spawned
monster carries the same `SearchTime` baseline concept as Delphi.

`ModernServer/src/world/map_actor.cpp:1609` implements
`MapActor::legacy_process_monster`. PR-4 keeps the strict `legacy_due` boundary
and moves `mark_legacy_run_time(now_ms)` before `handle_monster_ai`, matching
Delphi's `cret.RunTime := tcount` before `cret.Run`.

## Ordinary Monster Run Order

The Delphi baseline from PR-1 remains:

- `Source/M2Server/ObjMon.pas:376`: `TMonster.Run`.
- `Source/M2Server/ObjMon.pas:535`: `TATMonster.Run`.
- `Source/M2Server/ObjBase.pas:7552`: `TCreature.Run`.

Current C++ ordinary AI remains in
`ModernServer/src/world/map_actor_monster.hpp:1786`
`MapActor::handle_monster_ai`:

```text
legacy_active_search
-> legacy_monster_think
-> walk-wait / walk cadence
-> special behavior hook
-> legacy_attack_target
-> slave follow / return handling
-> legacy_goto_target_xy
-> legacy_wondering
```

`ModernServer/src/world/map_actor_monster.hpp:886` contains active pre-search.
It approximates Delphi `TATMonster.Run`'s `SearchEnemyTime` cadence, but it
does not yet reproduce Delphi `VisibleActors` traversal order.

`ModernServer/src/world/map_actor_monster.hpp:944` contains attack handling.
PR-4 locks the existing legacy feel that a monster already in attack range
does not move during hit cooldown.

## Deferred Compatibility Gaps

These are intentionally not fixed in PR-4:

- Target search still scans current C++ map objects, not Delphi
  `SearchViewRange + VisibleActors` list order.
- `TCreature.Run` local message queue order is not fully represented in the
  current C++ ordinary monster loop.
- Initial `RunTime + Random(1500)` is documented but not enabled to avoid
  unaudited RNG-order changes.
- Special Race branches in `ObjMon.pas`, `ObjMon2.pas`, and `ObjAxeMon.pas`
  stay in the later special-AI PR.
- Death, corpse, ghost, drop scatter, task, script, and broadcast ordering stay
  in later PRs.

## PR-4 Smoke Coverage

`ModernServer/tests/monster_base_object_smoke.cpp` now checks the exposed
legacy timing fields on a spawned monster template.

`ModernServer/tests/monster_legacy_tick_ai_smoke.cpp` now checks:

- strict `RunNextTick` `>` behavior,
- run-time update after a due monster run,
- strict `SearchRate + SearchTime` boundary,
- active pre-search cadence,
- passive/basic monster no-pre-search behavior,
- no movement during hit cooldown,
- target invalidation cleanup,
- walk-wait blocking and resume behavior.
