# Legacy Bag Protocol Findings

This note supersedes the ABI and bag protocol assumptions in `bag_system_migration_design.md` for the first migration slice. It is intentionally narrow: PR-1 records Delphi facts only, and PR-2 turns those facts into baseline tests. It does not prescribe the production fix.

## ABI findings

The M2Server and client Delphi projects both build with `$A8` alignment (`Source/M2Server/M2Server.cfg:1`, `Source/Client/mir2.cfg:1`). `TUserItem` is explicitly `packed`, while `TStdItem` and `TClientItem` are regular records in `Source/Common/Grobal2.pas`.

A DCC32 15.0 probe using the current Mir2 field layout and `$A8` produced:

```text
TStdItem=76
TUserItem=40
TClientItem=84
TUserItem.MakeIndex=0
TUserItem.Index=4
TUserItem.Dura=6
TUserItem.DuraMax=8
TUserItem.Desc=10
TUserItem.ColorR=24
TUserItem.ColorG=25
TUserItem.ColorB=26
TUserItem.Prefix=27
TStdItem.StdMode=15
TStdItem.Looks=22
TStdItem.Price=40
TStdItem.HpAdd=56
TClientItem.MakeIndex=76
TClientItem.Dura=80
TClientItem.DuraMax=82
```

At the PR-1/PR-2 audit point, C++ had `LegacyUserItem == 40` but still asserted `LegacyStdItem == 69` and `LegacyClientItem == 77` in `ModernServer/src/protocol/legacy_types.hpp`. The combined PR-3 changes update those item wire types to the Delphi `$A8` targets recorded here.

## TUserItem field contract

`TUserItem` is declared as a packed record at `Source/Common/Grobal2.pas:363`.

| Field | Offset | Size | Notes |
|---|---:|---:|---|
| `MakeIndex` | 0 | 4 | Runtime unique item instance id. |
| `Index` | 4 | 2 | Template item index. |
| `Dura` | 6 | 2 | Current durability or item-specific value. |
| `DuraMax` | 8 | 2 | Maximum durability or item-specific max. |
| `Desc[0..13]` | 10 | 14 | Upgrade, unidentified, and reserved bytes. |
| `ColorR/G/B` | 24/25/26 | 3 | Item color bytes. |
| `Prefix[0..12]` | 27 | 13 | Prefix text bytes. |

There is no count field in `TUserItem`. Normal bag items are distinct instances by `MakeIndex`; any stacking or bundle behavior must be proven from a specific Delphi path before it is modeled in C++.

## Desc field findings

`Source/Common/Grobal2.pas:368` documents `Desc[0..7]` as upgrade bytes and `Desc[10]` as an upgrade/identification marker. `Source/M2Server/itmunit.pas:518` applies those fields in `GetUpgradeStdItem`.

| Field | Finding |
|---|---|
| `Desc[0..7]` | Confirmed upgrade bytes used by `GetUpgradeStdItem`. |
| `Desc[8]` | Confirmed unknown/unidentified attribute marker. It is set in `itmunit.pas` and hidden in `SendAddItem`; equipping clears it in `ObjBase.pas:13278`. |
| `Desc[9]` | No non-comment usage found in this Mir2 source tree. Treat as reserved until proven otherwise. |
| `Desc[10]` | Confirmed upgrade/identify marker used by weapon upgrade and NPC upgrade paths. |
| `Desc[11..13]` | Not confirmed in this Mir2 code path. Do not import EI/Gadget meanings into the Mir2 migration without source evidence. |

## Client-to-server command fields

The old client sends item operations by `MakeIndex + item name`, not by TList bag index. Take-on/take-off also include the equipment slot in `param`. Merchant and storage commands keep the actor id in `recog` and split item `MakeIndex` into `param/tag`.

| Command | Delphi client source | `recog` | `param` | `tag` | Body |
|---|---|---:|---:|---:|---|
| `CM_DROPITEM` | `ClMain.pas:3057` | item `MakeIndex` | 0 | 0 | item name |
| `CM_TAKEONITEM` | `ClMain.pas:3073` | item `MakeIndex` | equipment slot | 0 | item name |
| `CM_TAKEOFFITEM` | `ClMain.pas:3081` | equipped item `MakeIndex` | equipment slot | 0 | item name |
| `CM_EAT` | `ClMain.pas:3089` | item `MakeIndex` | 0 | 0 | item name |
| `CM_DEALADDITEM` | `ClMain.pas:3280` | item `MakeIndex` | 0 | 0 | item name |
| `CM_DEALDELITEM` | `ClMain.pas:3288` | item `MakeIndex` | 0 | 0 | item name |
| `CM_USERSELLITEM` | `ClMain.pas:3147` | merchant actor id | `Loword(MakeIndex)` | `Hiword(MakeIndex)` | item name |
| `CM_USERSTORAGEITEM` | `ClMain.pas:3163` | storage/merchant actor id | `Loword(MakeIndex)` | `Hiword(MakeIndex)` | item name |
| `CM_USERTAKEBACKSTORAGEITEM` | `ClMain.pas:3187` | storage/merchant actor id | `Loword(MakeIndex)` | `Hiword(MakeIndex)` | item name |

The migration risk is therefore not that old clients directly send `bagindex` for these commands. The risk is that server-side operations resolve `MakeIndex + name` into a Delphi `ItemList` position, and that position changes after deletion because `TList.Delete` compacts.

## Server-to-client item messages

`Source/M2Server/ObjBase.pas:10501` builds `TClientItem` from `TStdItem + MakeIndex + Dura + DuraMax` for item messages.

| Message | Delphi source | Body | Header notes |
|---|---|---|---|
| `SM_BAGITEMS` | `ObjBase.pas:10590` | Repeated encoded `TClientItem` entries, slash-delimited | `recog = integer(self)`, `series = ItemList.Count` |
| `SM_ADDITEM` | `ObjBase.pas:10501` | One encoded `TClientItem` | `recog = integer(self)`, `series = 1` |
| `SM_DELITEM` | `ObjBase.pas:10554` | One encoded `TClientItem` | `recog = integer(self)`, `series = 1` |
| `SM_UPDATEITEM` | `ObjBase.pas:10532` | One encoded `TClientItem` | `recog = integer(self)`, `series = 1` |

These messages do not carry a standalone bag index. Legacy deletion identity is the `TClientItem` contents, especially `MakeIndex` and name.

## TList and Add/Del semantics

`TCreature.ItemList` behaves as a Delphi `TList` of `PTUserItem`. `ItemList.Delete(i)` compacts remaining entries.

| Operation | Delphi source | Finding |
|---|---|---|
| `CanAddItem` | `ObjBase.pas:6472` | Only checks `ItemList.Count < MAXBAGITEM`. No weight check here. |
| `AddItem` | `ObjBase.pas:6480` | Appends pointer, calls `WeightChanged`, returns true. It does not send item packets. |
| `DelItem` | `ObjBase.pas:6490` | Matches `MakeIndex` and `CompareText(std name, iname)`, disposes pointer, deletes list entry, then calls `WeightChanged`. |
| `DelItemIndex` | `ObjBase.pas:6509` | Deletes by current TList position and compacts. It does not set `Result := TRUE` and does not call `WeightChanged`. |

A C++ fixed array can remain the persistence shape, but legacy behavior needs either compact-on-delete or an explicit logical TList order. Merely skipping empty array slots in protocol output is not equivalent: `A,B -> remove A -> add C` becomes `B,C` in Delphi but can become `C,B` with first-empty array insertion.

## Core flow findings

| Flow | Delphi behavior to preserve as target |
|---|---|
| Eat/use item | `ServerGetEatItem` finds by `MakeIndex`, copies the item, applies the handler, deletes the list entry on success, then sends `WeightChanged` and `SM_EAT_OK`. It does not use `dura--` as the generic potion count model. |
| Drop item | `UserDropItem` matches `MakeIndex + CompareText(name)`, calls `DropItemDown`, and only deletes from `ItemList` after the map item is created successfully. Failure keeps the bag item. |
| Pickup item | `PickUp` checks ownership and bag availability, deletes the map item, verifies weight before `AddItem`, sends hide/add messages on success, and re-adds to map on weight failure. |
| Take on item | `ServerGetTakeOnItem` resolves `MakeIndex + name` to `bagindex`, writes `UseItems[where]`, calls `DelItemIndex(bagindex)`, optionally adds the replaced item, then sends ability/subability/take-on/feature updates. |
| Take off item | `ServerGetTakeOffItem` validates slot, `MakeIndex`, and name, calls `AddItem`, clears `UseItems[where]`, then sends take-off/add-item/ability/subability/feature updates. |

## PR-2 testing boundary

PR-2 should keep CI green by separating facts that the current C++ already satisfies from Delphi targets that are expected to fail until PR-3:

| Category | PR-2 handling |
|---|---|
| Current-compatible facts | Active smoke: `LegacyUserItem == 40`, `kMaxBagItems == 46`, CM decoding uses `MakeIndex/name/slot`. |
| Pending Delphi targets | Disabled smoke: `LegacyStdItem == 76`, `LegacyClientItem == 84`, `SM_*ITEM*` body/header targets, TList compact target, core operation sequence targets. |

The next implementation PR should fix wire ABI first, then bag logical order, then message ordering. `bagindex` should not be treated as the primary wire-layer risk for the listed commands.
