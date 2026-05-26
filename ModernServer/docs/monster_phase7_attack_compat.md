# Monster Phase 7 Attack Compatibility

Generated for PR-7.

## Scope and Non-Goals

PR-7 locks the ordinary monster physical attack baseline. It keeps the existing
`Monster`, `MapActor`, and `RuntimeDispatch` implementation shape and does not
add a parallel attack-policy service.

Out of scope:

- SpitSpider, GasAttack, CowKing, Zuma, guard, summon, and other special Race
  attacks,
- monster skill probability, poison, paralysis, push, pull, and area attacks,
- death reward, drop, task, script, and boss notice ordering,
- full Delphi local-message delivery delays.

## Delphi Evidence Baseline

- `Source/M2Server/ObjMon.pas:353` implements `TMonster.AttackTarget`.
- `Source/M2Server/ObjMon.pas:359` checks `TargetInAttackRange`.
- `Source/M2Server/ObjMon.pas:360` uses strict
  `GetCurrentTime - HitTime > NextHitTime`.
- `Source/M2Server/ObjMon.pas:361` writes `HitTime := GetCurrentTime`.
- `Source/M2Server/ObjMon.pas:362` writes `TargetFocusTime := GetTickCount`.
- `Source/M2Server/ObjMon.pas:363` calls `Attack(TargetCret, targdir)`.
- `Source/M2Server/ObjMon.pas:364` calls `BreakHolySeize`.
- `Source/M2Server/ObjBase.pas:8245` implements `TAnimal.Attack` as
  `HitHit(target, HM_HIT, dir)`.
- `Source/M2Server/ObjBase.pas:5550` implements `TCreature.HitHit`.
- `Source/M2Server/ObjBase.pas:5638` through
  `Source/M2Server/ObjBase.pas:5641` set attack direction and choose the
  target creature.
- `Source/M2Server/ObjBase.pas:5653` calls `_Attack`.
- `Source/M2Server/ObjBase.pas:5687` sends the hit motion after `_Attack`.
- `Source/M2Server/ObjBase.pas:5252` implements `_Attack`.
- `Source/M2Server/ObjBase.pas:5433` through
  `Source/M2Server/ObjBase.pas:5439` apply `IsProperTarget` and
  `AccuracyPoint > Random(SpeedPoint)`.
- `Source/M2Server/ObjBase.pas:5443` calls `GetHitStruckDamage`.
- `Source/M2Server/ObjBase.pas:5448` calls `StruckDamage`.
- `Source/M2Server/ObjBase.pas:5449` through
  `Source/M2Server/ObjBase.pas:5450` enqueue delayed `RM_STRUCK`.
- `Source/M2Server/ObjBase.pas:3433` through
  `Source/M2Server/ObjBase.pas:3448` implement physical damage as
  `max(0, damage - random AC)`, with undead bonus and magic-bubble handling.
- `Source/M2Server/ObjBase.pas:3470` implements `StruckDamage`.
- `Source/M2Server/ObjBase.pas:3483` through
  `Source/M2Server/ObjBase.pas:3534` show struck-side durability rolls.

## C++ Anchors

- `ModernServer/src/world/map_actor_monster.hpp` owns
  `legacy_attack_target`, `legacy_monster_temp_attack`, and
  `legacy_monster_attack_monster`.
- `ModernServer/src/world/game_object.cpp` owns the strict
  `legacy_attack_due_by_hit_time` check and `mark_legacy_hit_time`.
- `ModernServer/src/world/map_actor_helpers.hpp` owns
  `apply_legacy_monster_damage` and PR-6 retaliation filtering.

## Compatibility Rules Locked By PR-7

- Ordinary `basic` and `aggressive` monsters attack only at Chebyshev distance
  `1`; existing C++ `ranged` and `stationary` profile ranges are compatibility
  shims, not the Delphi ordinary-monster baseline.
- Attack cadence remains strict: `now_ms - hit_time_ms > attack_speed_ms`.
- A target in attack range but still on hit cooldown consumes the monster turn:
  no movement, no attack packet, and no `HitTime` refresh.
- A target out of attack range only refreshes `TargetX/TargetY`; it does not
  refresh `HitTime` or send attack packets.
- A due attack updates direction, writes `HitTime`, refreshes target focus,
  broadcasts `SM_HIT`, then performs hit, damage, struck/death settlement, and
  finally breaks holy seize.
- Current-frame dispatch keeps `SM_HIT` before `SM_STRUCK` or `SM_DEATH`.
- Hit chance is `Random(SpeedPoint) < AccuracyPoint`; equality is a miss.
- Physical damage is `dc_min + Random(dc_max - dc_min + 1)` minus
  `ac_min + Random(ac_max - ac_min + 1)`, clamped to zero.

## Confirmed Unknowns

- Delphi `SendDelayMsg.deliverytime` is still not fully modeled. PR-7 locks
  current-frame packet order only and does not implement delayed local-message
  queue delivery.
- Special Race attack classes in `ObjMon.pas`, `ObjMon2.pas`, and
  `ObjAxeMon.pas` remain PR-8 scope.
- Death reward, drop, task, and script ordering remain PR-9 scope.

## Tests

- `mir2_monster_attack_legacy_smoke`
  - checks strict attack cooldown boundaries,
  - checks due melee attack packet order and deterministic damage,
  - checks out-of-range target pursuit without attack or `HitTime` refresh,
  - checks in-range cooldown blocks movement and attack output,
  - checks equality miss produces `SM_HIT` only and no durability update,
  - checks hit durability update is preserved,
  - checks player-owned summon damage uses the monster-target attack path,
  - checks death output keeps `SM_HIT` before `SM_DEATH`.
