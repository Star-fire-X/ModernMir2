# PR1 Bag Audit: Delphi Inventory Semantics

Status: closed static review, docs-only.

This directory is the PR1 artifact for the legacy bag migration. It freezes
Delphi inventory behavior that later PRs must consume without reinterpreting
wire fields, item identity, or client-side drag semantics.

## Reading Order

1. `pr1_final_report.md` - one-page closure report and PR2-PR10 inputs.
2. `verification_checklist.md` - original PR1 acceptance criteria with final status.
3. `delphi_source_evidence.md` - source-backed evidence and line references.
4. `protocol_sequence.md` - frozen CM/SM packet and message-order sequences.
5. `client_inventory_interaction.md` - client drag, local reorder, and drop behavior.
6. `boundary_classification.md` - updated A/B/C boundaries.
7. `golden_traces/*.json` - static expected traces derived from Delphi source.

## Scope

PR1 is documentation-only. It does not change C++ production code, CMake files,
tests, protocol constants, or client UI code.

The combat/skill audit files under `docs/pr1_delphi_audit/` are outside this
bag PR. They should not be staged with this PR.

## Final Decisions

| Decision | Frozen Result |
|---|---|
| `CM_EAT` item identifier | `Recog/lparam1` carries `TClientItem.MakeIndex`; body may carry item name, but `ServerGetEatItem` does not require it. |
| `CM_DROPITEM` item identifier | `Recog/lparam1` carries `TClientItem.MakeIndex`; body carries item name. |
| `CM_TAKEONITEM` item identifier | `Recog/lparam1` carries `TClientItem.MakeIndex`; `Param/lparam2` carries equip slot; body carries item name. |
| `CM_TAKEOFFITEM` item identifier | `Recog/lparam1` carries `TClientItem.MakeIndex`; `Param/lparam2` carries equip slot; body carries item name. |
| `SendDelItem`定位 | Sends a complete `TClientItem`; client removes by `(S.Name, MakeIndex)`, not by bag index. |
| Server bag container | `ItemList: TList`; `AddItem` appends, `Delete` compresses. |
| Empty bag snapshot | `SendBagItems` emits no `SM_BAGITEMS` when the encoded body is empty. |
| Count/stacking | No server-side count field in `TUserItem`; each item instance is independent. |
| Client drag reorder | Local `ItemArr` reorder only; no bag-sort CM packet is sent. |
| Drop confirmation | Normal and special non-gold item drops send `CM_DROPITEM` directly; gold drop prompts for amount. |

## Notes For Later PRs

- `bagindex` exists only in internal helpers such as `DelItemIndex`; legacy item
  commands from the client use `MakeIndex`.
- The Delphi client often removes bag/equipment items optimistically before the
  server replies. `SM_EAT_OK`, `SM_TAKEON_OK`, and `SM_TAKEOFF_OK` finalize local
  state; the corresponding FAIL messages restore cached local items.
- Do not implement PR2+ behavior from the older speculative sections of
  `bag_system_migration_design.md` when this audit contradicts them.
