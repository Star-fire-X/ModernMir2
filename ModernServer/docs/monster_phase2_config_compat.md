# Monster Phase 2 Config Compatibility

Generated for PR-2.

This document records the monster configuration compatibility layer used by the
ModernServer migration. PR-2 keeps the existing C++ config structs as the
internal monster-template representation and does not change monster AI,
scheduling, movement, combat, death, drops, or protocol ordering.

## Scope

PR-2 covers only legacy monster input data:

- Delphi `Monster` table fields loaded by `LoadMonsters`.
- Delphi `Envir/MonGen.txt` spawn records loaded by `LoadZenLists`.
- Delphi `Envir/MonItems/*.txt` drop records loaded by `LoadMonItems`.
- Text fixture import and config loading behavior that can run in CI without
  Access/ODBC.

`MonsterType` was not confirmed in the PR-1 Delphi source trace, so PR-2 does
not add a `MonsterType` field.

## Monster Field Mapping

`MonsterDefConfig` remains the C++ monster-template shape for PR-2.

| Delphi field | C++ field | Notes |
| --- | --- | --- |
| `NAME` | `name` | Empty names are skipped. |
| `Race` | `race_server` | Alias `race` is accepted. |
| `RaceImg` | `race_image` | Alias `race_img` is accepted. |
| `IMGINDEX` | `appearance` | Alias `img_index` is accepted. |
| `Lv` | `level` | Alias `lv` is accepted. |
| `Undead` | `undead` | Numeric or bool TOML values are accepted. |
| `CoolEye` | `cool_eye` | Kept as legacy integer probability. |
| `Exp` | `exp` | Fight exp value. |
| `HP` / `MP` | `hp` / `mp` | Stored directly. |
| `AC` / `MAC` | `ac` / `mac` | Stored directly; runtime packing is separate. |
| `DC` / `DCMAX` | `dc` / `dc_max` | Alias `dcmax` is accepted. |
| `MC` / `SC` | `mc` / `sc` | Stored directly. |
| `AGILITY` | `agility` | Maps to monster speed/evasion point. |
| `ACCURATE` | `accurate` | Maps to monster hit/accuracy point. |
| `WALK_SPD` | `walk_speed_ms` | Alias `walk_spd`; clamped to at least `200`. |
| `WalkStep` | `walk_step` | Clamped to at least `1` by runtime spawn setup. |
| `WalkWait` | `walk_wait_ms` | Alias `walk_wait`. |
| `ATTACK_SPD` | `attack_speed_ms` | Alias `attack_spd`; clamped to at least `200`. |

The 200 ms speed floor matches Delphi `LoadMonsters`, where both walk speed and
attack speed are forced to at least `200`.

## Spawn and Drop Import

`LegacyImporter::import_tree` writes:

- `spawns/imported_monsters.toml` from `Envir/MonGen.txt`.
- `monsters/imported_drops.toml` from `Envir/MonItems/*.txt`.
- `monsters/imported_monsters.toml` from `Data.mdb` when ODBC is enabled, or
  a small placeholder set with a warning when ODBC is disabled.

`MonGen.txt` import preserves the legacy fields:

```text
MapName X Y "MonName" Area Count ZenMinutes SmallZenRate
```

Quoted names are preserved, and unquoted multi-token names are recovered by
reading the trailing numeric spawn fields from the right side of the record.
The output always marks these records as `legacy_group = true`, and writes
`area`, `count`, `zen_minutes`, and `small_zen_rate` instead of collapsing the
record into a single-point `respawn_ms` spawn.

`MonItems` import supports both chance formats:

```text
1/2 "Wooden Sword" 2
1 2 Wooden Sword 2
```

The output keeps Delphi probability semantics by writing `sel_point = n - 1`
and `max_point = m`. Item `count` defaults to `1` when omitted. `MonItems`
files are sorted by path before export so generated drop order is stable.

## ODBC Boundary

Full `Monster` table extraction still depends on `MIR2_ENABLE_ODBC` and a local
Access/ODBC driver. PR-2 does not add a new database dependency for CI.

When ODBC is unavailable, the importer keeps the existing placeholder monster
definitions and emits a warning. Text-based MonGen and MonItems import remains
covered by CI either way.

## Follow-Up Gates

Later monster PRs must still handle:

- Script and GM monster spawns using the same monster definitions and drop
  preload path.
- Race-specific AI behavior and special class mapping.
- `SearchViewRange` / `VisibleActors` target order.
- Death, exp, map quest, drop scatter, and `RM_DEATH` ordering.
- Legacy broadcast ordering and local message queue timing.
