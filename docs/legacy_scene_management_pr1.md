# Legacy Scene Management Review

This document is the PR-1 migration artifact for the Delphi scene-management
module. It records behavior that must be preserved before implementation PRs
change the C++ client. The goal is strict legacy compatibility, not a modern
scene framework.

## Reviewed Delphi Sources

- `Source/Client/ClMain.pas`
  - `FormCreate` creates `DScreen`, `IntroScene`, `LoginScene`,
    `SelectChrScene`, `PlayScene`, `LoginNoticeScene`, `Map`, actor/effect
    lists, and installs `Application.OnIdle`.
  - `AppOnIdle` calls `Timer1Timer` before any input or drawing stage.
  - `Timer1Timer` appends `SocStr` into `BufferStr`, extracts `#...!`
    packets in order, and calls `DecodeMessagePacket` for each packet.
  - `DecodeMessagePacket` handles login/lobby messages while `Myself = nil`,
    then game messages after the early login case.
- `Source/Client/DrawScrn.pas`
  - `TDrawScreen.ChangeScene` performs an immediate scene switch:
    `CurrentScene.CloseScene`, pointer assignment, `CurrentScene.OpenScene`.
  - `DrawScreen` clears the back buffer, calls `CurrentScene.PlayScene`, then
    draws play overlays such as health bars, focused names, chat bubbles, and
    area-state icons.
  - `DrawScreenTop` draws system messages after UI direct paint.
  - `DrawHint` draws the cursor hint after top messages.
- `Source/Client/PlayScn.pas`
  - `TPlayScene.OpenScene` clears `WProgUse` cache and shows the bottom HUD.
  - `TPlayScene.CloseScene` silences sound, hides chat edit, and hides the
    bottom HUD.
  - `TPlayScene.Run` advances the 100 ms movement clock, 50 ms animation
    clock, actor message queues, actor movement/run state, effect lists, fly
    objects, client events, dropped item cleanup, and distant event cleanup.
  - `TPlayScene.PlayScene` updates the visible map rectangle, calls
    `Map.UpdateMapPos`, then draws the play scene.
  - `TPlayScene.SendMsg` owns `SM_NEWMAP`, `SM_CHANGEMAP`, `SM_LOGON`,
    `SM_HIDE`, and action-message dispatch to actors.
- `Source/Client/IntroScn.pas`
  - Defines `TSceneType`: `stIntro`, `stLogin`, `stSelectCountry`,
    `stSelectChr`, `stNewChr`, `stLoading`, `stLoginNotice`, `stPlayGame`.

## Reviewed C++ Sources

- `ModernClient/src/app/client_app.cpp`
  - Main loop pumps Win32 messages, polls protocol, handles protocol events,
    flushes pending scene changes, runs timers, runs legacy frame stages, and
    presents.
  - Scene changes are requested through `request_scene_change` and flushed by
    `flush_scene_change_if_pending`.
  - Protocol messages are drained in `handle_protocol_events`.
- `ModernClient/src/app/legacy_frame_scheduler.hpp`
  - Runs `timer1_network_drain`, input phases, `dwin_process`, then either
    render phases or `scene_run`.
  - This matches the key Delphi distinction: render frames do not run
    `PlayScene.Run`; non-render or non-draw frames do.
- `ModernClient/src/scene/scenes.hpp`
  - Current scene ids: `boot`, `login`, `server_select`, `character_select`,
    `login_notice`, `loading`, `world`.
- `ModernClient/src/scene/scenes.cpp`
  - Contains `BootScene`, `LoginScene`, `ServerSelectScene`,
    `CharacterSelectScene`, `LoadingScene`, `LoginNoticeScene`, `WorldScene`.
  - `WorldScene` handles HUD, map sync, play input, animation, audio, and
    render ordering.
- `ModernClient/src/protocol/delphi_protocol_map.hpp`
  - Tracks Delphi send/receive mapping to `client_v1`.
- `ModernClient/src/animation/legacy_animation.cpp`
  - Implements legacy 100 ms movement and 50 ms animation clock semantics.

## Confirmed Legacy Ordering

Normal draw branch:

```text
Application.OnIdle
-> Timer1Timer
-> BufferStr packet extraction
-> DecodeMessagePacket for each #...! packet
-> ProcessKeyMessages
-> ProcessActionMessages
-> DWinMan.Process
-> DScreen.DrawScreen
   -> CurrentScene.PlayScene
   -> Play overlays
-> DWinMan.DirectPaint
-> DScreen.DrawScreenTop
-> DScreen.DrawHint
-> moving item cursor image
-> fade/version/login text overlays
-> DXDraw1.Flip
```

Non-draw branch:

```text
Application.OnIdle
-> Timer1Timer
-> ProcessKeyMessages
-> ProcessActionMessages
-> DWinMan.Process
-> PlayScene.Run
```

`PlayScene.Run()` is not called directly in the normal Delphi draw branch.
It is called when drawing is not possible or the frame interval has not elapsed.

## Scene State Machine

```text
startup
-> login
-> server_select
-> character_select
-> create_character_dialog
-> login_notice/loading
-> play
-> map_change
-> play
-> disconnect/reconnect
-> character_select or login
-> exit
```

### Login

- Entry: show login UI, keep account/password fields, play intro music.
- User exit: close confirmation or app close path.
- User login: send login request, start wait timer, prevent duplicate request.
- Network success: hide login box, show server-select dialog.
- Network failure: show failure dialog, remain in login, restore retry state.
- Disconnect: show disconnect message, keep credentials.

### Server Select

- Entry: show server-select dialog over login background.
- User select: send server-select request, prevent repeated selection.
- Network success: connect character gateway, then query character list.
- Network failure: show failure message, remain in server select.

### Character Select

- Entry: clear old character slots before applying server list.
- Network character list: add up to two characters, set selected/frozen state.
- User start: send selected-character request, prevent repeated start.
- User delete: confirm, send delete request, refresh character list on success.
- User create: open create-character UI, send create request, refresh list on
  success.

### Login Notice / Loading

- Delphi `ClientGetStartPlay` changes to `stLoginNotice` in the one-click path,
  clears bag/chat, waits, then sends run login.
- C++ `LoginNoticeScene` sends `LoginNoticeOk` and waits in `LoadingScene`.
- Exact notice timing outside one-click mode is pending source confirmation.

### Play

- `SM_NEWMAP` / map snapshot loads map synchronously before play rendering.
- `SM_LOGON` creates or binds `Myself`, immediately switches to `stPlayGame`,
  then sends `CM_QUERYBAGITEMS`.
- Exit closes HUD/chat, silences sound, and must clear play-only state.

## Protocol-Driven Scene Transitions

| Delphi | C++ `client_v1` | Scene effect | Required timing |
| --- | --- | --- | --- |
| `SendLogin` | `LoginRequest` | login wait | On button click |
| `SM_PASSOK_SELECTSERVER` | `LoginResult` + `ServerList` | login -> server_select | During packet handling |
| `CM_SELECTSERVER` | `SelectServerRequest` | server wait | On server click |
| `SM_SELECTSERVER_OK` | `SelectServerResult` | connect character gateway | During packet handling |
| `CM_QUERYCHR` | `CharacterListRequest` | request lobby characters | On character gateway connect |
| `SM_QUERYCHR` | `CharacterList` | character_select refresh | Clear then apply |
| `CM_NEWCHR` | `CreateCharacterRequest` | create wait | On create confirm |
| `SM_NEWCHR_SUCCESS` | `CreateCharacterResult` | refresh characters | Send/query list |
| `CM_DELCHR` | `DeleteCharacterRequest` | delete wait | After confirm |
| `SM_DELCHR_SUCCESS` | `DeleteCharacterResult` | refresh characters | Send/query list |
| `CM_SELCHR` | `SelectCharacterRequest` | enter wait | On start click |
| `SM_STARTPLAY` | `SelectCharacterResult` | connect game gateway | During packet handling |
| `CM_LOGINNOTICEOK` | `LoginNoticeOk` | wait for world | On notice OK |
| `SM_NEWMAP` | `WorldSnapshot.map_id` or map-change message | load map | Before play draw |
| `SM_LOGON` | `EnterWorldResult` + snapshot self actor | switch play | Immediate during message |
| `SM_RECONNECT` | planned reconnect result | reconnect path | Clear buffers first |
| `SM_OUTOFCONNECTION` | `DisconnectReason` / disconnect event | leave play | Clear play state |

## C++ Gaps and Compatibility Risks

- Scene switching is delayed by `request_scene_change` and currently flushed
  after protocol handling phases. Delphi `ChangeScene` is immediate inside
  packet handling. A `client_v1` drain containing multiple semantic messages
  can therefore apply later messages before the equivalent scene has opened.
- `WorldScene::update` calls the split legacy phases internally. The main loop
  uses split phases directly, but any caller of `SceneManager::update` can
  double-dispatch world input/run stages.
- `LoadingScene` is an empty placeholder. It does not yet model Delphi wait
  screens, blocking waits, or `WaitAndPass` timing.
- Reconnect and server-change behavior is incomplete compared with
  `ClientGetReconnect`, which clears buffers, changes connection state, clears
  game variables, and re-sends run login.
- Create-character UI state retention on failure is pending source
  confirmation.
- `MapMoving` / `MapMovingWait` stops packet drain for map transitions in
  Delphi. C++ needs an equivalent gate before map-change implementation is
  considered compatible.
- Play render row ordering still needs a golden trace for dropped items,
  actors, fly effects, and large object rows.
- UI hit-test capture is a C++-only split stage. It must not fire click
  callbacks before the `DWinMan.Process` equivalent.

## Expected C++ Integration Rules

- Treat `protocol_.poll` plus `handle_protocol_events` as the Delphi
  `Timer1Timer` compatibility boundary.
- Network handling remains before all input processing.
- Flush scene changes after each scene-changing protocol message, not merely at
  the end of a full receive drain.
- Render frames call `render_scene`, `paint_ui`, modal rendering, and present;
  they do not call `scene_run`.
- Non-render and non-draw frames call `scene_run`.
- UI callbacks fire in `dwin_process`; `capture_ui_input` only determines
  consumption/focus/drag state.
- Scene switch clears mouse capture, drag state, focus, action key, and
  world-intent state for the old scene.
- Return-to-login and disconnect paths clear `WorldViewState`, HUD state,
  pending item/action state, map cache, animation/effect state, and sound.

## Testing Plan

- Frame order smoke: normal render branch omits `scene_run`; non-render and
  non-draw branches call `scene_run`.
- Scene switch smoke: scene-changing protocol event becomes visible before the
  next semantic protocol event is applied.
- Login flow smoke: login failure remains in login; success reaches server
  select; server select reaches character list.
- Character flow smoke: create/delete success refreshes character list; start
  enters loading/notice/world once.
- Disconnect smoke: play disconnect clears world, HUD, focus, modal, and
  pending actions.
- Render trace smoke: assert scene, UI, top message, hint, moving item, present
  order.
- Play row trace smoke: assert map tile, small object, ground effect,
  row-integrated large object/item/actor/fly effect, overlay order.
- Fuzz: duplicate clicks, disconnect during scene change, old world messages
  after exit, mouse down in old scene and release in new scene.

## Pending Source Confirmation

- Exact `TLoginScene`, `TSelectChrScene`, and create-character dialog callback
  ordering in `IntroScn.pas`.
- Full `DWinMan.Process` modal/focus/mouse-capture priority.
- Exact login notice flow for non-one-click mode.
- Create-character input retention after `SM_NEWCHR_FAIL`.
- Exact `MapMoving` and `MapMovingWait` timing around `SM_CHANGEMAP`.
- Pixel-level blend and row order inside `TPlayScene.DrawTileMap` and
  `TPlayScene.PlayScene`.
- Resource release behavior for all WIL caches on scene exit versus map change.
