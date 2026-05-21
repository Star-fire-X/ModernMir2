# Client Inventory Interaction

## Local State

The legacy client keeps these relevant variables in `ClMain.pas` and `FState.pas`:

| State | Meaning |
|---|---|
| `ItemArr` | Local bag and quick-slot mirror. |
| `ItemMoving` | Whether the user is dragging an item. |
| `MovingItem` | Dragged item plus source marker. |
| `WaitingUseItem` | Item awaiting equip/take-off server confirmation. |
| `EatingItem` | Item awaiting `SM_EAT_OK` or `SM_EAT_FAIL`. |

## Bag Layout

- Quick slots `0..5` prefer `StdMode <= 3` items in `AddItemBag`.
- Visible bag slots begin at index `6` in `DItemGrid`.
- Full `SM_BAGITEMS` refresh decodes server items, then may restore a saved
  local `.itm` arrangement when the same item identities are present.
- `ArrangeItemBag` removes duplicate identities and moves overflow slots into
  visible bag slots.

## Bag-To-Bag Drag

Pure bag movement is local:

- `DBelt1Click` swaps quick-slot/bag-held items locally.
- `DItemGridGridSelect` swaps or places `MovingItem` in `ItemArr`.
- No bag reorder command is sent to the server.
- The next full bag snapshot can be reconciled by identity and local saved layout.

## Use Item

- The client removes the item locally before sending `CM_EAT`.
- On `SM_EAT_OK`, it clears `EatingItem` and arranges the bag.
- On `SM_EAT_FAIL`, it restores `EatingItem` through `AddItemBag`.
- Skill books can show a training confirmation dialog before `CM_EAT`; normal
  consumables do not.

## Equip And Take Off

- Bag-to-equipment drag stores the item in `WaitingUseItem` and sends
  `CM_TAKEONITEM`.
- `SM_TAKEON_OK` writes `WaitingUseItem` into the equipment slot; FAIL restores
  it to the bag.
- Equipment-to-bag drag stores the item in `WaitingUseItem` and sends
  `CM_TAKEOFFITEM`.
- `SM_TAKEOFF_OK` clears the waiting item; the server then sends `SM_ADDITEM`
  for the returned bag item. FAIL restores the item to equipment.

## Drop

- Ordinary item drop from `DropMovingItem` sends `CM_DROPITEM` immediately and
  keeps a pending local dropped-item copy.
- `SM_DROPITEM_FAIL` restores that pending item through `ClientGetDropItemFail`.
- Special non-gold items use the same client path without a confirmation dialog;
  server-side `TAIWANEVENTITEM` rejection returns `SM_DROPITEM_FAIL`.
- Gold drop is different: background click opens a dialog to ask for amount,
  then sends `CM_DROPGOLD`.

## PR2+ Consequences

- C++ should not treat legacy bag slot as command identity for use/drop/equip.
- C++ may preserve fixed internal slots, but the legacy protocol adapter must
  keep identity-based behavior.
- Client v1 can use explicit slots internally, but the legacy bridge should map
  legacy messages by MakeIndex and name.
