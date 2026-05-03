# Map Tile Rendering Phase 1 Alignment

This document is the phase-1 source map for aligning the ModernClient map tile
renderer with the Delphi client. It is intentionally evidence-first: phase 1
adds documentation and smoke tests only, without changing runtime rendering
semantics.

## Scope

- Locate the Delphi map loading, map cell, resource, camera, draw-order, and
  coordinate conversion code.
- Locate the ModernClient equivalents that already exist.
- Record confirmed parity, confirmed gaps, and items that need more evidence.
- Keep implementation fixes for phase 2 and later.

## Delphi Entry Points

| Delphi file | Class/function/variable | Responsibility | Caller | C++ location | Migration status |
|---|---|---|---|---|---|
| `Source/Client/MapUnit.pas:32` | `TMapHeader` | Normal `.map` header, including width and height. | `TMap.Open`, `TMap.updatemap` | `src/assets/asset_manager.cpp:407`, `MapDocument` in `src/assets/asset_manager.hpp` | Partially migrated. Normal 52-byte header is supported. |
| `Source/Client/MapUnit.pas:40` | `TMapHeader_AntiHack` | Alternate header for `LABY01..LABY04` and `SNAKE`, with XOR-decoded width/height. | `TMap.updatemap` | None | Missing. Needs further confirmation with matching map assets. |
| `Source/Client/MapUnit.pas:49` | `TMapInfo` | 12-byte map cell: `BkImg`, `MidImg`, `FrImg`, `DoorIndex`, `DoorOffset`, `AniFrame`, `AniTick`, `Area`, `light`. | Map loading, movement, draw code | `MapCell` in `src/assets/asset_manager.hpp:78` | Migrated for the normal format. |
| `Source/Client/MapUnit.pas:65` | `TMap` | Runtime map window cache: `MArr`, `ClientRect`, `OldClientRect`, `BlockLeft`, `BlockTop`, `CurrentMap`. | `TPlayScene` | `MapDocument`, `WorldScene` | Partially migrated. ModernClient keeps full `MapDocument`, but does not model Delphi `BlockLeft/BlockTop` cache. |
| `Source/Client/MapUnit.pas:184` | `TMap.updatemap` | Reads visible map block from file; normal maps use a 52-byte header and column-major cells. | `TMap.UpdateMapSquare`, `TMap.UpdateMapPos` | `AssetManager::decode_map` in `src/assets/asset_manager.cpp:407` | Normal column-major decode is migrated into a row-major `MapDocument`. |
| `Source/Client/MapUnit.pas:269` | `TMap.UpdateMapPos` | Computes block cache origin from player position and refreshes `MArr`. Contains special map `"3"` passability edits. | `TPlayScene.DrawScene` | `WorldScene::sync_map`, direct `MapDocument` access | Partially migrated. Special map `"3"` hardcoded passability edits are missing. |
| `Source/Client/MapUnit.pas:332` | `TMap.CanMove` | Passability: blocked by `BkImg & $8000`, `FrImg & $8000`, or closed door bits. | Movement checks | `MapDocument::can_move` in `src/assets/asset_manager.hpp:108` | Partially migrated. Door closed checks are missing. |
| `Source/Client/MapUnit.pas:429` | `TMap.DrawMiniMap` | Mini-map rendering from `WTiles`, `WSmTiles`, and objects. | `TPlayScene.DrawScene` | UI/minimap path needs further confirmation | Not part of phase 1 rendering fix. |
| `Source/Common/Grobal2.pas:738` | `LOGICALMAPUNIT`, `UNITX`, `UNITY`, `HALFX`, `HALFY` | Legacy map block and tile pixel constants: 40, 48, 32, 24, 16. | Map, scene, actor code | `scenes.cpp:134`, `legacy_animation.cpp` | Mostly migrated. Constants are duplicated rather than centralized. |
| `Source/Common/Grobal2.pas:1794` | `UpInt` | Delphi-style integer ceiling used by mouse-to-map conversion. | `TPlayScene.CXYfromMouseXY` | `WorldScene::screen_to_map_tile` in `src/scene/scenes.cpp:4878` | Not migrated exactly. C++ currently uses `std::lround`. |
| `Source/Client/PlayScn.pas:249` | `TPlayScene.DrawTileMap` | Cached background drawing: large ground tiles and small tiles. | `TPlayScene.DrawScene` | `WorldScene::render_tiles` in `src/scene/scenes.cpp:5532` | Partially migrated. No Delphi `MapSurface` cache/crop model. |
| `Source/Client/PlayScn.pas:554` | `TPlayScene.DrawScene` | Main play renderer: viewport, ground, objects, actors, items, effects, UI copy. | Main scene loop | `WorldScene::render` in `src/scene/scenes.cpp:4797` | Partially migrated. Several draw-order and camera gaps remain. |
| `Source/Client/PlayScn.pas:573` | `Map.ClientRect` setup | Visible map range: `Rx-9..Rx+9`, `Ry-9..Ry+8`. | `TPlayScene.DrawScene` | `WorldScene::render` in `src/scene/scenes.cpp:4818` | Partially migrated. C++ uses raw actor `x/y`, not always pose `Rx/Ry`. |
| `Source/Client/PlayScn.pas:600` | `defx`, `defy` | Legacy map-to-screen origin, including `Myself.ShiftX/Y`. | Map/object/actor draws | `kDefX`, `kDefY` and draw functions in `src/scene/scenes.cpp` | Partially migrated. Map/object layers do not fully consume self shift. |
| `Source/Client/PlayScn.pas:604` | Small object pass | Draws only object frames sized 48x32 before actors. | `TPlayScene.DrawScene` | `WorldScene::render_small_objects` | Migrated at a high level. |
| `Source/Client/PlayScn.pas:663` | Large object and actor pass | Draws large/blended objects row-by-row, then events/items/actors/fly effects. | `TPlayScene.DrawScene` | `WorldScene::render_large_objects_and_actors`, `render_ground_items` | Partially migrated. Ground items are not row-integrated. |
| `Source/Client/PlayScn.pas:734` | Dropped item row draw | Draws dropped items inside each row before actors. | `TPlayScene.DrawScene` | `WorldScene::render_ground_items` in `src/scene/scenes.cpp:5694` | Gap. ModernClient draws ground items after large objects and actors. |
| `Source/Client/PlayScn.pas:761` | Actor row draw | Draws actors when `j = actor.Ry - BlockTop - DownDrawLevel`. | `TPlayScene.DrawScene` | `WorldScene::render_large_objects_and_actors` | Partially migrated. `DownDrawLevel` behavior needs further confirmation. |
| `Source/Client/PlayScn.pas:1119` | `ScreenXYfromMCXY` | Map-cell to screen-pixel conversion using `Rx/Ry` and `ShiftX/Y`. | Magic, selection, item pickup | No exact helper; scattered formulas in `scenes.cpp` | Gap. Needs exact shared helper in phase 2. |
| `Source/Client/PlayScn.pas:1127` | `CXYfromMouseXY` | Screen-pixel to map-cell conversion using `UpInt`. | Mouse picking | `WorldScene::screen_to_map_tile` | Gap. Current formula differs. |
| `Source/Client/WIL/Path.pas:30` | `WTiles_IMAGEFILE`, `WSmTiles_IMAGEFILE`, `WObjects_IMAGEFILE` | Legacy map resource file names. | `LoadWMImagesLib` | `ArchiveId` and `AssetManager::archive_path` | Migrated for WIL/WIX archives. |
| `Source/Client/WIL/Path.pas:411` | `GetObjs` | Maps `Area` 0..6 to `Objects.wil..Objects7.wil`. | Map object draw | `object_archive_for_area` in `src/scene/scenes.cpp:414` | Migrated. |
| `Source/Client/WIL/Path.pas:425` | `GetObjsEx` | Gets object frame plus hot spot for blend draw. | Blended map objects | `SpriteFrame::hotspot_x/y` and large object blend draw | Partially migrated. Exact blend math needs confirmation. |
| `Source/Client/WIL/WIL.pas:143` | `TWILImages.Initialize` | Loads WIL/WIX, detects legacy header variants and palette formats. | Resource startup | `AssetManager::require_archive` | Migrated for current WIL/WIX usage. WZL/FIR needs further confirmation. |
| `Source/Client/WIL/WIL.pas:620` | `GetCachedSurface` | Missing/invalid frames return nil. | All resource draw paths | `AssetManager::get_frame` | Migrated: returns `nullptr` and caches failed archives. |
| `Source/Client/Textures.pas:292` | `TTexture.TransparentColor` | Default color key `$0000`. | Transparent draws | `rgba_from_palette`, `rgba_from_565`, `SoftwareSurface::blit_rgba` | Partially migrated through alpha conversion. Exact edge behavior needs visual checks. |
| `Source/Client/cliUtil.pas:158` | `DrawBlend`, `DrawBlendEx` | Custom blend routines for translucent map/effect objects. | `TPlayScene.DrawScene` | `draw_sprite(..., alpha=168)` | Approximate only. Needs pixel-level confirmation. |
| `Source/Client/Actor.pas` | `Rx`, `Ry`, `ShiftX`, `ShiftY`, `DownDrawLevel` | Actor render pose and row ordering. | `TPlayScene.DrawScene` | `ActorRenderPose`, `legacy_shift` | Partially migrated. Camera should consume self pose more exactly. |

## ModernClient Entry Points

| C++ file | Class/function/variable | Responsibility | Delphi source | Status |
|---|---|---|---|---|
| `src/assets/asset_manager.hpp:39` | `ArchiveId` | Typed WIL archive identifiers, including `tiles`, `sm_tiles`, and `objects1..objects7`. | `WIL/Path.pas` globals | Implemented. |
| `src/assets/asset_manager.hpp:66` | `SpriteFrame` | Decoded frame dimensions, hot spot, and BGRA pixels. | `TTexture`, `TWMImageInfo` | Implemented for current renderer. |
| `src/assets/asset_manager.hpp:78` | `MapCell` | Modern representation of Delphi `TMapInfo`. | `TMapInfo` | Implemented. |
| `src/assets/asset_manager.hpp:92` | `MapDocument` | Full decoded map and bounds-checked `cell(x,y)`. | `TMap.MArr`, map file reads | Implemented, but not block-cached like Delphi. |
| `src/assets/asset_manager.hpp:108` | `MapDocument::can_move` | Collision check for high bits. | `TMap.CanMove` | Missing closed door logic. |
| `src/assets/asset_manager.cpp:407` | `AssetManager::decode_map` | Reads normal `.map` files: 52-byte header, 12-byte cells, source column-major to target row-major. | `TMap.updatemap` | Implemented for normal maps. |
| `src/assets/asset_manager.cpp:460` | `AssetManager::archive_path` | Maps `ArchiveId` to WIL file names. | `LoadWMImagesLib` paths | Implemented for current archives. |
| `src/scene/scenes.cpp:414` | `object_archive_for_area` | Maps map cell `area` to `objects1..objects7`. | `GetObjs`, `GetObjsEx` | Implemented. |
| `src/scene/scenes.cpp:4797` | `WorldScene::render` | Main world rendering pipeline. | `TPlayScene.DrawScene` | Partially migrated. |
| `src/scene/scenes.cpp:4878` | `WorldScene::screen_to_map_tile` | Mouse-to-map conversion. | `CXYfromMouseXY` | Confirmed mismatch. |
| `src/scene/scenes.cpp:5532` | `WorldScene::render_tiles` | Ground and small ground tile draw. | `DrawTileMap` | Partially migrated. |
| `src/scene/scenes.cpp:5557` | `WorldScene::render_small_objects` | 48x32 object pass. | First object pass in `DrawScene` | Migrated at a high level. |
| `src/scene/scenes.cpp:5581` | `WorldScene::render_large_objects_and_actors` | Large objects, actors, fly effects by row. | Second object pass in `DrawScene` | Partially migrated. |
| `src/scene/scenes.cpp:5694` | `WorldScene::render_ground_items` | Ground dropped items. | Row-integrated dropped item draw | Confirmed draw-order gap. |
| `src/animation/legacy_animation.cpp:969` | `legacy_map_object_frame` | Delphi object animation frame and door offset rule. | `FrImg`, `AniFrame`, `AniTick`, `DoorOffset` math in `DrawScene` | Implemented and tested. |
| `src/animation/legacy_animation.cpp:993` | `legacy_map_object_blend` | `AniFrame & $80` blend marker. | `DrawScene`, `DrawMiniMap` | Implemented. |
| `src/animation/legacy_animation.cpp:1127` | `LegacyAnimationClock::advance` | 100 ms movement tick and 50 ms animation tick. | `TPlayScene.Run` | Implemented. |
| `src/render/software_renderer.cpp:161` | `SoftwareSurface::blit_rgba` | Software alpha/color-key style blit target. | `TTexture.Draw` | Implemented, but exact Delphi color-key edge behavior needs visual checks. |

## Data and Behavior Mapping

| Delphi data/function/behavior | Role | C++ equivalent | Implemented | Phase-2+ work |
|---|---|---|---|---|
| 52-byte normal map header | Width/height of normal maps | `AssetManager::decode_map` | Yes | Add tests for malformed maps and document header variants. |
| `TMapHeader_AntiHack` | XOR-decoded special maps | None | No | Add only if matching assets are present. |
| `TMapInfo` | Per-cell map record | `MapCell` | Yes | Keep byte layout covered by smoke tests. |
| Column-major map file storage | File offset is `header + (x * height + y) * 12` | Decode into row-major `cells` | Yes | Keep regression test. |
| `BkImg` | Ground tile index and high-bit block flag | `MapCell::bk_img` | Partially | C++ masks high bit for drawing; Delphi draw path evidence needs more visual confirmation. |
| `MidImg` | Small ground tile index | `MapCell::mid_img` | Yes | Verify screen offset against Delphi after camera fix. |
| `FrImg` | Object/front image and high-bit block flag | `MapCell::fr_img` | Yes | Confirm masking and row draw ordering. |
| `DoorIndex`, `DoorOffset` | Door animation and closed-door collision | `MapCell::door_index`, `door_offset` | Partial | Add closed-door collision parity. |
| `AniFrame`, `AniTick` | Object animation frame count and speed | `legacy_map_object_frame` | Yes | Check all blend and zero-tick edge cases. |
| `Area` | Selects `Objects.wil..Objects7.wil` | `object_archive_for_area` | Yes | Add render-path tests after refactorable helper exists. |
| `light` | Map light source metadata | `MapCell::light` | Parsed | Rendering effect needs further confirmation. |
| `WTiles.Images[BkImg-1]` | Large ground tile resource | `ArchiveId::tiles`, `get_frame` | Yes | Ensure even x/y rule and exact draw offset. |
| `WSmTiles.Images[MidImg-1]` | Small tile resource | `ArchiveId::sm_tiles`, `get_frame` | Yes | Ensure offset `+UNITY` and clipping parity. |
| `GetObjs(Area, idx)` | Object resource lookup | `object_archive_for_area` and `get_frame` | Yes | Keep missing-resource behavior silent/null. |
| `GetObjsEx(Area, idx, ax, ay)` | Object lookup with hot spot | `SpriteFrame::hotspot_x/y` | Partial | Exact blend and hot spot draw should be screenshot-tested. |
| `ClientRect = Rx/Ry +/- ranges` | Visible map range | `WorldScene::render` | Partial | Use self render pose, not just raw actor position. |
| `ScreenXYfromMCXY` | Map to screen coordinate | Scattered draw formulas | Partial | Introduce exact helper in phase 2. |
| `CXYfromMouseXY` and `UpInt` | Mouse to map coordinate | `screen_to_map_tile` | No | Replace `lround` formula with Delphi-compatible formula. |
| `DrawTileMap` cache | Cached ground layer surface | None | No | Decide whether cache is needed after correctness fixes. |
| First object pass | Draw 48x32 objects before actors | `render_small_objects` | Yes | Verify offsets after camera fix. |
| Second object pass | Draw large/blend objects row-by-row | `render_large_objects_and_actors` | Partial | Integrate items/events/actors in exact row order. |
| Dropped items row order | Items draw before actors on same row | `render_ground_items` | No | Move into row pass in phase 3. |
| Transparent color `$0000` | Color-key transparency | WIL decode to alpha and `blit_rgba` | Partial | Verify black edge behavior visually. |
| Missing surfaces return nil | Missing resources skip draw | `get_frame` returns `nullptr` | Yes | Confirm no repeated log spam or stalls. |
| `MainAniCount` 50 ms | Map/effect animation clock | `LegacyAnimationClock` | Yes | Confirm against screenshots for animated tiles. |

## Confirmed Phase-1 Gaps

1. Camera and viewport are not fully Delphi-compatible. Delphi bases visible
   range and scroll offsets on `Myself.Rx/Ry` plus `ShiftX/ShiftY`; C++ map and
   object layers currently derive the range from raw `ActorState.x/y`.
2. Mouse picking is not Delphi-compatible. Delphi uses `UpInt`, while C++ uses
   `std::lround`.
3. Ground items are not row-integrated. Delphi draws dropped items inside the
   row pass before actors; C++ draws them after large objects and actors.
4. Door collision is incomplete. Delphi blocks closed doors when both
   `DoorIndex & $80` and `DoorOffset & $80` are set; C++ only checks `BkImg` and
   `FrImg` high bits.
5. Map `"3"` hardcoded passability edits in `TMap.UpdateMapPos` are not
   represented in C++.
6. Boundary/background fill differs: Delphi fills the play surfaces with black;
   C++ clears the world render with a dark blue color.
7. `DrawBlend`/`DrawBlendEx` are only approximated by fixed alpha in C++.
8. AntiHack map headers are not decoded in C++; current availability of matching
   `LABY01..LABY04`/`SNAKE` assets needs further confirmation.

## Phase-1 Test Coverage Added

The `modern_client_map_render_alignment_smoke` test is intended to lock down
facts needed before phase-2 rendering fixes:

- Normal map decode byte layout: 52-byte header, 12-byte cells, column-major
  file storage, row-major `MapDocument::cell(x,y)`.
- Current resource availability: real root initializes, `Map/0.map` loads,
  `Tiles.wil`, `SmTiles.wil`, and `Objects.wil` can decode representative
  frames.
- Delphi constants and formulas: `LOGICALMAPUNIT=40`, `UNITX=48`, `UNITY=32`,
  visible range `Rx-9..Rx+9` and `Ry-9..Ry+8`, `UpInt`, and
  `CXYfromMouseXY`.
- Tile/resource rules: large ground tiles draw only on even x/y cells,
  `BkImg-1` and `MidImg-1` frame indices, and `Area -> Objects1..Objects7`.
- Existing animation parity: `legacy_map_object_frame`,
  `legacy_map_object_blend`, and door offset frame adjustment.

## Needs Further Confirmation

- Exact visual result of Delphi `DrawBlend` and `DrawBlendEx` compared with the
  current fixed-alpha software renderer.
- Whether the active map/resource set includes AntiHack maps that require
  `TMapHeader_AntiHack`.
- Whether WZL/FIR resource formats are required by this ModernClient target.
- Whether Delphi mini-map parity should be part of the same map-rendering track
  or a separate UI track.
