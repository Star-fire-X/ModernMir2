# PR1 Verification Checklist

Status values:

- `confirmed`: closed by source evidence in this PR.
- `not_applicable`: not needed for PR1 because source review proves the behavior.
- `deferred_with_reason`: assigned to a later PR with an explicit reason.

| Original PR1 Item | Status | Result |
|---|---|---|
| Confirm `CM_EAT` / `CM_DROPITEM` / `CM_TAKEONITEM` `svindex` meaning. | confirmed | It is `MakeIndex` in `DefaultMessage.Recog/lparam1`. Drop/take-on also use item name in body; `CM_EAT` name body is optional for server behavior. `CM_TAKEONITEM` also carries equip slot in `Param/lparam2`. |
| Confirm `SendDelItem` identity. | confirmed | `SendDelItem` sends a complete `TClientItem`; client deletes by `(S.Name, MakeIndex)`. |
| Confirm Delphi quantity stacking. | confirmed | `TUserItem` has no count field; each item instance is independent. |
| Confirm whether client bag drag sends server request. | confirmed | Pure bag-to-bag drag only mutates local `ItemArr`; no reorder CM packet is sent. |
| Confirm drop confirmation window. | confirmed | Ordinary bag item drop sends immediately; gold drop prompts for amount; special non-gold items do not prompt client-side, and blocked event items fail server-side with `SM_DROPITEM_FAIL`. |
| Output updated boundary classification. | confirmed | See `boundary_classification.md`. |
| Output golden trace expected sequences. | confirmed | See `golden_traces/*.json`; all are static source-derived traces. |
| Clear open source-review checklist. | confirmed | No open PR1 source-review item remains. Runtime packet capture is optional follow-up, not a PR1 blocker. |

## Deferred With Reason

| Item | Status | Reason | Owner |
|---|---|---|---|
| Convert static traces into executable CTest golden diff. | deferred_with_reason | PR1 is docs-only; executable coverage belongs to PR10. | PR10 |
| Runtime packet capture from a running Delphi server/client. | deferred_with_reason | Source evidence is sufficient to freeze PR1 behavior; runtime capture can add confidence later. | PR10 |
