# Frozen Protocol Sequences

All sequences are derived from static Delphi source review. Direction labels use
`C->S` and `S->C`.

## Inventory Packet Format

| Message | Direction | Ident | Payload | Identity |
|---|---:|---:|---|---|
| `CM_DROPITEM` | C->S | 1000 | `DefaultMessage.Recog=MakeIndex`; body encoded item name. | MakeIndex + name |
| `CM_TAKEONITEM` | C->S | 1003 | `Recog=MakeIndex`; `Param=equip_slot`; body encoded item name. | MakeIndex + name + slot |
| `CM_TAKEOFFITEM` | C->S | 1004 | `Recog=MakeIndex`; `Param=equip_slot`; body encoded item name. | MakeIndex + name + slot |
| `CM_EAT` | C->S | 1006 | `Recog=MakeIndex`; body may contain encoded item name, but server use path ignores name. | MakeIndex |
| `SM_ADDITEM` | S->C | 200 | One encoded `TClientItem`. | `TClientItem.MakeIndex` + name |
| `SM_BAGITEMS` | S->C | 201 | Non-empty bag only: `/`-separated encoded `TClientItem` records; `Series=ItemList.Count`. Empty bag sends no `SM_BAGITEMS`. | record identity |
| `SM_DELITEM` | S->C | 202 | One encoded `TClientItem`. | `TClientItem.MakeIndex` + name |
| `SM_UPDATEITEM` | S->C | 203 | One encoded `TClientItem`. | `TClientItem.MakeIndex` + name |

## Full Bag Snapshot

Non-empty path:

1. `C->S CM_QUERYBAGITEMS`.
2. Server `SendBagItems` iterates `ItemList[0..Count-1]`.
3. `S->C SM_BAGITEMS(Series=ItemList.Count)` with encoded `TClientItem/` records.
4. Client clears `ItemArr`, decodes every record, and calls `AddItemBag`.
5. Client may restore the local `.itm` layout if the saved item set matches.
6. Client calls `ArrangeItemBag` and marks bag loaded.

Empty path:

1. `C->S CM_QUERYBAGITEMS`.
2. Server `SendBagItems` leaves the encoded body empty.
3. Because Delphi guards the send with `if data <> ''`, no `SM_BAGITEMS`
   packet is emitted for an empty bag.

Compatibility note: server snapshot order is TList order, but client visible
slots are an identity-based local layout.

## Add Item

1. Server allocates or receives a `TUserItem`.
2. `AddItem` checks `ItemList.Count < MAXBAGITEM`.
3. `AddItem` appends the pointer to `ItemList`.
4. `WeightChanged` emits `RM_WEIGHTCHANGED`.
5. Caller sends `SM_ADDITEM` with one `TClientItem` when the operation should be visible.
6. Client `ClientGetAddItem` decodes the item and calls `AddItemBag`.

## Delete Item

1. Server finds item by `MakeIndex` and name, or by internal TList index in helper-only paths.
2. Server disposes the pointer and calls `ItemList.Delete`, compressing TList order.
3. If visible to the client, caller sends `SM_DELITEM` with the removed `TClientItem`.
4. Client `ClientGetDelItem` decodes the payload and removes by `(Name, MakeIndex)`.

## Use Consumable Or Book

Client optimistic path:

1. Client selects a bag item and copies it into `EatingItem`.
2. Client clears the local `ItemArr` slot before sending.
3. `C->S CM_EAT(Recog=MakeIndex, body=optional name)`.

Server success path:

1. `ServerGetEatItem` scans `ItemList` for `MakeIndex`.
2. Matching item is applied by `EatItem`, `ReadBook`, or unbind logic.
3. Instant HP/MP items emit `RM_HEALTHSPELLCHANGED` during `EatItem`.
4. Server deletes the `ItemList` entry and compresses order.
5. `WeightChanged` emits `RM_WEIGHTCHANGED`.
6. Server sends `SM_EAT_OK`.
7. Client clears `EatingItem` and arranges the bag.

Server failure path:

1. Server sends `SM_EAT_FAIL`.
2. Client restores `EatingItem` with `AddItemBag`.

Frozen correction: ordinary Delphi `CM_EAT` success does not require
`SM_DELITEM`; the client already removed the item locally.

## Drop Item

Client path:

1. Client drag state calls `DropMovingItem`.
2. `C->S CM_DROPITEM(Recog=MakeIndex, body=name)`.
3. Client adds the item to local dropped-item pending list.

Server success path:

1. `UserDropItem` scans `ItemList` by `MakeIndex` and name.
2. Server calls `DropItemDown` to create the ground item.
3. After ground add succeeds, server disposes and deletes the bag item.
4. `WeightChanged` emits weight refresh.
5. Server sends `SM_DROPITEM_SUCCESS`.

Server failure path:

1. Server sends `SM_DROPITEM_FAIL`.
2. Client `ClientGetDropItemFail` restores the pending dropped item to the bag.

Special item path:

1. Client uses the same no-confirm `DropMovingItem` path for non-gold items.
2. Server rejects `TAIWANEVENTITEM` in `UserDropItem`.
3. Client receives `SM_DROPITEM_FAIL` and restores the pending item.

## Take On Item

Client path:

1. Client drags a bag item onto an equipment slot.
2. Client stores it in `WaitingUseItem`, clears local moving state, and sends `CM_TAKEONITEM`.

Server success path:

1. `ServerGetTakeOnItem` finds bag item by `MakeIndex` and name.
2. Server validates target slot and requirements.
3. If target equipment slot contains an item, server copies it to a new `PTUserItem`.
4. Server writes new item to `UseItems[where]`.
5. Server deletes original bag entry by internal `bagindex`.
6. If an old equipment item existed, server appends it to bag and sends `SM_ADDITEM`.
7. Server recalculates ability and sends ability/subability updates.
8. Server sends `SM_TAKEON_OK`.
9. Server broadcasts feature change.

Server failure path:

1. Server sends `SM_TAKEON_FAIL` with failure code.
2. Client restores `WaitingUseItem` to the bag.

## Take Off Item

Client path:

1. Client drags an equipment item into the bag.
2. Client stores it in `WaitingUseItem` and sends `CM_TAKEOFFITEM`.

Server success path:

1. Server validates slot, `MakeIndex`, name, and take-off restrictions.
2. Server appends the equipment item to `ItemList` with `AddItem`.
3. Server clears `UseItems[where].Index`.
4. Server sends `SM_TAKEOFF_OK`.
5. Server sends `SM_ADDITEM` for the item now in the bag.
6. Server recalculates ability and broadcasts feature change.

Server failure path:

1. Server sends `SM_TAKEOFF_FAIL`.
2. Client restores `WaitingUseItem` to its equipment slot.

## Bag-To-Bag Drag

1. Client selects an item from `ItemArr`.
2. Client swaps or places it into another `ItemArr` slot locally.
3. Client calls `ArrangeItemBag`.
4. No `CM_*` packet is sent for pure bag-to-bag reorder.
