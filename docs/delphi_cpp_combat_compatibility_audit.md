# Delphi/C++ Combat Compatibility Audit

Baseline: `origin/main@d7dcc0d7af37a91b2f1abaa9e1f741abc28d6f61`.

This file is the PR-1 stack refresh entry point. It does not redefine Delphi combat
semantics; it records what current `origin/main` already absorbed and points later
PRs at the frozen Delphi evidence under `docs/pr1_delphi_audit/`.

## Mainline Baseline

Current `origin/main` already contains, or partially contains, these map and
monster-compatibility tracks. They are not blockers for PR-2 through PR-5:

| Area | Baseline status | Keep in this stack |
|---|---|---|
| `safe_zone` combat blocking | Present on main and covered by `safe_zone_legacy_smoke.cpp`. | Keep existing smoke. |
| `area_state` sync | Present on main via map area-state sync paths. | Do not reopen unless combat changes regress it. |
| monster home leash | Present on main and covered by `monster_home_leash_smoke.cpp`. | Keep existing smoke. |
| visibility order | Present on main and covered by monster visibility/order smoke. | Do not rewrite in this stack. |
| monster `ATTACK_SPD >= 200` import clamp | Present on main through monster import/runtime speed floor. | Treat as importer/runtime baseline, not a combat compatibility bug. |

## Frozen Delphi Evidence

The stack consumes these existing PR-1 audit traces:

| Scenario | Evidence |
|---|---|
| `HitXY` cadence window | `docs/pr1_delphi_audit/golden_traces/attack_basic.json` |
| `SpellXY` cooldown and failure flow | `docs/pr1_delphi_audit/golden_traces/spell_failure.json` |
| `_Attack` long/wide/cross/fire chain | `attack_longhit.json`, `attack_widehit.json`, `attack_crosshit.json`, `attack_firehit.json` |
| `IsProperTarget + CheckAttackRule2` | `docs/pr1_delphi_audit/protocol_sequence.md` and `boundary_classification.md` |
| `SendDelayMsg` 200ms/500ms struck timing | `docs/pr1_delphi_audit/golden_traces/struck_delay_player_vs_monster.json` |
| death/revival/death packet order | `docs/pr1_delphi_audit/golden_traces/death_player.json` |

The CTest-facing fixture entry point is
`ModernServer/tests/golden/legacy_combat/combat_sequence_cases.json`. PR-6 also fixes the
canonical end-state list in
`ModernServer/tests/golden/legacy_combat/canonical_combat_snapshots.json`.

## Smoke Classification

Existing combat tests now have explicit roles in
`ModernServer/tests/golden/legacy_combat/combat_smoke_classification.json`.

`current_stability_smoke` means the test protects current working behavior or a
mainline-absorbed compatibility track. It is not by itself a Delphi parity proof
for the combat stack.

`delphi_parity_smoke` means the test should become or remain direct Delphi parity
coverage. Some entries intentionally carry `contains_known_parity_gap: true`; later
PRs must update those tests when the runtime behavior changes.

## Stack Ownership

| PR | Owned behavior |
|---|---|
| PR-2 | Player `HitXY` cadence, speed-hack counters, and removal of the player attack `max(200, ...)` wrapper. |
| PR-3 | Hit RNG, skill 4 sword-skill inclusion, HeavyHit/BigHit multiplier cleanup, and random-call order. |
| PR-4 | Warrior sword target chains and 200ms/500ms struck delay timing. |
| PR-5 | PK target rules and per-command action throttling instead of a global gameplay-action frame gate. |
| PR-6 | Death/revival/downlink order, poison/firewall/magic-shield timing, and remaining monster combat timing. |

## Non-Goals For PR-1

- No runtime logic changes.
- No packet order changes.
- No map/monster rewrite for behavior already present on main.
