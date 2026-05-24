# Legacy Event System Migration Analysis

This note is the corrected EventMan boundary document for the ModernMir2 server
migration. It intentionally narrows the scope to Delphi `Event.pas` and the
frame slots that call it. Broader gameplay flows such as NPC scripts, MapQuest,
monster death, castle wars, notices, player lifecycle, PK decay, buffs, poison,
and ground item cleanup are business-module compatibility work, not EventMan
types.

## 1. Overall Conclusion

The Delphi event manager is a map-object lifecycle manager. It is not a generic
server event bus.

The C++ `LegacyEventManager` is close to Delphi for active, visible map events
with positive `continue_ms`, but it should not be treated as fully proven until
characterization tests lock the boundary conditions, deletion traversal, closed
cleanup, and fire-burn timing.

Recommended adoption of the previous report:

- Keep the core observation that the frame has a dedicated EventMan slot after
  UserEngine.
- Keep the observation that event records run on a 500 ms default tick.
- Correct the scope: EventMan only owns Delphi `Event.pas` native map events.
- Correct the risk model: most missing gameplay flows are module-order audits,
  not EventMan migration gaps.
- Do not implement a large `LegacyEventScheduler`, `ExtendedLegacyEventRecord`,
  `ScriptEventBridge`, `MonsterEventBridge`, `PlayerEventBridge`, persistence
  adapter, protocol adapter, or unified EventBus in the first phase.

## 2. Delphi Event.pas Boundary

Primary source: `Source/Mir200/Event.pas`.

Native Event.pas classes:

- `TEvent`: visible or invisible map event object with `PEnvir`, `X`, `Y`,
  `EventType`, `EventParam`, `OpenStartTime`, `ContinueTime`, `CloseTime`,
  `Closed`, `Damage`, `OwnCret`, `runstart`, and `runtick`.
- `TStoneMineEvent`: mine marker stored in the map environment. It sets
  `Active := FALSE`, so it is not a normal auto-ticked EventMan entry.
- `TPileStones`: visible pile-stone event with Event.pas-managed lifetime.
- `THolyCurtainEvent`: visible holy-curtain map event with Event.pas-managed
  lifetime.
- `TFireBurnEvent`: visible fire-burn map event with Event.pas-managed lifetime
  plus fire damage ticks.
- `TEventManager`: owns `EventList` and `ClosedList`.

Native event constants and call sites also include base `TEvent` records for:

- `ET_DIGOUTZOMBI`
- `ET_SCULPEICE`

These should be represented as Event.pas-native map events if the C++ type set
is expanded. They should not be replaced by broad business-event kinds.

## 3. Delphi Time Semantics

Event.pas uses delta comparisons:

```text
GetTickCount - runstart > runtick
GetTickCount - OpenStartTime > ContinueTime
GetTickCount - CloseTime > 5 * 60 * 1000
GetTickCount - ticktime > 3000
```

Important details:

- The comparison is `>`, not `>=`.
- `runstart := GetTickCount` is assigned before `event.Run`.
- `TEvent.Run` only closes the event when the open duration has expired.
- `TFireBurnEvent.Run` performs the fire-damage check, then calls inherited
  `TEvent.Run`.
- Delphi's unsigned tick subtraction is naturally suited to DWORD wraparound.
- C++ `now_ms > start + interval` should not be documented as exactly
  equivalent to the Delphi delta form. A future C++ alignment PR should prefer a
  helper equivalent to `now_ms - start > interval`.

Fire-burn nuance:

- Delphi does not explicitly initialize `ticktime` in `TFireBurnEvent.Create`.
  In normal server uptime this means the first eligible 500 ms run can satisfy
  `GetTickCount - ticktime > 3000` and apply damage immediately.
- The 3000 ms rule applies to subsequent fire ticks.

## 4. EventManager Traversal And Deletion Semantics

`TEventManager.Run` uses manual index traversal:

```text
i := 0
while True do
  if i >= EventList.Count then break
  event := EventList[i]
  ...
  if event.Closed then
    ClosedList.Add(event)
    EventList.Delete(i)
  else
    Inc(i)
```

Expected behavior:

- Active events are checked in insertion order.
- Deleting `EventList[i]` does not increment `i`; the next shifted event is
  checked in the same frame.
- Closed events are appended to `ClosedList`.
- `ClosedList` cleanup deletes and frees at most one expired closed event per
  `Run`, because the loop breaks after `ClosedList.Delete(i)`.

These semantics must be locked by characterization tests before further C++
alignment work.

## 5. Main Frame Order

Primary source: `Source/Mir200/svMain.pas`, `RunTimerTimer`.

The relevant order is:

```text
RunSocket.Run
FrmIDSoc.DecodeSocStr
UserEngine.ExecuteRun
EventMan.Run
FrmSrvMsg.Run / FrmMsgClient.Run
```

C++ should preserve the stage order:

```text
RunSocketRun
DecodeIdSocket
UserEngineExecuteRun
EventManagerRun
ServerMessageRun
```

The stage order alone does not prove message-order equivalence. Delphi code may
send messages synchronously inside business logic, while C++ often accumulates
`RuntimeDispatch` and flushes later. Message-order compatibility should be
tested per affected gameplay flow.

## 6. UserEngine Boundary

Primary source: `Source/Mir200/UsrEngn.pas`, `ExecuteRun`.

Delphi `UserEngine.ExecuteRun` order:

```text
ProcessUserHumans
ProcessMonsters
ProcessMerchants
ProcessNpcs
1s tasks
500ms door tasks
10min tasks
10s tasks
```

The periodic tasks inside UserEngine are not EventMan events.

Examples:

- 1s: `ProcessMissions`, `CheckServerWaitTimeOut`, `CheckHolySeizeValid`
- 500ms: `CheckOpenDoors`
- 10min: `NoticeMan.RefreshNoticeList`, main out message, `UserCastle.SaveAll`
- 10s: user count, guild-war timeout, `UserCastle.Run`, shut-up cleanup

C++ should keep these tasks in the UserEngine/LogicRuntime stage. Do not move
them into `LegacyEventManager`.

## 7. Correct Event-System Boundary

### Event.pas / EventMan native events

Belongs in `LegacyEventManager` or its direct map-object support:

- `TEvent`
- `TPileStones`
- `THolyCurtainEvent`
- `TFireBurnEvent`
- base `TEvent` values such as `ET_DIGOUTZOMBI` and `ET_SCULPEICE`
- stone-mine map-environment support, but not as an active auto-ticked
  EventMan entry

### UserEngine periodic tasks

Belongs in `LogicRuntime` / UserEngine-equivalent processing:

- mission checks
- server timeout checks
- holy-seize validity checks
- door checks
- notice refresh
- castle save
- user count
- guild-war timeout
- castle run
- shut-up cleanup

### Business-module event chains

Belongs in the original business modules, with order tests:

- MapQuest: map environment / script trigger flow
- NPC scripts: NPC and script runtime
- monster death, experience, tasks, drops, corpse/ghost: monster/death flow
- castle and guild war: CastleManager / GuildManager
- notices and server messages: NoticeMan / ServerMessage stage
- player login, logout, map change, death cleanup: player lifecycle
- PK decay, buffs, poison: actor run/status systems
- ground item cleanup: map environment / ground item lifecycle

These are not `LegacyEventManager` event kinds.

## 8. C++ Implementation Audit Focus

Primary C++ files:

- `ModernServer/src/world/game_object.hpp`
- `ModernServer/src/world/legacy_event_manager.hpp`
- `ModernServer/src/world/legacy_event_manager.cpp`
- `ModernServer/src/world/legacy_frame_driver.hpp`
- `ModernServer/src/world/legacy_frame_driver.cpp`
- `ModernServer/src/world/logic_runtime.hpp`
- `ModernServer/src/world/logic_runtime.cpp`
- `ModernServer/src/services/world_service.cpp`

Key checks before implementation PRs:

- `LegacyEventRecord` should cover Event.pas-native fields that matter for
  behavior, including event type and event parameter where needed.
- `run_tick_ms = 500` should remain the default.
- Event close timing should use Delphi `>` boundary semantics.
- Closed-list cleanup should delete at most one expired closed event per run.
- Fire burn should preserve first eligible tick, 3000 ms subsequent ticks, and
  close ordering.
- Holy curtain grouping in C++ should be documented as integration behavior, not
  a native `TEventManager` feature.
- Runtime dispatch order should be tested where compatibility depends on output
  sequence.

## 9. Revised Risk Levels

| Level | Risk |
|---|---|
| P0 | Event.pas lifecycle mismatch, `>` boundary mismatch, deletion traversal mismatch, fire-burn tick/close mismatch, missing Event.pas-native type coverage, UserEngine stage order regression |
| P1 | MapQuest/NPC sync order, monster death chain order, Castle/Guild/NoticeMan timer placement, player lifecycle cleanup order, dispatch/message order |
| P2 | Ground item cleanup cadence, periodic save cadence, daily activities, delayed script tasks, event persistence if Delphi source proves it exists |
| P3 | Audit logger, idempotency keys, unified EventBus, protocol adapter generalization, high-load scheduling optimization |

Overestimated in the previous plan:

- generic scheduler as a P0 requirement
- EventManager persistence
- business event type expansion
- bridge classes for every module
- EventBus unification

Underestimated in the previous plan:

- Event.pas source-characterization tests
- stone-mine inactive behavior
- base `TEvent` values beyond fire/holy/pile
- delta time comparison
- dispatch/message order differences
- monster death and MapQuest ordering

## 10. PR Plan

### PR-1: Delphi source confirmation and EventMan boundary rewrite

Goal:

- Correct this document.
- Establish Event.pas-native scope.
- Move business-module flows out of EventMan scope.

Do not:

- Change C++ code.
- Add tests.
- Add scheduler, bridge, persistence, or EventBus abstractions.

Acceptance:

- The document no longer states that missing business-event types are
  EventManager gaps.
- The document explicitly separates Event.pas events, UserEngine periodic tasks,
  and business-module event chains.
- PR-2 characterization tests are listed.

### PR-2: EventMan characterization tests

Goal:

- Lock Delphi EventMan lifecycle behavior in C++ tests before production
  alignment changes.

Test cases:

- 500 ms tick boundary:
  - `open + 499` does not run
  - `open + 500` does not run
  - `open + 501` runs
- continue close boundary:
  - `open + continue` does not close
  - `open + continue + 1` closes
- fire burn:
  - first eligible run emits `fire_tick`
  - `last_damage + 3000` does not tick
  - `last_damage + 3001` ticks
  - closed fire event does not tick again
  - same-tick action order is stable
- EventList deletion:
  - deleting the current event does not skip the shifted next event
  - insertion order is preserved
- ClosedList cleanup:
  - multiple expired closed events clean up one per run
- map object state:
  - active event is discoverable
  - closed event is removed from active lookup
- frame order:
  - `UserEngineExecuteRun` precedes `EventManagerRun`
  - `EventManagerRun` precedes `ServerMessageRun`

Do not:

- Add business-event enums.
- Add `LegacyEventScheduler`.
- Add production behavior fixes unless needed only to expose test state.

### PR-3 and later

Only after PR-2:

- Apply minimal `LegacyEventManager` fixes required by characterization failures.
- Add Event.pas-native missing types if needed.
- Align UserEngine timers in `LogicRuntime`.
- Align MapQuest/NPC/monster death/Castle/Guild/NoticeMan in their own modules.

## 11. Verification Commands

PR-1:

```powershell
git diff --check
```

PR-2:

```powershell
cmake -S F:\mir2-event-pr2\ModernServer -B F:\mir2-event-pr2\ModernServer\build -G Ninja -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe
cmake --build F:\mir2-event-pr2\ModernServer\build --parallel 8
ctest --test-dir F:\mir2-event-pr2\ModernServer\build -R "legacy_event_manager|legacy_frame" --output-on-failure
```
