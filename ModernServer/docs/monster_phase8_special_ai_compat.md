# Monster Phase 8 Special AI Compatibility

Generated for PR-8.

## Scope and Non-Goals

PR-8 locks the current special Race compatibility baseline on the existing
`Monster`, `MapActor`, `RuntimeDispatch`, and `LegacyMapEnvironment` paths. It
does not add a new special-AI registry or change ordinary monster combat,
ordinary target selection, death/drop settlement, or protocol structures.

Out of scope:

- CowKing, DeadCowKing, BanyaGuard, LightingZombi, SoccerBall, elf, explosion
  spider, and other special classes not yet represented by
  `LegacyMonsterRaceBehavior`,
- boss notices, task/drop ordering, and corpse/ghost/respawn settlement,
- full Delphi local delayed-message delivery semantics.

## Delphi Evidence Baseline

The Race-to-class matrix remains the PR-1 source baseline:

- `Source/M2Server/UsrEngn.pas:796` maps `RC_SPITSPIDER` to `TSpitSpider`.
- `Source/M2Server/UsrEngn.pas:828` maps `RC_BIGKUDEKI` to
  `TGasAttackMonster`.
- `Source/M2Server/UsrEngn.pas:838` maps `RC_MAGCOWFACEMON` to
  `TMagCowMonster`.
- `Source/M2Server/UsrEngn.pas:900` maps `RC_GASMOTH` to
  `TGasMothMonster`.
- `Source/M2Server/UsrEngn.pas:905` maps `RC_DUNG` to `TGasDungMonster`.
- `Source/M2Server/UsrEngn.pas:910` maps `RC_CENTIPEDEKING` to
  `TCentipedeKingMonster`.
- `Source/M2Server/UsrEngn.pas:925` maps `RC_SPIDERHOUSEMON` to
  `TSpiderHouseMonster`.
- `Source/M2Server/UsrEngn.pas:935` and `Source/M2Server/UsrEngn.pas:940`
  map high-risk and big-poison spiders to spider variants.
- `Source/M2Server/UsrEngn.pas:983`, `Source/M2Server/UsrEngn.pas:988`,
  and `Source/M2Server/UsrEngn.pas:993` map castle doors, walls, and archer
  guards to structure/guard classes.
- `Source/M2Server/ObjMon.pas:632` implements `TSpitSpider.AttackTarget`.
- `Source/M2Server/ObjMon.pas:741` implements gas attack target handling.
- `Source/M2Server/ObjMon.pas:1346` shows `TGasMothMonster.GasAttack`.
- `Source/M2Server/ObjMon2.pas:333` implements `TBeeQueen.Run`.
- `Source/M2Server/ObjMon2.pas:393` implements
  `TCentipedeKingMonster.AttackTarget`.
- `Source/M2Server/ObjMon2.pas:627` implements `TSpiderHouseMonster.Run`.
- `Source/M2Server/ObjMon2.pas:852` implements `TArcherGuard.Run`.
- `Source/M2Server/ObjMon2.pas:1001` and
  `Source/M2Server/ObjMon2.pas:1061` implement castle door and wall run paths.

## C++ Anchors

- `ModernServer/src/world/map_actor_helpers.hpp:64` defines
  `LegacyMonsterRaceBehavior`.
- `ModernServer/src/world/map_actor_helpers.hpp:79` maps Race values to
  `LegacyMonsterRaceBehavior`.
- `ModernServer/src/world/game_object.cpp:2209` through
  `ModernServer/src/world/game_object.cpp:2277` initialize per-Race runtime
  fields such as chain-shot counts, hide/stick mode, summon names, dig ranges,
  and special search cadence.
- `ModernServer/src/world/map_actor_monster.hpp:1345` implements child summon
  creation.
- `ModernServer/src/world/map_actor_monster.hpp:1412` implements special
  attack execution.
- `ModernServer/src/world/map_actor_monster.hpp:1677` implements special run
  behavior.

## Compatibility Locked By PR-8

- `RC_GASMOTH` and `RC_DUNG` now map to the existing `front_gas` behavior
  instead of falling through to ordinary AI.
- Non-hidden special attackers (`spit`, `front_gas`, `front_magic`, and
  `fly_axe`) select a nearby player after the legacy no-target
  `SearchEnemyTime > 1000 ms` boundary before calling the special attack path,
  so they do not depend on the ordinary `MonsterNormalAttack` pre-run search.
- Spit spider variants keep the current C++ distinction where HighRiskSpider
  does not enter the poison gate and the other spit variants do.
- BigKudeki, GasMoth, GasDung, and ToxicGhost enter the gas hit and poison gate
  path; MagCow enters the anti-magic gate path.
- DualAxe, ThornDark, and ArcherMon keep distinct chain-shot counts.
- KillingHerb, DigOutZombi, CentipedeKing, and ScultureKing keep hide/stick
  and dig-up behavior on the special path.
- BeeQueen and SpiderHouse keep summon limits and child summon labels.
- ArcherGuard keeps guard filtering: calm blue players are ignored, red players
  can trigger the ranged guard attack.
- CastleDoor and Wall remain non-moving structure behavior.

## Current Gaps

- `RC_COWFACEKINGMON`, `RC_DEADCOWKING`, and `RC_BANYAGUARD` have Delphi range
  attack evidence in `ObjMon.pas`, but the exact current C++ equivalent is not
  implemented in PR-8. They remain high-risk special Race gaps.
- `TGasMothMonster` can break human hide through its Delphi `GasAttack`; the
  current C++ gas path records the gas/poison gates but does not yet model that
  hide-breaking side effect.
- `MonsterDetecterAttack`, `BoViewFixedHide`, and full hidden-target detector
  rules remain incomplete.
- ScultureKing follower calls and no-follower variants need a dedicated source
  pass before behavior is declared complete.
- The local delayed-message timings for special struck effects remain a known
  PR-1/PR-7 compatibility gap.

## Tests

- `mir2_monster_special_race_smoke`
  - checks mapped Race initialization for fly-axe, summoner, dig, guard, and
    structure groups,
  - checks SpitSpider, HighRiskSpider, and BigPoisonSpider attack/poison entry,
  - checks BigKudeki, MagCow, GasMoth, GasDung, and ToxicGhost special attack
    gates,
  - checks StickHide, DigOutZombi, CentipedeKing, and ScultureKing dig-up,
  - checks BeeQueen and SpiderHouse summon,
  - checks ArcherGuard calm and red-player attack behavior.
- `mir2_monster_race_ai_smoke`
  - now uses non-interactive fail-fast checks and direct `MapActor` execution
    to avoid MSVC Debug assertion popups and runtime spawn-timing noise.
