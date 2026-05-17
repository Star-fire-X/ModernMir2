# PR1 Final Report: Delphi Bag Semantics Audit

## Closure Summary

PR1 is closed as a docs-only static Delphi source review. The main risk from the
original design is resolved: legacy item commands use item identity
(`MakeIndex` plus name), not client-visible bag index.

The server still uses TList positions internally. That means C++ fixed-array
slots must not leak as legacy command identity, and later PRs must preserve
identity-based deletion/update packets.

## Frozen Decisions

| Topic | Result |
|---|---|
| Client use/drop/equip identity | `MakeIndex` in `Recog/lparam1`; drop/equip use name body; eat does not require name. |
| Equip slot | `CM_TAKEONITEM` and `CM_TAKEOFFITEM` carry slot in `Param/lparam2`. |
| `SendDelItem` | Sends encoded `TClientItem`; client deletes by `(Name, MakeIndex)`. |
| Server add/delete order | `AddItem` appends to `ItemList`; delete compresses the TList. |
| Empty bag snapshot | `SendBagItems` emits no `SM_BAGITEMS` when the encoded body is empty. |
| Stacking | No server count field; no server-side stack semantics in `TUserItem`. |
| Use item deletion | Client removes item optimistically; success is finalized by `SM_EAT_OK`. |
| Bag drag reorder | Pure bag-to-bag movement is local only. |
| Drop confirmation | Normal and special non-gold item drops have no confirmation dialog; gold prompts for amount. |

## Important Corrections For Later PRs

- Do not implement legacy `CM_EAT`, `CM_DROPITEM`, or `CM_TAKEONITEM` as bag-index commands.
- Do not require `SM_DELITEM` on ordinary use-item success; Delphi client already removed the item locally.
- Do not treat Delphi `AddItem` as first-empty-slot behavior; it appends to a compact TList.
- Do not introduce count stacking for potions, books, or scrolls.

## PR2-PR10 Inputs

| PR | Required Input From PR1 |
|---|---|
| PR2 | Preserve `TUserItem` no-count layout and `TClientItem.MakeIndex` identity. |
| PR3 | Align fixed-array adapter with TList append/delete-compress semantics without changing wire identity. |
| PR4 | Encode/decode `SM_BAGITEMS`, `SM_ADDITEM`, `SM_DELITEM`, and `SM_UPDATEITEM` by `TClientItem` identity; preserve empty-bag no-packet behavior for legacy compatibility. |
| PR5 | Align use-item flow with optimistic client removal, `SM_EAT_OK/FAIL`, and instant HP/MP ordering. |
| PR6 | Align drop flow: client pending drop, server ground add before bag delete, `SM_DROPITEM_SUCCESS/FAIL`. |
| PR7 | Align take-on/take-off flow, especially `SM_TAKEOFF_OK` before `SM_ADDITEM`. |
| PR8 | Apply identity-based item operations to shop, storage, trade, and script paths. |
| PR9 | Validate all external item commands by owning player + MakeIndex + name, not slot. |
| PR10 | Turn static golden traces into executable protocol/client interaction tests. |

## Artifacts

- `delphi_source_evidence.md`
- `boundary_classification.md`
- `protocol_sequence.md`
- `client_inventory_interaction.md`
- `verification_checklist.md`
- `golden_traces/*.json`
