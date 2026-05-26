# Map Compatibility Phase1 Findings

This document is the PR-1 baseline for map compatibility. It records Delphi expectations, current C++ behavior, and planned fix PRs. PR-1 does not change runtime behavior.

## Baseline Matrix

| Topic | Delphi expectation baseline | Current C++ baseline | Evidence | Planned fix |
|---|---|---|---|---|
| Safe-zone and area-state | `InSafeZone` should include `LawFull || badman±10 || StartPoint±10`; `SM_AREASTATE AREA_SAFE` should not be set just because a generic imported `safe_zones` rect exists | `is_safe_zone()` is `law_full || point_in_safe_zones`; `area_state_mask()` sets `AREA_SAFE` from that result | `ModernServer/src/world/map_actor_helpers.hpp` | PR4 |
| Event show/hide wire | Legacy clients expect explicit `SM_SHOWEVENT/SM_HIDEEVENT` wire for event lifecycle | Constants and packet builders are not present yet | `ModernServer/src/protocol/legacy_types.hpp`, `ModernServer/src/world/map_actor_packets.hpp` | PR3 |
| `SM_ITEMSHOW` body shape | Keep explicit baseline for old-client decode behavior | Body is encoded as legacy string item name | `ModernServer/src/world/map_actor_packets.hpp` `make_item_show_packet` | PR5 recheck |
| `CanFly` / `CanFireFly` split | Keep explicit baseline before later changes | `can_fly_line` and `can_fire_fly_line` use different blocking predicates | `ModernServer/src/world/legacy_map_environment.cpp` | PR7 recheck |
| MapQuest exact rule | Remove synthetic "empty mon/item means enter quest" semantics in final compat | Current `trigger_map_quest()` treats empty mon/item as enter trigger | `ModernServer/src/world/map_actor_npc.hpp` | PR6 |
| Runtime MoveAttr overlay | Do not implement speculative runtime overlay without verified Delphi call chain | No runtime MoveAttr overlay hook in map actor/environment paths | `ModernServer/src/world/map_actor.hpp`, `ModernServer/src/world/map_actor.cpp`, `ModernServer/src/world/legacy_map_environment.cpp` | PR7 deferred with guard |
| Map 3 seven patch points | Track exactly seven points before implementation claims | Pending Delphi-side trace confirmation; no runtime claim in PR-1 | This document + PR-1 smoke guard | PR7 decision |

## Map 3 Patch Point Placeholders

- `MAP3_PATCH_1`: pending Delphi trace evidence.
- `MAP3_PATCH_2`: pending Delphi trace evidence.
- `MAP3_PATCH_3`: pending Delphi trace evidence.
- `MAP3_PATCH_4`: pending Delphi trace evidence.
- `MAP3_PATCH_5`: pending Delphi trace evidence.
- `MAP3_PATCH_6`: pending Delphi trace evidence.
- `MAP3_PATCH_7`: pending Delphi trace evidence.

## PR7 Deferred Marker

- `MOVEATTR_OVERLAY_DEFERRED_PR7`: runtime MoveAttr overlay remains deferred until Delphi server-side call-chain evidence is captured.
