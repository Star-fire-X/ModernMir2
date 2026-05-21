# Delphi Source Evidence

This file records the evidence used to close PR1. Line numbers refer to the
checked-in Delphi source under `Source/`.

## Data Layout And Constants

| Topic | Evidence | Conclusion |
|---|---|---|
| Item instance | `Source/Common/Grobal2.pas:363` defines `TUserItem = packed record` with `MakeIndex`, `Index`, `Dura`, `DuraMax`, `Desc[0..13]`, color, and prefix fields. | There is no count field. Every inventory item is one independent instance. |
| Client item | `Source/Common/Grobal2.pas:451` defines `TClientItem = record` as `TStdItem + MakeIndex + Dura + DuraMax`. | All SM inventory updates can carry identity with the item payload itself. |
| Bag message ids | `Source/Common/Grobal2.pas:879-882` defines `SM_ADDITEM=200`, `SM_BAGITEMS=201`, `SM_DELITEM=202`, `SM_UPDATEITEM=203`. | C++ constants must keep these numeric values. |
| Client commands | `Source/Common/Grobal2.pas:1105-1111` defines `CM_DROPITEM=1000`, `CM_TAKEONITEM=1003`, `CM_TAKEOFFITEM=1004`, `CM_EAT=1006`. | These are the legacy wire commands for bag item operations. |

## Server ItemList Semantics

| Function | Evidence | Frozen Behavior |
|---|---|---|
| `CanAddItem` | `Source/M2Server/ObjBase.pas:6472` checks only `ItemList.Count < MAXBAGITEM`. | Capacity check only; weight is not checked in this helper. |
| `AddItem` | `Source/M2Server/ObjBase.pas:6480` appends with `ItemList.Add(pu)`, calls `WeightChanged`, returns true. | Add order is TList append. There are no internal empty slots. |
| `DelItem` | `Source/M2Server/ObjBase.pas:6490` scans `ItemList`, matches `MakeIndex` and item name, disposes pointer, then `ItemList.Delete(i)`. | Delete by identity compresses the TList. |
| `DelItemIndex` | `Source/M2Server/ObjBase.pas:6509` deletes by positional `bagindex` and compresses. | `bagindex` is an internal TList position, not the legacy client item identifier. |
| `CalcBagWeight` | `Source/M2Server/ObjBase.pas:4058` sums `StdItem.Weight` for every entry in `ItemList`. | Bag weight is item-template weight summed over all item instances. |
| `WeightChanged` | `Source/M2Server/ObjBase.pas:6397` sets `WAbil.Weight := CalcBagWeight` and emits `RM_WEIGHTCHANGED`. | Weight updates follow successful item mutations. |

## Server Packet Builders

| Packet | Evidence | Frozen Behavior |
|---|---|---|
| `SendAddItem` | `Source/M2Server/ObjBase.pas:10501` builds `TClientItem` and sends `SM_ADDITEM` with `Series=1`. | Body is one encoded `TClientItem`. |
| `SendUpdateItem` | `Source/M2Server/ObjBase.pas:10532` builds `TClientItem` and sends `SM_UPDATEITEM` with `Series=1`. | Body is one encoded `TClientItem`. |
| `SendDelItem` | `Source/M2Server/ObjBase.pas:10554` builds `TClientItem`, copies `ui.MakeIndex`, and sends `SM_DELITEM` with `Series=1`. | Delete packet identifies the item by encoded `TClientItem.MakeIndex` and name. No bag index is sent. |
| `SendBagItems` | `Source/M2Server/ObjBase.pas:10590` iterates `ItemList[0..Count-1]`, appends encoded `TClientItem` records separated by `/`, and sends `SM_BAGITEMS` with `Series=ItemList.Count` only when `data <> ''` at `Source/M2Server/ObjBase.pas:10616`. | Full snapshot order is current TList order for non-empty bags; empty bags emit no `SM_BAGITEMS`. |

## Server Command Dispatch

| Command | Evidence | Frozen Behavior |
|---|---|---|
| `CM_DROPITEM` | `Source/M2Server/ObjBase.pas:12139` calls `UserDropItem(msg.Description, msg.lparam1)`. | `lparam1` is the item MakeIndex, body is item name. |
| `CM_TAKEONITEM` | `Source/M2Server/ObjBase.pas:12165` calls `ServerGetTakeOnItem(msg.lparam2, msg.lparam1, msg.Description)`. | `lparam1` is MakeIndex, `lparam2` is equip slot, body is item name. |
| `CM_TAKEOFFITEM` | `Source/M2Server/ObjBase.pas:12172` calls `ServerGetTakeOffItem(msg.lparam2, msg.lparam1, msg.Description)`. | Same identity format as take-on. |
| `CM_EAT` | `Source/M2Server/ObjBase.pas:12179` calls `ServerGetEatItem(msg.lparam1, msg.Description)`. | `lparam1` is MakeIndex. The body may contain a name, but the server use path does not require it. |

## Server Operation Details

| Operation | Evidence | Frozen Behavior |
|---|---|---|
| Drop item | `Source/M2Server/ObjBase.pas:6787` scans by `MakeIndex` and name, rejects `TAIWANEVENTITEM` at `Source/M2Server/ObjBase.pas:6801`, calls `DropItemDown`, deletes with `ItemList.Delete(i)`, then `WeightChanged`. | Successful drop creates ground item before removing from bag; event/special items fail server-side without a client confirmation dialog. |
| Take on | `Source/M2Server/ObjBase.pas:13215` finds bag item by `MakeIndex` and name, writes `UseItems[where]`, deletes the bag entry with `DelItemIndex(bagindex)`, optionally adds old equipped item to bag, sends `SM_TAKEON_OK`. | Client item was already removed locally; server does not need a delete packet for the newly equipped bag item. |
| Take off | `Source/M2Server/ObjBase.pas:13304` validates equipped item by slot and `MakeIndex`, appends it to bag with `AddItem`, clears `UseItems[where]`, sends `SM_TAKEOFF_OK`, then `SendAddItem`. | Success packet precedes the add-item packet. |
| Eat item | `Source/M2Server/ObjBase.pas:13369` finds an item by `MakeIndex`; item-name comparison is commented out in this function. Successful cases delete the item from `ItemList`, call `WeightChanged`, and send `SM_EAT_OK`. | The client has already removed the item locally; ordinary success does not send `SM_DELITEM`. |
| Instant HP/MP item | `Source/M2Server/ObjBase.pas:2960` `IncHealthSpell` emits `RM_HEALTHSPELLCHANGED`; `Source/M2Server/ObjBase.pas:6961` calls `EatItem`, and `Source/M2Server/ObjBase.pas:6976` calls `IncHealthSpell` for `FASTFILL_ITEM`. | For instant-fill consumables, HP/MP update is emitted during `EatItem`, before deletion and `SM_EAT_OK`. |

## Client Send Paths

| Client function | Evidence | Frozen Wire Shape |
|---|---|---|
| `SendDropItem` | `Source/Client/ClMain.pas:2976` sends `MakeDefaultMsg(CM_DROPITEM, itemserverindex, 0, 0, 0) + EncodeString(name)`. | `Recog` is MakeIndex; body is item name. |
| `SendTakeOnItem` | `Source/Client/ClMain.pas:2992` sends `MakeDefaultMsg(CM_TAKEONITEM, itmindex, where, 0, 0) + EncodeString(itmname)`. | `Recog` is MakeIndex; `Param` is equip slot; body is name. |
| `SendTakeOffItem` | `Source/Client/ClMain.pas:3000` uses the same identity shape for equipped items. | `Recog` is MakeIndex; `Param` is equip slot; body is name. |
| `SendEat` | `Source/Client/ClMain.pas:3008` sends `MakeDefaultMsg(CM_EAT, itmindex, 0, 0, 0) + EncodeString(itmname)`. `Source/Client/ClMain.pas:1941` can call it after clearing the local item name. | `Recog` is MakeIndex; body name is optional for server behavior. |

## Client Receive Paths

| Packet | Evidence | Client Behavior |
|---|---|---|
| `SM_ADDITEM` | `Source/Client/ClMain.pas:5404` decodes one `TClientItem` and calls `AddItemBag`. | Add by first local slot group available, not by server-provided slot. |
| `SM_UPDATEITEM` | `Source/Client/ClMain.pas:5415` decodes one `TClientItem`, updates bag and any matching equipped item by `(Name, MakeIndex)`. | Update by identity. |
| `SM_DELITEM` | `Source/Client/ClMain.pas:5432` decodes one `TClientItem`, calls `DelItemBag(cu.S.Name, cu.MakeIndex)`, and clears matching equipped item. | Delete by `(Name, MakeIndex)`. |
| `SM_DELITEMS` | `Source/Client/ClMain.pas:5449` decodes name/index pairs and removes by identity. | Batch delete also uses identity, not slot. |
| `SM_BAGITEMS` | `Source/Client/ClMain.pas:5473` clears `ItemArr`, decodes each `TClientItem`, calls `AddItemBag`, optionally restores local saved layout if the item set matches, then `ArrangeItemBag`. | Snapshot payload order is preserved as item identity set, but local client layout may be restored from `.itm`. |

## Client Local Bag Helpers

| Helper | Evidence | Client Behavior |
|---|---|---|
| `AddItemBag` | `Source/Client/ClFunc.pas:148` rejects duplicate `(MakeIndex, Name)`, puts `StdMode<=3` items in quick slots `0..5`, otherwise slots `6..MAXBAGITEMCL-1`. | The visual bag layout is client-side and not a direct server TList index. |
| `DelItemBag` | `Source/Client/ClFunc.pas:193` scans from high slot to low slot and clears the matching `(Name, MakeIndex)`. | Identity-based deletion can find locally moved items. |
| `ArrangeItemBag` | `Source/Client/ClFunc.pas:208` removes duplicates, cancels `MovingItem` if its item reappears, and moves overflow slots `46..` into visible slots `6..45`. | Client has local layout normalization independent of the server container. |

## Client Drag And Confirmation

| Interaction | Evidence | Frozen Behavior |
|---|---|---|
| Cancel drag | `Source/Client/FState.pas:1669` restores the dragged item to equipment, bag, deal list, or first bag slot depending on `MovingItem.Index`. | Drag state is local and recoverable. |
| Drop item | `Source/Client/FState.pas:1700` calls `FrmMain.SendDropItem` and `AddDropItem` immediately. | Ordinary bag item drop does not show an OK/Cancel confirm dialog in this path. |
| Gold drop | `Source/Client/FState.pas:1724` opens `DMessageDlg` to ask for the gold amount before `SendDropGold`. | Gold uses a prompt; normal bag item drop does not. |
| Special item drop | `Source/Client/FState.pas:1700` has no item-type branch before `SendDropItem`; `Source/M2Server/ObjBase.pas:6801` rejects `TAIWANEVENTITEM` server-side. | Special non-gold items do not prompt on the client; blocked items are restored through drop failure. |
| Equip drag | `Source/Client/FState.pas:2768` puts a bag item into `WaitingUseItem` and sends `SendTakeOnItem`. | Equip is optimistic local state plus server OK/FAIL. |
| Belt drag | `Source/Client/FState.pas:3293` swaps quick-slot items locally. | Local reorder does not send a server bag-sort command. |
| Bag grid drag | `Source/Client/FState.pas:3768` swaps or places `ItemArr` locally; only moving equipment back to bag sends `SendTakeOffItem`. | Bag-to-bag movement is local only. |
