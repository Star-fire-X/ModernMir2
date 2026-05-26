# Monster PR-10 Visibility / Broadcast Compatibility

## Scope and Non-Goals

PR-10 locks the C++ visibility/broadcast ordering used by monster show, walk,
attack aftermath, death/drop visibility, and player nine-grid re-sync. It keeps
the existing `Monster`, `MapActor`, `RuntimeDispatch`, and `LegacyMapEnvironment`
runtime shape.

This PR does not change monster AI, target selection, damage formulas, special
Race behavior, death settlement ownership, drop rolling, task triggers, or
protocol identifiers. PR-9 remains the death/drop settlement baseline; this PR
only makes visibility output deterministic around that baseline.

## Delphi Evidence

- `Source/M2Server/ObjBase.pas:1497` defines `TCreature.SendRefMsg`; this is the
  common reference-message path for turn, walk, hit, struck, death, and item
  visibility side effects.
- `Source/M2Server/ObjBase.pas:1690` defines `TCreature.SearchViewRange`.
  Existing visible actors are marked/check-pruned, new actors are appended, and
  first-time actors are sent with `RM_TURN` around `Source/M2Server/ObjBase.pas:1874`.
- `Source/M2Server/ObjBase.pas:1901` sends item visibility with `RM_ITEMSHOW`
  after actor visibility processing inside `SearchViewRange`.
- `Source/M2Server/ObjBase.pas:2076` sends turn with `RM_TURN`;
  `Source/M2Server/ObjBase.pas:2188` uses `SendRefMsg` for walk-like movement.
- `Source/M2Server/ObjBase.pas:2806` sends death with `RM_DEATH`.
- `Source/M2Server/ObjBase.pas:6716` and `Source/M2Server/ObjBase.pas:6876`
  cover direct item show/hide reference messages.
- `Source/Common/Grobal2.pas:1171`, `1172`, `1192`, and `1221` define
  `RM_TURN`, `RM_WALK`, `RM_DEATH`, and `RM_ITEMSHOW`.

The critical PR-10 rule is not “make a smarter visibility system”; it is to stop
unordered C++ containers from choosing packet order where Delphi used map cell
scan and `VisibleActors` list order.

## C++ Anchors

- `ModernServer/src/world/map_actor_visibility.hpp:40`:
  `ordered_visible_actor_ids` now scans cells in `x` outer / `y` inner order and
  then cell `obj_list` order.
- `ModernServer/src/world/map_actor_visibility.hpp:66`:
  `ordered_visible_item_ids` uses the same cell scan for item show order.
- `ModernServer/src/world/map_actor_visibility.hpp:91`:
  `sync_player_visibility` emits actor shows before item shows and no longer
  lets `objects_` or `ground_items_` iteration order pick first-viewport packets.
- `ModernServer/src/world/map_actor_visibility.hpp:176`:
  actor movement visibility re-sync walks players in deterministic map order.
- `ModernServer/src/world/map_actor_visibility.hpp:259`:
  actor disappearance removes watchers in deterministic player order.
- `ModernServer/src/world/map_actor_monster.hpp:839`:
  successful monster walk is still the only monster movement path that calls
  `sync_visibility_after_actor_move`.
- `ModernServer/src/world/map_actor_monster.hpp:575`:
  PR-9 `finalize_monster_death` remains the death/drop settlement entry; PR-10
  does not reorder that settlement.

## Locked Behavior

- A player entering visibility sees actor `SM_TURN`/turn-like packets before
  ground-item `SM_ITEMSHOW` packets.
- New actor show order is stable by legacy cell scan, not by `unordered_map`.
- New item show order is stable by legacy cell scan, not by `unordered_map`.
- Stale actor and item hides are sorted before sending so an unordered visibility
  set cannot reorder same-frame hides.
- Monster movement still emits `SM_WALK` only after a successful walk; blocked
  movement, cooldown, and WalkWait continue to emit no walk packet.
- Attack/struck/death packet order remains owned by PR-7/PR-9 paths. PR-10 does
  not introduce an async broadcast queue.

## Tests

- `ModernServer/tests/monster_visibility_order_smoke.cpp` covers:
  - two monsters spawned in non-cell order are shown to a newly entering player
    in legacy cell order;
  - actor show packets precede item show packets when a player enters a cell
    containing both visible actors and ground items.
- Existing coverage remains relevant:
  - `mir2_visibility_delta_smoke`;
  - `mir2_visibility_phase3_smoke`;
  - `mir2_attack_protocol_golden_smoke`;
  - `mir2_client_v1_semantic_golden_smoke`.

## Confirmed Gaps

- Delphi `SearchViewRange` also maintains reverse `RefObjCount`; C++ visibility
  still keeps per-player visibility sets and does not fully recreate Delphi
  reference counting.
- EventObject wire show/hide remains intentionally suppressed until the exact
  packet shape is confirmed.
- Full legacy/client_v1 visual equivalence for every monster special attack is
  still PR-11 golden-matrix work.
