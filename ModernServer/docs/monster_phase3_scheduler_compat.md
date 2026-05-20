# Monster Phase 3 Scheduler Compatibility

Generated for PR-3.

This document records the current C++ monster scheduler boundary after PR-3.
The goal is to pin the Delphi `ProcessMonsters` phase semantics before later
PRs change monster AI, target selection, movement, combat, death, drops, or
broadcast ordering.

## Scheduler Boundary

PR-3 keeps the scheduler inside `LogicRuntime::process_monsters`; it does not
introduce a separate `LegacyMonsterScheduler` runtime service.

The legacy creature phase order remains:

```text
ProcessUserHumans
-> ProcessMonsters
-> ProcessMerchants
-> ProcessNpcs
```

`LogicRuntime::process_monsters` owns only the outer Delphi scheduler state:

| Delphi state | C++ state |
| --- | --- |
| `MonList` | `monster_groups_` |
| `GenCur` | `gen_cur_` |
| `MonCur` | `mon_cur_` |
| `MonSubCur` | `mon_sub_cur_` |
| `onezentime` | `one_zen_time_ms_` |
| `MonLimitTime` | `config_.budgets.monster_budget_ms` |

## Spawn Group Polling

The scheduler checks at most one monster spawn group per `ProcessMonsters`
call. It checks the group at `gen_cur_` only when:

```text
now_ms > one_zen_time_ms_ + 200
```

After the check, `gen_cur_` advances by one and wraps at the end of
`monster_groups_`. Large wall-clock jumps do not catch up multiple spawn
groups in one frame.

## Monster Traversal

Monster traversal starts from `mon_cur_`. Within that group, traversal resumes
from `mon_sub_cur_`; if the saved sub cursor is outside the current group, it
starts at `0`.

Each monster ref is dispatched immediately through:

```text
MapActor::legacy_process_monster(actor_id, current_tick_, now_ms, group_index, sub_index)
```

The returned dispatch is appended immediately, preserving stable trace and
message order for the current map/group traversal. When traversal completes
without budget exhaustion, `mon_cur_` and `mon_sub_cur_` reset to `0`.

## Budget Semantics

Positive `monster_budget_ms` mirrors the Delphi `MonLimitTime` boundary by
stopping only when elapsed scheduler time is strictly greater than the budget.

`monster_budget_ms == 0` remains a deterministic smoke-test mode: it processes
one monster ref and saves the next cursor. This mode is not a Delphi runtime
configuration; it exists so cursor behavior can be tested without relying on
wall-clock timing.

When the budget is exhausted, the scheduler saves the next monster position:

- Same group: `mon_cur_` stays on the current group and `mon_sub_cur_` becomes
  the next monster index.
- End of group: `mon_cur_` advances to the next group and `mon_sub_cur_`
  becomes `0`.
- End of all groups: both cursors wrap to `0`.

## Out Of Scope

`RunNextTick`, `SearchRate`, `SearchTime`, active search, movement, attack,
special Race behavior, death/ghost/drop timing, and protocol broadcasts still
live below the scheduler in `MapActor::legacy_process_monster` and the monster
helpers. PR-3 does not claim parity for those behaviors; they remain follow-up
work for later monster migration PRs.
