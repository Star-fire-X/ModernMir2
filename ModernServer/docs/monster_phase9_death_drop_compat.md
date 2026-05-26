# Monster Phase 9 Death, Drop, and Slave Reward Compatibility

## Scope and Non-Goals

PR-9 locks the current monster death settlement boundary and makes the
death/drop/slave validation path stable under MSVC Debug and G++.

This phase does not change monster AI, movement, target selection, attack damage,
special Race behavior, protocol structure, boss announcements, or visibility
ordering beyond the existing `finalize_monster_death` path. Boss notice sources
remain a later gap because no C++ monster config source currently carries the
legacy boss-announcement rule.

## Delphi Evidence

- `Source/M2Server/UsrEngn.pas:664` implements `MonGetRandomItems`.
- `Source/M2Server/UsrEngn.pas:1036` calls `MonGetRandomItems` while creating the
  monster, so PR-9 keeps spawn-time drop roll instead of re-rolling `MonItems`
  at death.
- `Source/M2Server/ObjBase.pas:2616` through
  `Source/M2Server/ObjBase.pas:2661` handle monster kill experience and map
  quest kill calls for the exp hitter or its group.
- `Source/M2Server/ObjBase.pas:2746` calls `DropUseItems`.
- `Source/M2Server/ObjBase.pas:2748` calls `ScatterBagItems`.
- `Source/M2Server/ObjBase.pas:2750` calls `ScatterGolds`.
- `Source/M2Server/ObjBase.pas:2806` sends `RM_DEATH` after the reward/drop work
  in the traced path.
- `Source/M2Server/ObjBase.pas:2391` implements `ScatterBagItems`.
- `Source/M2Server/ObjBase.pas:2492` implements `ScatterGolds`.
- `Source/M2Server/ObjBase.pas:6680` implements `DropItemDown`.
- `Source/M2Server/ObjBase.pas:6716` broadcasts item appearance with
  `RM_ITEMSHOW`.
- `Source/M2Server/ObjBase.pas:7614` calls `MakeGhost` after
  `GetTickCount - DeathTime > 3 * 60 * 1000`.

These Delphi source anchors are carried forward from
`ModernServer/docs/monster_phase1_delphi_trace.md:500`.

## C++ Anchors

- `ModernServer/src/world/map_actor_monster.hpp:341` restores saved slaves
  without recursively entering `handle_mail`, preserving spawn semantics while
  avoiding Debug stack growth during login restore.
- `ModernServer/src/world/map_actor_monster.hpp:575` is the single
  `MapActor::finalize_monster_death` settlement entry.
- `ModernServer/src/world/map_actor_monster.hpp:595` through
  `ModernServer/src/world/map_actor_monster.hpp:598` mark death state, refresh the
  map object state, and make the settlement idempotent with `death_settled`.
- `ModernServer/src/world/map_actor_monster.hpp:600` through
  `ModernServer/src/world/map_actor_monster.hpp:607` keep slave/no-item monsters
  from dropping items or paying rewards.
- `ModernServer/src/world/map_actor_monster.hpp:612` through
  `ModernServer/src/world/map_actor_monster.hpp:640` resolve reward ownership:
  direct player kills reward that player, player-owned slave kills reward the
  master and advance the slave exp bucket, and wild/no-owner kills do not create a
  player drop owner.
- `ModernServer/src/world/map_actor_monster.hpp:654` through
  `ModernServer/src/world/map_actor_monster.hpp:656` award monster kill exp before
  map quest processing.
- `ModernServer/src/world/map_actor_monster.hpp:668` through
  `ModernServer/src/world/map_actor_monster.hpp:673` trigger `monster_die` map
  quest processing after exp attribution.
- `ModernServer/src/world/map_actor_monster.hpp:708` through
  `ModernServer/src/world/map_actor_monster.hpp:711` assign drop owner protection
  as `now_ms + kLegacyDropOwnerMs`.
- `ModernServer/src/world/map_actor_monster.hpp:756` through
  `ModernServer/src/world/map_actor_monster.hpp:786` scatter gold chunks first,
  then the already-rolled monster drop items.
- `ModernServer/src/world/map_actor_mail.hpp:2907` through
  `ModernServer/src/world/map_actor_mail.hpp:2910` call
  `finalize_monster_death` before queuing the pending `SM_DEATH` packet in the
  player melee kill path.
- `ModernServer/src/world/map_actor.cpp:1748`,
  `ModernServer/src/world/map_actor.cpp:1759`, and
  `ModernServer/src/world/map_actor.cpp:1763` settle already-dead monsters during
  object tick and then convert corpse to ghost when the corpse timer expires.
- `ModernServer/src/world/map_actor_monster.hpp:792` through
  `ModernServer/src/world/map_actor_monster.hpp:814` implement ghost cleanup and
  respawn scheduling after the corpse phase.

## Locked PR-9 Semantics

Death settlement order is:

1. Mark death and refresh map state.
2. Return immediately if `death_settled` is already true.
3. For slave or `no_item` monsters, remove slave ownership, clear hitters, and
   stop before exp/drop.
4. Resolve reward and drop owner from the exp hitter.
5. Award player exp or master exp and slave exp.
6. Trigger `monster_die` map quest processing.
7. Scatter death gold, then spawn-time rolled item drops.
8. Let the caller queue `SM_DEATH` after settlement in the existing combat path.
9. Keep the corpse until the ghost timer expires.
10. On ghost cleanup, remove the map object and schedule respawn for non-slave,
    item-dropping monsters.

The settlement entry is idempotent. Repeated calls to `finalize_monster_death`
after `death_settled` must not duplicate exp, task triggers, drops, traces, or
respawn state.

## Drop Ownership

Death drops inherit `owner_actor_id` from the resolved player owner. Player kills
own their drops; player-owned slave kills assign owner protection to the master.
Pickup by another player before `ownership_expire_ms` is rejected. The strict
boundary remains observable: `now_ms == drop_time + 120000` still rejects, and
`now_ms == drop_time + 120001` allows pickup.

## Validation Coverage

- `mir2_monster_death_drop_smoke` covers exp/task/drop/death packet order and
  death-settlement idempotence.
- `mir2_monster_legacy_drop_scatter_smoke` covers legacy drop scatter ordering
  and spawn-time rolled drops.
- `mir2_monster_drop_owner_protection_smoke` covers owner protection and the
  strict `+120000ms` boundary.
- `mir2_monster_slave_exp_smoke` covers legacy hitter aging and slave exp levels.
- `mir2_monster_slave_lifecycle_smoke` covers slave restore/save, royalty expiry,
  life expiry, no-drop death settlement, follow, relax, and recall.
- `mir2_monster_death_corpse_ghost_smoke` covers corpse-to-ghost cleanup and
  respawn scheduling.

PR-9 removes the owner-protection, slave-exp, and slave-lifecycle tests from
quarantine after replacing Debug `assert` exits with fail-fast checks.

## Confirmed Gaps

- Boss death announcements need a confirmed legacy config/source mapping before
  C++ can add behavior.
- Full party/group experience parity is not proven by PR-9 tests.
- Full Delphi local-message `deliverytime` ordering is still broader than this
  phase; PR-9 only preserves the current combat path where settlement happens
  before pending `SM_DEATH` packets are appended.
- Visibility ordering of item show/hide packets remains PR-10 scope.
