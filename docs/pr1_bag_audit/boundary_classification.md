# Updated Boundary Classification

## A. Must Preserve

| ID | Boundary | Frozen PR1 Result | Later PR |
|---|---|---|---|
| A1 | `TUserItem` layout | Packed item instance has no count field and keeps `MakeIndex` as identity. | PR2 |
| A2 | Bag capacity | Server `ItemList.Count < MAXBAGITEM`; no internal empty slots in Delphi. | PR3 |
| A3 | Add order | Server `AddItem` appends to TList and calls `WeightChanged`. | PR3 |
| A4 | Delete semantics | `DelItem` and `DelItemIndex` compress TList after deletion. | PR3 |
| A5 | Client command identity | `CM_EAT`, `CM_DROPITEM`, `CM_TAKEONITEM`, and `CM_TAKEOFFITEM` use `MakeIndex` plus name. | PR3/PR4 |
| A6 | Delete packet identity | `SM_DELITEM` body is `TClientItem`; client deletes by `(Name, MakeIndex)`. | PR4 |
| A7 | Full bag snapshot | Non-empty `SM_BAGITEMS` sends every item in TList order as `TClientItem` records; empty bags emit no `SM_BAGITEMS`. | PR4 |
| A8 | No server stacking | Multiple potions/books/items are multiple `TUserItem` entries. | PR5/PR8 |
| A9 | Use item success | Client removes item before sending `CM_EAT`; server success finalizes with `SM_EAT_OK`. | PR5 |
| A10 | Drop item order | Server creates ground item before deleting the bag entry and sending result. | PR6 |
| A11 | Equip success | Take-on deletes bag entry server-side and sends `SM_TAKEON_OK`; swapped old equipment is sent with `SM_ADDITEM`. | PR7 |
| A12 | Take-off success | Take-off sends `SM_TAKEOFF_OK` before `SM_ADDITEM`. | PR7 |
| A13 | Bag drag reorder | Bag-to-bag drag is local `ItemArr` movement only. | PR4/PR10 |
| A14 | Gold drop prompt | Gold drop asks for amount; normal item drop path sends immediately. | PR6/PR10 |
| A15 | Special item drop | Special non-gold items do not prompt client-side; server-blocked event items fail and restore through `SM_DROPITEM_FAIL`. | PR6/PR10 |

## B. May Be Modernized Behind Same Behavior

| ID | Area | Allowed C++ Shape | Compatibility Requirement |
|---|---|---|---|
| B1 | Internal storage | Fixed array may remain if protocol and snapshot mapping preserve MakeIndex identity and visible order. | Do not expose fixed slot as legacy bag identity. |
| B2 | Client v1 inventory slots | Client v1 may use explicit slots. | Bridge from legacy packets by identity and documented order. |
| B3 | Weight calculation helper | C++ can cache or recompute. | Result must match sum of `StdItem.Weight` over live bag entries. |
| B4 | Transactions | C++ can use rollback helpers. | Public success/failure packet order must match frozen traces. |
| B5 | Golden traces | Static JSON traces are accepted for PR1. | Later executable tests should use these names and message order. |

## C. Explicitly Not Part Of PR1

| ID | Area | Reason |
|---|---|---|
| C1 | Changing `ModernServer` item behavior | PR1 is docs-only. |
| C2 | Refactoring client UI | PR1 only freezes Delphi behavior. |
| C3 | CTest additions | Deferred to PR10 coverage work. |
| C4 | Runtime packet capture | Static source review is sufficient for PR1; runtime capture can be added later as extra evidence. |

## Key Correction To Earlier Design Notes

The legacy client does not send `bagindex` for `CM_EAT`, `CM_DROPITEM`, or
`CM_TAKEONITEM`. It sends `MakeIndex` plus item name, and server helpers find the
item in `ItemList`. `bagindex` remains important because internal TList deletion
compresses order, but it is not the wire identity for these commands.
