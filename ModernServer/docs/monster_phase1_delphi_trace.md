# Monster Phase 1 Delphi Trace

Generated for PR-1.

This document freezes the Delphi M2Server monster-system evidence that later
monster migration PRs must preserve. The goal is behavior compatibility with
the old Mir2 server, not a redesigned MMO monster system.

## Scope and Non-Goals

Scope:

- Legacy server main-loop position for monsters.
- `ProcessMonsters` scheduling, spawn-group polling, and budget cursor state.
- Normal monster run order, target search, movement, attack, local messages,
  death, drops, ghost removal, and Race-based class dispatch.
- Current C++ anchor points that later PRs must compare against.

Non-goals:

- No C++ runtime behavior changes in PR-1.
- No new `MonsterTemplateLoader`, scheduler, AI, drop, or protocol code in
  PR-1.
- No CTest expectation changes in PR-1.
- No attempt to modernize monster behavior, pathing, target selection, or
  message ordering.

`Source/M2Server` is the primary Delphi source tree for this phase.
`Source/Mir200` appears to contain a close related copy, but PR-1 does not
establish it as a second authority.

Primary evidence files covered in this trace:

- `Source/M2Server/svMain.pas`
- `Source/M2Server/UsrEngn.pas`
- `Source/M2Server/ObjBase.pas`
- `Source/M2Server/ObjMon.pas`
- `Source/M2Server/ObjMon2.pas`
- `Source/M2Server/ObjAxeMon.pas`
- `Source/M2Server/LocalDB.pas`
- `Source/Common/Grobal2.pas`
- `ModernServer/src/world/legacy_frame_driver.cpp`
- `ModernServer/src/world/logic_runtime.cpp`
- `ModernServer/src/world/map_actor.cpp`
- `ModernServer/src/world/map_actor_monster.hpp`
- `ModernServer/src/config/models.hpp`

## Legacy Main Loop

Delphi drives the old server from the VCL timer:

- `Source/M2Server/svMain.dfm:139` defines `RunTimer` with interval `1`.
- `Source/M2Server/svMain.pas:1241` implements `TFrmMain.RunTimerTimer`.
- `Source/M2Server/svMain.pas:1244` calls `RunSocket.Run`.
- `Source/M2Server/svMain.pas:1246` calls `FrmIDSoc.DecodeSocStr`.
- `Source/M2Server/svMain.pas:1248` calls `UserEngine.ExecuteRun`.
- `Source/M2Server/svMain.pas:1250` calls `EventMan.Run`.

`UserEngine.ExecuteRun` is the legacy creature phase boundary:

- `Source/M2Server/UsrEngn.pas:2525` implements `TUserEngine.ExecuteRun`.
- `Source/M2Server/UsrEngn.pas:2531` calls `ProcessUserHumans`.
- `Source/M2Server/UsrEngn.pas:2533` calls `ProcessMonsters`.
- `Source/M2Server/UsrEngn.pas:2535` calls `ProcessMerchants`.
- `Source/M2Server/UsrEngn.pas:2537` calls `ProcessNpcs`.

The monster migration must preserve this relative order:

```text
RunSocket.Run
-> FrmIDSoc.DecodeSocStr
-> UserEngine.ExecuteRun
   -> ProcessUserHumans
   -> ProcessMonsters
   -> ProcessMerchants
   -> ProcessNpcs
-> EventMan.Run
```

Player input and player actions are therefore processed before monster AI in
the same legacy frame; merchants, NPCs, and event manager run after monsters.

## ProcessMonsters Algorithm

The Delphi monster scheduler is `TUserEngine.ProcessMonsters`:

- `Source/M2Server/UsrEngn.pas:2233` starts `ProcessMonsters`.
- `Source/M2Server/UsrEngn.pas:2256` records `start := GetTickCount`.
- `Source/M2Server/UsrEngn.pas:2257` records `tcount := GetCurrentTime`.
- `Source/M2Server/UsrEngn.pas:2263` runs one spawn-group check when more than
  200 ms elapsed since `onezentime`.
- `Source/M2Server/UsrEngn.pas:2267` advances `GenCur` over `MonList`.
- `Source/M2Server/UsrEngn.pas:2271` checks `StartTime` against `ZenTime`.
- `Source/M2Server/UsrEngn.pas:2272` counts live monsters with `GetMonCount`.
- `Source/M2Server/UsrEngn.pas:2275` calls `RegenMonsters`.
- `Source/M2Server/UsrEngn.pas:2286` resumes monster traversal from `MonCur`.
- `Source/M2Server/UsrEngn.pas:2289` resumes a group from `MonSubCur`.
- `Source/M2Server/UsrEngn.pas:2295` checks
  `tcount - cret.RunTime > cret.RunNextTick`.
- `Source/M2Server/UsrEngn.pas:2296` writes `cret.RunTime := tcount` before
  calling `cret.Run`.
- `Source/M2Server/UsrEngn.pas:2297` checks
  `GetTickCount > cret.SearchRate + cret.SearchTime`.
- `Source/M2Server/UsrEngn.pas:2298` updates `cret.SearchTime`.
- `Source/M2Server/UsrEngn.pas:2300` calls `cret.SearchViewRange` when
  `RefObjCount > 0` or the creature is hidden.
- `Source/M2Server/UsrEngn.pas:2308` calls the virtual `cret.Run`.
- `Source/M2Server/UsrEngn.pas:2317` frees ghost monsters after
  `5 * 60 * 1000 + cret.GhostTime`.
- `Source/M2Server/UsrEngn.pas:2326` stops the round when
  `GetTickCount - start > MonLimitTime`.
- `Source/M2Server/UsrEngn.pas:2329` stores `MonSubCur := k`.
- `Source/M2Server/UsrEngn.pas:2347` stores `MonCur := i` when budget is
  exhausted, otherwise resets it to `0`.

The behavior can be summarized as:

```text
start = GetTickCount
tcount = GetCurrentTime

if GetTickCount - onezentime > 200:
  onezentime = GetTickCount
  pz = MonList[GenCur]
  advance GenCur
  if pz is active and respawn time elapsed:
    zcount = GetMonCount(pz)
    if pz.Count > zcount:
      goodzen = RegenMonsters(pz, pz.Count - zcount)
    if goodzen:
      pz.StartTime = GetTickCount

for group i from MonCur:
  k = MonSubCur if it is still inside pz.Mons else 0
  MonSubCur = 0
  while k < pz.Mons.Count:
    cret = pz.Mons[k]
    if not cret.BoGhost:
      if tcount - cret.RunTime > cret.RunNextTick:
        cret.RunTime = tcount
        if GetTickCount > cret.SearchRate + cret.SearchTime:
          cret.SearchTime = GetTickCount
          if cret.RefObjCount > 0 or cret.HideMode:
            cret.SearchViewRange()
          else:
            cret.RefObjCount = 0
        cret.Run()
    else if GetTickCount > 5min + cret.GhostTime:
      remove and free cret

    k += 1
    if GetTickCount - start > MonLimitTime:
      MonSubCur = k
      MonCur = i
      stop this frame

if no budget exhaustion:
  MonCur = 0
```

`MonLimitTime` is a configured legacy budget:

- `Source/M2Server/svMain.pas:232` declares `MonLimitTime`.
- `Source/M2Server/svMain.pas:424` sets the default to `30`.
- `Source/M2Server/svMain.pas:470` reads ini key `MonLimit`.

Spawn timing has a player-count acceleration path:

- `Source/M2Server/UsrEngn.pas:2234` defines local `GetZenTime`.
- `Source/M2Server/UsrEngn.pas:2239` only accelerates `ztime < 30 * 60 *
  1000`.
- `Source/M2Server/UsrEngn.pas:2240` derives the acceleration from
  `GetUserCount`, `UserFullCount`, and `ZenFastStep`.
- `Source/M2Server/UsrEngn.pas:2242` caps the acceleration factor at `6`.

## Monster Configuration and Spawn Inputs

Delphi monster definition records are in `TMonsterInfo`:

- `Source/Common/Grobal2.pas:645` defines `TMonsterInfo`.
- `Source/Common/Grobal2.pas:647` stores `Race`.
- `Source/Common/Grobal2.pas:648` stores `RaceImg`.
- `Source/Common/Grobal2.pas:649` stores `Appr`.
- `Source/Common/Grobal2.pas:650` through
  `Source/Common/Grobal2.pas:669` store level, HP, MP, AC, MAC, DC, agility,
  accuracy, walk speed, walk step, walk wait, attack speed, and item list.

Delphi spawn records are in `TZenInfo`:

- `Source/Common/Grobal2.pas:672` defines `TZenInfo`.
- `Source/Common/Grobal2.pas:673` through
  `Source/Common/Grobal2.pas:682` store map, point, monster name, race, area,
  count, respawn time, start time, runtime monster list, and small-spawn rate.

Definition and drop loading:

- `Source/M2Server/LocalDB.pas:169` implements `LoadMonItems`.
- `Source/M2Server/LocalDB.pas:177` loads
  `EnvirDir + MONBAGDIR + monname + '.txt'`.
- `Source/M2Server/LocalDB.pas:221` implements `LoadMonsters`.
- `Source/M2Server/LocalDB.pas:235` reads `NAME`.
- `Source/M2Server/LocalDB.pas:238` reads `Race`.
- `Source/M2Server/LocalDB.pas:239` reads `RaceImg`.
- `Source/M2Server/LocalDB.pas:240` reads `IMGINDEX` into `Appr`.
- `Source/M2Server/LocalDB.pas:241` through
  `Source/M2Server/LocalDB.pas:254` read level, undead, cool eye, exp, HP, MP,
  AC, MAC, DC, DCMAX, MC, SC, agility, accuracy, walk speed, walk step, walk
  wait, and attack speed.
- `Source/M2Server/LocalDB.pas:255` clamps walk speed to at least `200`.
- `Source/M2Server/LocalDB.pas:256` clamps attack speed to at least `200`.
- `Source/M2Server/LocalDB.pas:259` loads the monster's item list.

Spawn-list loading:

- `Source/M2Server/LocalDB.pas:323` implements `LoadZenLists`.
- `Source/M2Server/LocalDB.pas:328` reads `EnvirDir + ZENFILE`.
- `Source/M2Server/LocalDB.pas:338` through
  `Source/M2Server/LocalDB.pas:360` parse map, x, y, quoted monster name,
  area, count, zen minutes, and `SmallZenRate`.
- `Source/M2Server/LocalDB.pas:368` creates a runtime `Mons` list.
- `Source/M2Server/LocalDB.pas:376` appends an empty sentinel group used by
  sysop-created monsters.

Ability application:

- `Source/M2Server/UsrEngn.pas:558` implements `ApplyMonsterAbility`.
- `Source/M2Server/UsrEngn.pas:563` writes `RaceServer`.
- `Source/M2Server/UsrEngn.pas:564` writes `RaceImage`.
- `Source/M2Server/UsrEngn.pas:565` writes `Appearance`.
- `Source/M2Server/UsrEngn.pas:566` through
  `Source/M2Server/UsrEngn.pas:585` copy level, HP, MP, AC, MAC, DC, MC, SC,
  speed point, accuracy point, walk speed, walk step, walk wait, and attack
  speed.

Spawn construction:

- `Source/M2Server/UsrEngn.pas:733` implements `AddCreature`.
- `Source/M2Server/UsrEngn.pas:740` dispatches by `race`.
- `Source/M2Server/UsrEngn.pas:1021` applies monster ability data.
- `Source/M2Server/UsrEngn.pas:1024` through
  `Source/M2Server/UsrEngn.pas:1030` writes map, position, random direction,
  name, and working ability.
- `Source/M2Server/UsrEngn.pas:1033` applies `CoolEye` by setting
  `BoViewFixedHide` with `Random(100) < CoolEye`.
- `Source/M2Server/UsrEngn.pas:1036` calls `MonGetRandomItems` at spawn time.
- `Source/M2Server/UsrEngn.pas:1038` calls `Initialize`.
- `Source/M2Server/UsrEngn.pas:1043` through
  `Source/M2Server/UsrEngn.pas:1059` relocate the creature when the initial
  spawn cell cannot be used.

Sysop/script style creation:

- `Source/M2Server/UsrEngn.pas:1072` implements `AddCreatureSysop`.
- `Source/M2Server/UsrEngn.pas:1077` resolves race with `GetMonRace`.
- `Source/M2Server/UsrEngn.pas:1078` calls `AddCreature`.
- `Source/M2Server/UsrEngn.pas:1082` adds the result to the sentinel monster
  group.

Group respawn:

- `Source/M2Server/UsrEngn.pas:1087` implements `RegenMonsters`.
- `Source/M2Server/UsrEngn.pas:1098` chooses clustered small spawn when
  `Random(100) < SmallZenRate`.
- `Source/M2Server/UsrEngn.pas:1099` through
  `Source/M2Server/UsrEngn.pas:1105` choose a cluster center inside the spawn
  area, then each monster within `-10 + Random(20)` of that center.
- `Source/M2Server/UsrEngn.pas:1114` through
  `Source/M2Server/UsrEngn.pas:1120` otherwise choose each monster directly
  inside `x/y +/- area`.
- `Source/M2Server/UsrEngn.pas:1123` stops the spawn attempt when
  `ZenLimitTime` is exceeded.
- `Source/M2Server/UsrEngn.pas:1137` implements `GetMonCount`.
- `Source/M2Server/UsrEngn.pas:1142` counts only monsters that are not
  `Death` and not `BoGhost`.

## Monster Run Order

Base timing fields are initialized in `TCreature.InitValues`:

- `Source/M2Server/ObjBase.pas:1116` sets
  `RunTime := GetCurrentTime + Random(1500)`.
- `Source/M2Server/ObjBase.pas:1117` sets `RunNextTick := 250`.
- `Source/M2Server/ObjBase.pas:1118` sets
  `SearchRate := 2000 + Random(2000)`.
- `Source/M2Server/ObjBase.pas:1119` sets `SearchTime := GetTickCount`.

`TMonster.Create` overrides normal-monster defaults:

- `Source/M2Server/ObjMon.pas:268` implements `TMonster.Create`.
- `Source/M2Server/ObjMon.pas:272` sets `RunNextTick := 250`.
- `Source/M2Server/ObjMon.pas:273` sets
  `SearchRate := 3000 + Random(2000)`.
- `Source/M2Server/ObjMon.pas:274` sets `SearchTime := GetTickCount`.
- `Source/M2Server/ObjMon.pas:275` sets `RaceServer := RC_MONSTER`.

`TATMonster.Create` changes active-search cadence:

- `Source/M2Server/ObjMon.pas:524` implements `TATMonster.Create`.
- `Source/M2Server/ObjMon.pas:527` sets
  `SearchRate := 1500 + Random(1500)`.

`TMonster.Run` order:

- `Source/M2Server/ObjMon.pas:376` implements `TMonster.Run`.
- `Source/M2Server/ObjMon.pas:380` skips behavior when ghost, dead, hidden,
  stone, or stone-poisoned.
- `Source/M2Server/ObjMon.pas:384` calls `Think` first.
- `Source/M2Server/ObjMon.pas:385` immediately calls `inherited Run` and exits
  when `Think` consumed the action.
- `Source/M2Server/ObjMon.pas:388` clears walk wait mode after
  `WalkWaitTime`.
- `Source/M2Server/ObjMon.pas:392` checks
  `GetCurrentTime - WalkTime > NextWalkTime`.
- `Source/M2Server/ObjMon.pas:393` writes `WalkTime := GetCurrentTime`.
- `Source/M2Server/ObjMon.pas:394` increments `WalkCurStep`.
- `Source/M2Server/ObjMon.pas:396` enters walk-wait mode after `WalkStep`.
- `Source/M2Server/ObjMon.pas:402` calls `AttackTarget` when a target exists.
- `Source/M2Server/ObjMon.pas:404` calls `inherited Run` and exits when
  `AttackTarget` handled this action.
- `Source/M2Server/ObjMon.pas:409` installs mission target coordinates when
  there is no target.
- `Source/M2Server/ObjMon.pas:414` through
  `Source/M2Server/ObjMon.pas:438` handle slave follow and long-distance
  space move back to the master.
- `Source/M2Server/ObjMon.pas:450` exits early for relaxed slaves after
  `inherited Run`.
- `Source/M2Server/ObjMon.pas:457` calls `GotoTargetXY` when `TargetX <> -1`.
- `Source/M2Server/ObjMon.pas:463` calls `Wondering` otherwise.
- `Source/M2Server/ObjMon.pas:467` calls `inherited Run` after monster AI.

`TATMonster.Run` order:

- `Source/M2Server/ObjMon.pas:535` implements `TATMonster.Run`.
- `Source/M2Server/ObjMon.pas:537` skips if dead, run-done, ghost, or
  stone-poisoned.
- `Source/M2Server/ObjMon.pas:538` searches after 8000 ms, or after 1000 ms
  when `TargetCret = nil`.
- `Source/M2Server/ObjMon.pas:539` updates `SearchEnemyTime`.
- `Source/M2Server/ObjMon.pas:540` calls `MonsterNormalAttack`.
- `Source/M2Server/ObjMon.pas:543` calls `inherited Run`, entering
  `TMonster.Run`.

`TCreature.Run` order:

- `Source/M2Server/ObjBase.pas:7552` implements `TCreature.Run`.
- `Source/M2Server/ObjBase.pas:7561` drains local messages with
  `while GetMsg(msg) do RunMsg(msg)`.
- `Source/M2Server/ObjBase.pas:7571` applies `NeverDie`.
- `Source/M2Server/ObjBase.pas:7576` computes elapsed 20 ms slices from
  `GetTickCount`.
- `Source/M2Server/ObjBase.pas:7581` through
  `Source/M2Server/ObjBase.pas:7595` handle HP/MP regeneration.
- `Source/M2Server/ObjBase.pas:7596` checks `WAbil.HP = 0`.
- `Source/M2Server/ObjBase.pas:7608` calls `Die`.
- `Source/M2Server/ObjBase.pas:7614` calls `MakeGhost` after 3 minutes dead.
- `Source/M2Server/ObjBase.pas:7690` through
  `Source/M2Server/ObjBase.pas:7738` clear target, last hitter, exp hitter,
  master/slave state, holy seize, crazy mode, and open-health timers.

Important order note: for normal active monsters, `TATMonster.Run` performs its
pre-search, then `TMonster.Run` performs think/walk/attack/move/wander, and
only then calls `inherited Run` into `TCreature.Run` unless an earlier branch
explicitly exits through `inherited Run`. It is therefore not correct to model
ordinary active monsters as "always process local messages before AI" without
checking the concrete subclass.

Local message queue evidence:

- `Source/M2Server/ObjBase.pas:374` declares `MsgList`.
- `Source/M2Server/ObjBase.pas:1243` implements `SendFastMsg`.
- `Source/M2Server/ObjBase.pas:1262` implements `SendMsg`.
- `Source/M2Server/ObjBase.pas:1311` implements `SendDelayMsg`.
- `Source/M2Server/ObjBase.pas:1399` implements `GetMsg`.
- `Source/M2Server/ObjBase.pas:1415` skips delayed messages whose
  `deliverytime` is still in the future, but continues scanning later
  messages.
- `Source/M2Server/ObjBase.pas:1420` removes and returns the first due
  message.

## Target Search and Visibility

There is no confirmed Delphi function named `SearchTarget` in this source
tree. The observed monster target acquisition path is:

```text
ProcessMonsters due search
-> TCreature.SearchViewRange
-> update VisibleActors
-> TAnimal.MonsterNormalAttack or MonsterDetecterAttack
-> SelectTarget
```

Visibility evidence:

- `Source/M2Server/ObjBase.pas:1690` implements `SearchViewRange`.
- `Source/M2Server/ObjBase.pas:1708` marks existing visible items, events, and
  actors before refreshing.
- `Source/M2Server/ObjBase.pas:1716` through
  `Source/M2Server/ObjBase.pas:1726` scans `CX/CY +/- ViewRange`.
- `Source/M2Server/ObjBase.pas:1731` iterates map cells in x-major, then
  y-major order.
- `Source/M2Server/ObjBase.pas:1736` iterates each cell's `ObjList` from index
  `0` upward.
- `Source/M2Server/ObjBase.pas:1755` ignores ghost, hidden, and supervisor
  creatures.
- `Source/M2Server/ObjBase.pas:1761` through
  `Source/M2Server/ObjBase.pas:1771` filters whether a monster needs to track
  this creature.
- `Source/M2Server/ObjBase.pas:1772` calls `UpdateVisibleGay`.

Target selection evidence:

- `Source/M2Server/ObjBase.pas:8277` implements `MonsterNormalAttack`.
- `Source/M2Server/ObjBase.pas:8283` iterates `VisibleActors` in list order.
- `Source/M2Server/ObjBase.pas:8285` requires not dead, `IsProperTarget`, and
  either not human-hidden or `BoViewFixedHide`.
- `Source/M2Server/ObjBase.pas:8286` ranks by Manhattan distance.
- `Source/M2Server/ObjBase.pas:8293` calls `SelectTarget`.
- `Source/M2Server/ObjBase.pas:8298` implements `MonsterDetecterAttack`.
- `Source/M2Server/ObjBase.pas:8304` omits the `BoHumHideMode` filter, allowing
  detector monsters to consider hidden targets.

Later C++ target selection must preserve both the visibility refresh cadence
and the list traversal order; scanning an unordered object map is not a legacy
equivalent.

## Movement and Attack Chain

Movement to a target:

- `Source/M2Server/ObjBase.pas:8319` implements `SetTargetXY`.
- `Source/M2Server/ObjBase.pas:8325` implements `GotoTargetXY`.
- `Source/M2Server/ObjBase.pas:8338` through
  `Source/M2Server/ObjBase.pas:8357` choose the first desired direction by
  comparing target x/y against current x/y.
- `Source/M2Server/ObjBase.pas:8367` calls `WalkTo(wantdir, FALSE)`.
- `Source/M2Server/ObjBase.pas:8368` chooses `rand := Random(3)`.
- `Source/M2Server/ObjBase.pas:8369` through
  `Source/M2Server/ObjBase.pas:8378` retry up to 7 directions, rotating
  forward or backward depending on `rand`.
- `Source/M2Server/ObjBase.pas:8383` implements `Wondering`.
- `Source/M2Server/ObjBase.pas:8385` only wanders when `Random(20) = 0`.
- `Source/M2Server/ObjBase.pas:8386` turns to `Random(8)` when `Random(4) = 1`;
  otherwise it walks in the current direction.

Walking and broadcasting:

- `Source/M2Server/ObjBase.pas:6228` implements `WalkTo`.
- `Source/M2Server/ObjBase.pas:6240` writes `self.Dir := dir`.
- `Source/M2Server/ObjBase.pas:6242` through
  `Source/M2Server/ObjBase.pas:6250` compute the next cell by direction.
- `Source/M2Server/ObjBase.pas:6253` rejects out-of-map positions.
- `Source/M2Server/ObjBase.pas:6255` checks fire avoidance through
  `CanSafeWalk` when `BoFearFire`.
- `Source/M2Server/ObjBase.pas:6258` prevents a slave from stepping into its
  master's front cell.
- `Source/M2Server/ObjBase.pas:6263` calls
  `PEnvir.MoveToMovingObject(..., allowdup)`.
- `Source/M2Server/ObjBase.pas:6270` calls `Walk(RM_WALK)` after position
  change.
- `Source/M2Server/ObjBase.pas:2107` implements `Walk`.
- `Source/M2Server/ObjBase.pas:2188` sends the reference move message through
  `SendRefMsg`.

Attack chain:

- `Source/M2Server/ObjMon.pas:353` implements `TMonster.AttackTarget`.
- `Source/M2Server/ObjMon.pas:357` checks `TargetInAttackRange`.
- `Source/M2Server/ObjMon.pas:360` checks
  `GetCurrentTime - HitTime > NextHitTime`.
- `Source/M2Server/ObjMon.pas:361` writes `HitTime := GetCurrentTime`.
- `Source/M2Server/ObjMon.pas:362` writes `TargetFocusTime := GetTickCount`.
- `Source/M2Server/ObjMon.pas:363` calls `Attack(TargetCret, targdir)`.
- `Source/M2Server/ObjMon.pas:364` calls `BreakHolySeize`.
- `Source/M2Server/ObjBase.pas:8245` implements `TAnimal.Attack`.
- `Source/M2Server/ObjBase.pas:5550` implements `HitHit`.
- `Source/M2Server/ObjBase.pas:5252` implements `_Attack`.
- `Source/M2Server/ObjBase.pas:3470` implements `StruckDamage`.

Reference broadcasts:

- `Source/M2Server/ObjBase.pas:1497` implements `SendRefMsg`.
- `Source/M2Server/ObjBase.pas:1505` exits when supervisor or hidden.
- `Source/M2Server/ObjBase.pas:1510` refreshes message targets every 500 ms or
  when the cache is empty.
- `Source/M2Server/ObjBase.pas:1514` through
  `Source/M2Server/ObjBase.pas:1518` scans a `12`-cell box around the sender.
- `Source/M2Server/ObjBase.pas:1522` iterates each cell's `ObjList` from the
  end toward the beginning.
- `Source/M2Server/ObjBase.pas:1543` sends directly to user humans.
- `Source/M2Server/ObjBase.pas:1546` through
  `Source/M2Server/ObjBase.pas:1549` sends to non-player creatures only when
  `WantRefMsg` and the message is `RM_STRUCK`, `RM_HEAR`, or `RM_DEATH`.
- `Source/M2Server/ObjBase.pas:1573` through
  `Source/M2Server/ObjBase.pas:1583` reuses cached targets while they remain on
  the same map within `11` cells.

## Death, Drop, Ghost, Respawn

Spawn-time drop roll:

- `Source/M2Server/UsrEngn.pas:664` implements `MonGetRandomItems`.
- `Source/M2Server/UsrEngn.pas:674` matches the runtime monster name to the
  loaded `MonDefList` item list.
- `Source/M2Server/UsrEngn.pas:682` rolls each drop row with
  `pmi.SelPoint >= Random(pmi.MaxPoint)`.
- `Source/M2Server/UsrEngn.pas:683` handles gold rows by increasing
  `mon.Gold`.
- `Source/M2Server/UsrEngn.pas:696` creates a concrete user item from the item
  name.
- `Source/M2Server/UsrEngn.pas:698` randomizes durability to
  `20 + Random(80)` percent.
- `Source/M2Server/UsrEngn.pas:708` applies random item upgrade when
  `Random(10) = 0`.
- `Source/M2Server/UsrEngn.pas:719` adds the created item to the monster's
  `ItemList`.

Death chain:

- `Source/M2Server/ObjBase.pas:7596` checks for zero HP in `TCreature.Run`.
- `Source/M2Server/ObjBase.pas:7608` calls `Die`.
- `Source/M2Server/ObjBase.pas:2583` implements `TCreature.Die`.
- `Source/M2Server/ObjBase.pas:2594` sets `Death := TRUE`.
- `Source/M2Server/ObjBase.pas:2595` sets `DeathTime := GetTickCount`.
- `Source/M2Server/ObjBase.pas:2616` through
  `Source/M2Server/ObjBase.pas:2661` handle monster kill experience and map
  quest kill calls for the exp hitter or its group.
- `Source/M2Server/ObjBase.pas:2746` calls `DropUseItems`.
- `Source/M2Server/ObjBase.pas:2748` calls `ScatterBagItems`.
- `Source/M2Server/ObjBase.pas:2750` calls `ScatterGolds`.
- `Source/M2Server/ObjBase.pas:2806` sends `RM_DEATH` after the reward/drop
  work in the shown code path.

Drop placement:

- `Source/M2Server/ObjBase.pas:2391` implements `ScatterBagItems`.
- `Source/M2Server/ObjBase.pas:2492` implements `ScatterGolds`.
- `Source/M2Server/ObjBase.pas:2518` implements `DropUseItems`.
- `Source/M2Server/ObjBase.pas:6680` implements `DropItemDown`.
- `Source/M2Server/ObjBase.pas:6716` broadcasts item appearance with
  `RM_ITEMSHOW`.
- `Source/M2Server/ObjBase.pas:6740` implements `DropGoldDown`.
- `Source/M2Server/ObjBase.pas:6764` broadcasts gold appearance with
  `RM_ITEMSHOW`.

Corpse and ghost:

- `Source/M2Server/ObjBase.pas:7614` calls `MakeGhost` after
  `GetTickCount - DeathTime > 3 * 60 * 1000`.
- `Source/M2Server/ObjBase.pas:2343` implements `MakeGhost`.
- `Source/M2Server/UsrEngn.pas:2317` frees ghost monsters after
  `GetTickCount > 5 * 60 * 1000 + cret.GhostTime`.

Respawn count:

- `Source/M2Server/UsrEngn.pas:1137` implements `GetMonCount`.
- `Source/M2Server/UsrEngn.pas:1142` counts a monster only if it is not
  `Death` and not `BoGhost`.

This means respawn availability is based on live count, not simply on whether
the corpse object has already been removed from the map.

## Race Matrix

Race constants are defined in `Source/Common/Grobal2.pas:1460` through
`Source/Common/Grobal2.pas:1525`. `AddCreature` dispatches them to concrete
Delphi classes in `Source/M2Server/UsrEngn.pas:733`.

| Race | Value | Delphi class or behavior | Evidence |
| --- | ---: | --- | --- |
| `RC_MONSTER` | 80 | `TMonster` | `UsrEngn.pas:783`, `ObjMon.pas:268` |
| `RC_OMA` | 81 | `TATMonster` | `UsrEngn.pas:787`, `ObjMon.pas:524` |
| `RC_SPITSPIDER` | 82 | `TSpitSpider` | `UsrEngn.pas:796`, `ObjMon.pas:632` |
| `RC_SLOWMONSTER` | 83 | `TSlowATMonster` | `UsrEngn.pas:800` |
| `RC_SCORPION` | 84 | `TScorpion` | `UsrEngn.pas:804` |
| `RC_KILLINGHERB` | 85 | `TStickMonster` | `UsrEngn.pas:808`, `ObjMon2.pas:182` |
| `RC_SKELETON` | 86 | `TATMonster` | `UsrEngn.pas:812` |
| `RC_DUALAXESKELETON` | 87 | `TDualAxeMonster` | `UsrEngn.pas:816`, `ObjAxeMon.pas:83` |
| `RC_HEAVYAXESKELETON` | 88 | `TATMonster` | `UsrEngn.pas:820` |
| `RC_KNIGHTSKELETON` | 89 | `TATMonster` | `UsrEngn.pas:824` |
| `RC_BIGKUDEKI` | 90 | `TGasAttackMonster` | `UsrEngn.pas:828`, `ObjMon.pas:741` |
| `RC_MAGCOWFACEMON` | 91 | `TMagCowMonster` | `UsrEngn.pas:838`, `ObjMon.pas:820` |
| `RC_COWFACEKINGMON` | 92 | `TCowKingMonster` | `UsrEngn.pas:842` |
| `RC_THORNDARK` | 93 | `TThornDarkMonster` | `UsrEngn.pas:847` |
| `RC_LIGHTINGZOMBI` | 94 | `TLightingZombi` | `UsrEngn.pas:852`, `ObjMon.pas:944` |
| `RC_DIGOUTZOMBI` | 95 | `TDigOutZombi` | `UsrEngn.pas:857` |
| `RC_ZILKINZOMBI` | 96 | `TZilKinZombi` | `UsrEngn.pas:863` |
| `RC_COWMON` | 97 | `TCowMonster` | `UsrEngn.pas:833` |
| `RC_WHITESKELETON` | 100 | `TWhiteSkeleton` | `UsrEngn.pas:869` |
| `RC_SCULTUREMON` | 101 | `TScultureMonster` | `UsrEngn.pas:874` |
| `RC_SCULKING` | 102 | `TScultureKingMonster` | `UsrEngn.pas:880` |
| `RC_BEEQUEEN` | 103 | `TBeeQueen` | `UsrEngn.pas:890`, `ObjMon2.pas:313` |
| `RC_ARCHERMON` | 104 | `TArcherMonster` | `UsrEngn.pas:895` |
| `RC_GASMOTH` | 105 | `TGasMothMonster` | `UsrEngn.pas:900` |
| `RC_DUNG` | 106 | `TGasDungMonster` | `UsrEngn.pas:905` |
| `RC_CENTIPEDEKING` | 107 | `TCentipedeKingMonster` | `UsrEngn.pas:910`, `ObjMon2.pas:393` |
| `RC_BLACKPIG` | 108 | `TATMonster`, may set `BoFearFire` | `UsrEngn.pas:791` |
| `RC_CASTLEDOOR` | 110 | `TCastleDoor` | `UsrEngn.pas:983`, `ObjMon2.pas:1001` |
| `RC_WALL` | 111 | `TWallStructure` | `UsrEngn.pas:988`, `ObjMon2.pas:1061` |
| `RC_ARCHERGUARD` | 112 | `TArcherGuard` | `UsrEngn.pas:993`, `ObjMon2.pas:824` |
| `RC_ELFMON` | 113 | `TElfMonster` | `UsrEngn.pas:1003` |
| `RC_ELFWARRIORMON` | 114 | `TElfWarriorMonster` | `UsrEngn.pas:1008` |
| `RC_BIGHEARTMON` | 115 | `TBigHeartMonster` | `UsrEngn.pas:915`, `ObjMon2.pas:491` |
| `RC_SPIDERHOUSEMON` | 116 | `TSpiderHouseMonster` | `UsrEngn.pas:925`, `ObjMon2.pas:600` |
| `RC_EXPLOSIONSPIDER` | 117 | `TExplosionSpider` | `UsrEngn.pas:930`, `ObjMon2.pas:697` |
| `RC_HIGHRISKSPIDER` | 118 | `THighRiskSpider` | `UsrEngn.pas:935` |
| `RC_BIGPOISIONSPIDER` | 119 | `TBigPoisionSpider` | `UsrEngn.pas:940` |
| `RC_SOCCERBALL` | 120 | `TSoccerBall` | `UsrEngn.pas:1013`, `ObjMon2.pas:1147` |
| `RC_BAMTREE` | 121 | `TBamTreeMonster` | `UsrEngn.pas:920` |
| `RC_SCULKING_2` | 122 | `TScultureKingMonster`, follower disabled | `UsrEngn.pas:884` |
| `RC_BLACKSNAKEKING` | 123 | `TDoubleCriticalMonster` | `UsrEngn.pas:945` |
| `RC_NOBLEPIGKING` | 124 | `TATMonster` | `UsrEngn.pas:950` |
| `RC_FEATHERKINGOFKING` | 125 | `TDoubleCriticalMonster` | `UsrEngn.pas:955` |
| `RC_SKELETONKING` | 126 | `TSkeletonKingMonster` | `UsrEngn.pas:961`, `ObjMon.pas:1774` |
| `RC_TOXICGHOST` | 127 | `TGasAttackMonster` | `UsrEngn.pas:965` |
| `RC_SKELETONSOLDIER` | 128 | `TSkeletonSoldier` | `UsrEngn.pas:969`, `ObjMon.pas:1678` |
| `RC_BANYAGUARD` | 129 | `TBanyaGuardMonster` | `UsrEngn.pas:974`, `ObjMon.pas:1859` |
| `RC_DEADCOWKING` | 130 | `TDeadCowKingMonster` | `UsrEngn.pas:978`, `ObjMon.pas:1978` |

Rows that map to a base class such as `TATMonster` still require separate
behavior confirmation when the source also mutates flags such as `BoFearFire`,
summoning, hidden state, or special attack timings.

## Current C++ Anchors

These are anchors for later comparison only; PR-1 does not change or validate
their behavior.

Frame and stage ordering:

- `ModernServer/src/world/legacy_frame_driver.hpp:16` defines
  `LegacyFrameStage`.
- `ModernServer/src/world/legacy_frame_driver.cpp:38` maps stage names.
- `ModernServer/src/world/legacy_frame_driver.cpp:79` implements
  `LegacyFrameDriver::run_frame`.
- `ModernServer/src/world/legacy_frame_driver.cpp:115` runs `RunSocketRun`.
- `ModernServer/src/world/legacy_frame_driver.cpp:121` runs `DecodeIdSocket`.
- `ModernServer/src/world/legacy_frame_driver.cpp:139` runs
  `UserEngineExecuteRun`.
- `ModernServer/src/world/legacy_frame_driver.cpp:145` runs
  `EventManagerRun`.
- `ModernServer/src/world/legacy_frame_driver.cpp:151` runs
  `ServerMessageRun`.

Runtime and monster scheduler:

- `ModernServer/src/world/logic_runtime.cpp:2084` implements
  `LogicRuntime::tick()`.
- `ModernServer/src/world/logic_runtime.cpp:2094` implements
  `LogicRuntime::tick(now_ms, context)`.
- `ModernServer/src/world/logic_runtime.cpp:2102` calls `process_monsters`.
- `ModernServer/src/world/logic_runtime.cpp:2775` implements
  `process_monsters`.
- `ModernServer/src/world/logic_runtime.cpp:2810` calls
  `MapActor::legacy_process_monster`.

Current C++ monster object and AI anchors:

- `ModernServer/src/world/map_actor.cpp:1609` implements
  `MapActor::legacy_process_monster`.
- `ModernServer/src/world/map_actor.cpp:1670` calls `handle_monster_ai`.
- `ModernServer/src/world/map_actor_monster.hpp:903` implements
  `legacy_monster_normal_attack`.
- `ModernServer/src/world/map_actor_monster.hpp:984` computes attack range from
  the current C++ AI profile.
- `ModernServer/src/world/map_actor_monster.hpp:1034` handles stationary
  wandering behavior.
- `ModernServer/src/world/map_actor_monster.hpp:1786` implements
  `handle_monster_ai`.

Current C++ config model anchors:

- `ModernServer/src/config/models.hpp:166` defines `SpawnConfig`.
- `ModernServer/src/config/models.hpp:188` defines `MonsterAiProfile`.
- `ModernServer/src/config/models.hpp:197` defines `MonsterDefConfig`.
- `ModernServer/src/config/models.hpp:224` defines `MonsterDropConfig`.
- `ModernServer/src/config/models.hpp:432` stores `spawns`.
- `ModernServer/src/config/models.hpp:433` stores `monsters`.
- `ModernServer/src/config/models.hpp:434` stores `monster_drops`.

Existing C++ tests that later monster PRs should preserve or extend:

- `ModernServer/tests/legacy_monster_cursor_smoke.cpp`
- `ModernServer/tests/monster_legacy_tick_ai_smoke.cpp`
- `ModernServer/tests/monster_race_ai_smoke.cpp`
- `ModernServer/tests/monster_special_race_smoke.cpp`
- `ModernServer/tests/legacy_monster_import_defs_spawns_drops_smoke.cpp`
- `ModernServer/tests/monster_death_corpse_ghost_smoke.cpp`

## Confirmed Unknowns

`SearchTarget`:

- No exact Delphi function named `SearchTarget` was confirmed in the inspected
  source. The observed equivalent is `SearchViewRange` plus
  `MonsterNormalAttack` or `MonsterDetecterAttack`.
- Needs source confirmation before introducing a C++ API with that name.

`MonsterType`:

- No exact Delphi monster definition field named `MonsterType` was confirmed in
  the inspected `TMonsterInfo`.
- The inspected old server uses `Race/RaceServer` for behavior and
  `RaceImg/Appr` for presentation.
- Needs source confirmation if another data source or later fork uses
  `MonsterType`.

`GetCurrentTime`:

- `ProcessMonsters`, `TMonster.AttackTarget`, and `TMonster.Run` mix
  `GetCurrentTime` and `GetTickCount`.
- The inspected project evidence did not establish a local custom definition
  of `GetCurrentTime`.
- Needs source confirmation before replacing Delphi time checks with fixed
  C++ tick arithmetic.

`SendFastMsg.deliverytime`:

- `SendMsg` and `SendDelayMsg` have explicit message enqueue paths, while
  `GetMsg` checks `deliverytime`.
- `SendFastMsg` must be audited before assuming every queued message has the
  same delay semantics.
- Needs source confirmation before implementing a unified local-message queue.

## PR-2+ High-Risk Gates

Later implementation PRs must address these before claiming monster parity:

- Target search order: preserve `SearchViewRange`, `VisibleActors`, and list
  traversal order.
- Local message order: preserve subclass-specific `Run` versus
  `TCreature.Run` ordering.
- Special Race coverage: migrate by Delphi Race class, not by a generic modern
  AI profile.
- Script and GM spawns: ensure `MONGEN` and sysop spawns use the same
  definition, drop roll, and sentinel group semantics as `AddCreatureSysop`.
- Death ordering: preserve experience, map quest, drop, and `RM_DEATH`
  ordering.
- Visibility and broadcast ordering: do not let unordered C++ containers decide
  target or packet order when Delphi list/cell order is observable.
