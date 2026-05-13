# PR-1 Legacy Map and Tile Semantics Audit

This document records the Delphi source audit for migrating the Mir2 map and tile system into the C++ client/server codebase without changing legacy behavior.

Reviewed source:

- `Source/Client/MapUnit.pas`
- `Source/Client/PlayScn.pas`
- `Source/Client/DrawScrn.pas`
- `Source/Client/WIL/Path.pas`
- `Source/M2Server/Envir.pas`
- `Source/M2Server/ObjBase.pas`
- `Source/Common/Grobal2.pas`
- `shared/legacy/map_document.hpp`
- `ModernClient/src/assets/asset_manager.cpp`
- `ModernClient/src/scene/scenes.cpp`
- `ModernClient/src/shared/legacy/map_render_math.hpp`
- `ModernServer/src/world/legacy_map_environment.cpp`
- `ModernServer/src/world/map_actor_movement.hpp`
- `ModernServer/src/world/map_actor_visibility.hpp`

## 1. Map File Format

Delphi client `MapUnit.pas` defines normal maps as a packed 52-byte header followed by 12-byte cells. Map cell data is stored column-major in the file: all `y` cells for `x=0`, then all `y` cells for `x=1`, and so on.

Normal client header:

| Field | Offset | Size | Meaning | Delphi read | C++ target | Risk |
| --- | ---: | ---: | --- | --- | --- | --- |
| `Width` | 0 | 2 | Map width in cells | `FileRead(header)` | `uint16_t width` | Must be little-endian |
| `Height` | 2 | 2 | Map height in cells | `FileRead(header)` | `uint16_t height` | Must be little-endian |
| `Title` | 4 | 21 | Delphi `string[20]` | Not used for render | Preserve/ignore | Do not parse as C string |
| `UpdateDate` | 25 | 8 | `TDateTime` | Not used for render | Preserve/ignore | Client header is packed |
| `Reserved` | 33 | 19 | Reserved | Not used | Preserve/ignore | None |

AntiHack client header is used by `LABY01`, `LABY02`, `LABY03`, `LABY04`, and `SNAKE`.

| Field | Offset | Size | Meaning | Delphi read | C++ target | Risk |
| --- | ---: | ---: | --- | --- | --- | --- |
| `Title` | 0 | 31 | Delphi `string[30]` | Read before dimensions | Preserve/ignore | Do not treat as normal header |
| `Width` | 31 | 2 | XOR encoded width | `Width xor CheckKey` | `uint16_t width` | Server shared decoder currently lacks this |
| `CheckKey` | 33 | 2 | XOR key | Used for width/height/images | `uint16_t check_key` | Must also decode image ids |
| `Height` | 35 | 2 | XOR encoded height | `Height xor CheckKey` | `uint16_t height` | Same as above |
| `UpdateDate` | 37 | 8 | `TDateTime` | Not used | Preserve/ignore | None |
| `Reserved` | 45 | 19 | Reserved | Not used | Preserve/ignore | Header size is 64 |

Cell layout:

| Field | Cell offset | Size | Meaning | Delphi behavior | C++ target | Risk |
| --- | ---: | ---: | --- | --- | --- | --- |
| `BkImg` | 0 | 2 | Background tile id, bit 15 static block | Draw with `WTiles`, mask `$7FFF` | `uint16_t bk_img` | `0x8000` is not part of image id |
| `MidImg` | 2 | 2 | Small ground tile id | Draw with `WSmTiles` | `uint16_t mid_img` | 1-based image id |
| `FrImg` | 4 | 2 | Object/front id, bit 15 static block | Draw with `Objects*`, mask `$7FFF` | `uint16_t fr_img` | Shared with collision |
| `DoorIndex` | 6 | 1 | Bit 7 marks door, low 7 bits are door group | Door lookup/open/close | `uint8_t door_index` | Door group is local clustered state |
| `DoorOffset` | 7 | 1 | Bit 7 open flag, low 7 bits image offset | Adds frame offset when open | `uint8_t door_offset` | Client visual and server state must sync |
| `AniFrame` | 8 | 1 | Bit 7 blend flag, low 7 bits frame count | Drives object animation | `uint8_t ani_frame` | Blend is not ordinary alpha |
| `AniTick` | 9 | 1 | Animation delay factor | Used in `MainAniCount` formula | `uint8_t ani_tick` | Global animation counter |
| `Area` | 10 | 1 | Object archive selector | `Objects.wil` through `Objects7.wil` | `uint8_t area` | Fallback is `Objects.wil` |
| `Light` | 11 | 1 | Tile light value | Used by light pipeline | `uint8_t light` | Full light behavior still needs audit |

Parser rules:

- Use explicit little-endian reads. Do not `reinterpret_cast` a C++ struct.
- Keep static map cell data immutable after decode.
- Store decoded cells row-major in C++ for access, but preserve column-major file semantics in tests.
- Decode AntiHack maps in the shared decoder so client and server cannot diverge.
- Invalid image ids are preserved in map data and skipped at render time when the WIL frame is missing.
- Short reads fail the map load. They must not create partially trusted collision data.

## 2. Tile and WIL/WIX Resource Mapping

Delphi resource declarations are in `Source/Client/WIL/Path.pas`.

| Resource type | Delphi library | Index rule | Transparency/blend | Render layer | C++ location | Verify |
| --- | --- | --- | --- | --- | --- | --- |
| Background tile | `WTiles` / `Data/Tiles.wil` | `(BkImg & $7FFF) - 1`, only when map x and y are both even | Normal draw | `DrawTileMap` pass 1 | `ArchiveId::tiles` | Confirm even-cell rule |
| Small tile | `WSmTiles` / `Data/SmTiles.wil` | `MidImg - 1` | Transparent draw | `DrawTileMap` pass 2 | `ArchiveId::sm_tiles` | Confirm full-pass order |
| Object/front | `WObjects1..7` / `Objects*.wil` | `(FrImg & $7FFF) - 1`, plus animation and door offsets | Transparent or `DrawBlend` | Row-scanned object pass | `ArchiveId::objects1..7` | Confirm blend pixels |
| Drop item | `WDnItem` / `DnItems.wil` | `Looks` | Transparent draw | Row-scanned before actors | `ArchiveId::dn_items` | Confirm focused item brightening |
| Mini map | `WMMap` / `mmap.wil` | `MiniMapIndex` | Normal or blend by minimap style | End of world scene | `ArchiveId::mmap` | Confirm `MiniMapInfo.txt` mapping |

Object archive selector:

| `Area` | Delphi archive |
| ---: | --- |
| 0 | `Objects.wil` |
| 1 | `Objects2.wil` |
| 2 | `Objects3.wil` |
| 3 | `Objects4.wil` |
| 4 | `Objects5.wil` |
| 5 | `Objects6.wil` |
| 6 | `Objects7.wil` |
| other | `Objects.wil` fallback |

Object animation formula from `PlayScn.pas`:

```text
fridx = FrImg & $7FFF
ani = AniFrame
blend = (ani & $80) != 0
ani = ani & $7F
if ani > 0:
  fridx += (MainAniCount mod (ani + ani * AniTick)) div (1 + AniTick)
if (DoorOffset & $80) != 0 and (DoorIndex & $7F) > 0:
  fridx += DoorOffset & $7F
frame_index = fridx - 1
```

## 3. Client Render Trace

Confirmed Delphi entry:

```text
ClMain.Timer1Timer
-> DecodeMessagePacket
-> ProcessKeyMessages
-> ProcessActionMessages
-> DScreen.DrawScreen
-> CurrentScene.PlayScene
-> DWinMan.DirectPaint
-> DScreen.DrawScreenTop
-> DScreen.DrawHint
-> DXDraw1.Flip
```

Confirmed `TPlayScene.PlayScene` world render order:

| Order | Delphi operation | Content | Sorting rule | C++ target | Risk |
| ---: | --- | --- | --- | --- | --- |
| 1 | `Map.UpdateMapPos(Myself.Rx, Myself.Ry)` | Load visible 3x logical segment | Player logical position | world map load/viewport | Do not async reorder |
| 2 | `DrawTileMap` pass 1 | `BkImg` background tiles | Full scan | `render_tiles` background phase | C++ currently combines bg/mid in one loop |
| 3 | `DrawTileMap` pass 2 | `MidImg` small tiles | Full scan | `render_tiles` mid phase | Must remain after all background |
| 4 | `ObjSurface.Draw(MapSurface)` | Copy map surface | N/A | renderer base pass | Preserve black clear |
| 5 | First `FrImg` loop | 48x32 small objects | Full extended row scan | `render_small_objects` | Frame dimensions decide layer |
| 6 | `GroundEffectList` | Ground magic/effects | List order | `render_ground` | Must be before row actors |
| 7 | Row loop: large object | Non-48x32 `FrImg` objects | Row scan, left to right | `render_large_objects_for_row` | Foreground occlusion depends on this |
| 8 | Row loop: events | Map events | Matching row | event render | C++ event render is incomplete |
| 9 | Row loop: dropped items | Ground items | Matching row, list order | `render_ground_items_for_row` | Must be before actors |
| 10 | Row loop: actors | NPC/monster/player | `Ry - DownDrawLevel == row`, `ActorList` order | `render_actor` | `unordered_map` fallback is unsafe |
| 11 | Row loop: fly effects | Flying magic | Matching row | `render_fly` | Must be after actors in that row |
| 12 | Focus redraw | Myself/focus/magic target highlight | Explicit objects | selection blend pass | Must be before actor/effect overlays |
| 13 | Actor effects | `actor.DrawEff` | ActorList order | actor overlay/effect pass | Verify exact placement |
| 14 | Magic effects | `EffectList` | List order | `render_overlay` | Verify overlay class split |
| 15 | Item flash | Dropped item flash | Drop list order | item flash pass | Missing/partial |
| 16 | Death grayscale | Entire scene effect | If self dead | death effect pass | Must happen before world blit/UI |
| 17 | World blit | `ObjSurface` to screen | N/A | present world scene | Preserve minimap after this |
| 18 | Mini map | `DrawMiniMap` | End of scene | minimap UI/world overlay | Confirm coordinates |
| 19 | UI/top/hint | `DirectPaint`, `DrawScreenTop`, `DrawHint` | Fixed order | UI tree/top/hint | Hint must be last before flip |

Compatibility rule: do not replace the row scan with a modern z-sort. The visible result depends on the exact order of large objects, dropped items, actors, and fly effects per row.

## 4. Collision and Movement Semantics

Client `MapUnit.pas`:

- `CanMove(x,y)` returns true only when both `(BkImg & $8000) == 0` and `(FrImg & $8000) == 0`.
- If the cell is a door, movement also requires `DoorOffset & $80 != 0`.
- `CanFly(x,y)` checks only `FrImg & $8000` plus door open state.
- `MarkCanWalk` mutates the local visible `FrImg` high bit. This is a client-side visible segment operation and must not mutate shared static map data.

Server `Envir.pas`:

- Static map load sets `MoveAttr` from `BkImg` and `FrImg` high bits.
- `CanWalk(x,y,allowdup)` requires `MoveAttr == MP_CANMOVE`.
- If `allowdup` is false, `ObjList` is scanned for `OS_MOVINGOBJECT` that blocks.
- A moving object blocks only when it is not ghost, holds place, is not dead, is not hidden, and is not supervisor mode.
- Dropped items do not block movement.
- Item add fails on static non-walk cells and on cells with 5 or more objects, except gold can merge up to `BAGGOLD`.
- `MoveToMovingObject` removes the old moving object entry before adding the new entry.

Server dynamic object shapes confirmed in C++:

| Legacy shape | Meaning | Blocks movement | C++ equivalent |
| --- | --- | --- | --- |
| `OS_MOVINGOBJECT` | Player/monster/NPC | Conditional | `LegacyMapObjectShape::moving_object` |
| `OS_ITEMOBJECT` | Dropped item | No | `item_object` |
| `OS_GATEOBJECT` | Map transfer gate | No direct block | `gate_object` |
| `OS_EVENTOBJECT` | Event/magic terrain | Optional | `event_object` with `blocks_walk` |

Known C++ gaps:

- `shared/legacy/map_document.hpp` handles normal maps only; AntiHack support is currently client-only in `AssetManager::decode_map`.
- `LegacyMapEnvironment::can_fly_line` uses a modern step loop and does not match the Delphi 10-step `Round` behavior. The exact Delphi behavior still needs a focused fixture because the source appears to increment from the original point each loop.
- C++ event rendering and event visibility are intentionally incomplete pending packet shape confirmation.

## 5. Door Semantics

Client:

- A door cell has `DoorIndex & $80`.
- Door group number is `DoorIndex & $7F`.
- Door open visual state is `DoorOffset & $80`.
- When open and group number is non-zero, frame offset adds `DoorOffset & $7F`.
- `OpenDoor` updates cells in a local neighborhood with matching group.
- `CloseDoor` clears the open bit in a local neighborhood with matching group.

Server:

- `TEnvirnoment.LoadMap` groups door cells by door number and proximity within 10 cells.
- Each group has a shared core: open state, lock state, lock key, open time.
- `ApplyDoors` adds door objects to the map.
- `ArroundDoorOpened` checks doors in the 3x3 area around the actor.

C++:

- `LegacyMapEnvironment::load_doors_from_map` already implements door number plus 10-cell grouping.
- `open_doors_around` and `close_expired_doors` maintain shared cores.
- `MapActor::broadcast_open_doors` / `broadcast_close_doors` emit door packets to viewers.

PR follow-up: after shared AntiHack decode, server and client door cells must derive from identical decoded cell data.

## 6. Map Switching Sequence

Delphi server `TCreature.EnterAnotherMap` sequence:

```text
Validate target map/level/quest/castle
-> save old env/x/y
-> Disappear
   -> PEnvir.DeleteFromMap(CX, CY, OS_MOVINGOBJECT, self)
   -> SendRefMsg(RM_DISAPPEAR)
-> clear MsgTargetList / VisibleItems / VisibleEvents / VisibleActors
-> enter target map and appear
```

C++ cross-map sequence in `MapActor::try_gate_transfer` and `try_item_map_move`:

```text
queue SM_CLEAROBJECTS
queue SM_CHANGEMAP
queue save character
detach slaves
remove actor from visibility
environment.delete_from_map
visibility.erase
objects.erase
dispatch cross-map spawn_player mail
```

Target map spawn sequence currently uses:

```text
SM_NEWMAP
SM_LOGON
SM_USERNAME
SM_AREA_STATE
SM_MAPDESCRIPTION
SM_ABILITY
SM_SENDUSEITEMS
SM_SENDMYMAGIC
visibility sync
```

Compatibility requirements:

- Client must clear old actors/items/events/effects before applying new-map objects.
- Late messages from the old map must be dropped by map generation or equivalent session boundary.
- Same-map transfer still needs a clear/change/logon/sync refresh because Delphi clears visible lists.
- A failed transfer must not delete the old moving object.

## 7. Protocol Map

Map-relevant client messages from `Grobal2.pas`:

| Message | Id | Meaning | C++ requirement |
| --- | ---: | --- | --- |
| `CM_TURN` | 3010 | Turn | Preserve direction update order |
| `CM_WALK` | 3011 | Walk | Immediate collision update on success |
| `CM_RUN` | 3013 | Run | Validate intermediate and final cells |
| `CM_HIT` etc. | 3014+ | Attack | Area/safe-zone checks use current map state |
| `CM_SPELL` | 3017 | Cast | Magic/fire/fly checks use map collision |
| `CM_DROPITEM` | 1000 | Drop item | Item object semantics |
| `CM_PICKUP` | 1001 | Pick up item | Item visibility and object removal |
| `CM_OPENDOOR` | 1002 | Open door | Door core and client visual sync |
| `CM_CLICKNPC` | 1010 | NPC click | Actor lookup in visible/current map |
| `CM_DROPGOLD` | 1016 | Drop gold | Gold merge semantics |
| `CM_WANTMINIMAP` | 1033 | Request minimap | `MiniMapInfo` mapping |

Map-relevant server messages:

| Message | Id | Meaning | C++ requirement |
| --- | ---: | --- | --- |
| `SM_TURN` | 10 | Actor appears/turns | Used by visibility sync |
| `SM_WALK` | 11 | Actor walk | Sent after server movement succeeds |
| `SM_RUN` | 13 | Actor run | Sent after two-step validation |
| `SM_DISAPPEAR` | 30 | Actor leaves visibility | Must be ordered with visibility diff |
| `SM_DAYCHANGING` | 46 | Day/night | Light pipeline follow-up |
| `SM_NEWMAP` | 51 | Initial map entry | Contains map id and player x/y |
| `SM_MAPDESCRIPTION` | 54 | Map title | Comes after logon sequence in C++ |
| `SM_ITEMSHOW` | 610 | Item appears | Row render before actor |
| `SM_ITEMHIDE` | 611 | Item disappears | Must remove by id/order |
| `SM_OPENDOOR_OK` | 612 | Door opened | Updates client door offset |
| `SM_CLOSEDOOR` | 614 | Door closed | Updates client door offset |
| `SM_CLEAROBJECTS` | 633 | Clear visible objects | Required before change map |
| `SM_CHANGEMAP` | 634 | Change current map | Must gate late messages |
| `SM_MAGICFIRE` | 638 | Magic/fire effect | Ground/fly/effect layer mapping |
| `SM_AREA_STATE` | 708 | Safe/fight/no rules | Must follow current map/x/y |

## 8. Current C++ Reuse and Refactor Boundary

Reuse:

- `AssetManager` WIL/WIX decoder and archive ids.
- `WorldScene` row rendering structure.
- `legacy::map_render_math` viewport constants and click conversion.
- `LegacyMapEnvironment` dynamic object model.
- `MapActor` transfer and visibility hooks.

Refactor only where required:

- Move AntiHack map decode into shared `map_document`.
- Split client render trace phases so background and mid tiles are separately verifiable.
- Replace or add Delphi-compatible `CanFly` logic.
- Add map generation/session boundary checks for stale client messages.
- Add draw/visibility traces as tests before D3D11 batching.

Do not refactor:

- Do not introduce ECS or a new generic tilemap engine.
- Do not collapse object rendering into a global z-sort.
- Do not merge client static cell data with server dynamic occupancy.
- Do not add feature flags for legacy behavior; the legacy path is the behavior.

## 9. Open Verification Items

These require follow-up source audit before implementation:

- Exact `DrawLight`, darkness, daylight, actor light, torch/magic light behavior.
- Exact `DrawBlend` pixel formula and whether C++ `blit_rgba_legacy_blend` is visually equivalent.
- Exact event object packet shape and client event rendering order.
- Full client click selection priority: UI, item, NPC, monster, player, ground.
- Exact Delphi server `CanFly` loop behavior and whether the apparent non-accumulating step is intentional.
- Whether Delphi M2Server supports AntiHack maps in another branch or hidden include.
- Mini map point colors and group/guild/NPC display rules.
- Same-frame ordering for movement, death, drop, and spell packets in `SendRefMsg` flows.

## 10. PR-1 Acceptance Checklist

- `.map` normal and AntiHack header documented.
- Cell field layout documented.
- Tile/resource mapping documented.
- Object animation and door frame formula documented.
- Client render trace documented in real Delphi order.
- Server collision and dynamic occupancy rules documented.
- Map switch message and object cleanup sequence documented.
- Current C++ reuse/gap list documented.
- Unconfirmed behavior marked as open verification, not guessed.
