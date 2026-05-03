# Delphi Client Migration Map

This client continues to use the typed `client_v1` protocol rather than the
Delphi text/EdCode packet format. Legacy `CM_*` and `SM_*` behavior is mapped
into explicit C++ messages so new tests can cover encode/decode and UI state
without preserving the old socket framing.

The canonical machine-checkable map lives in
`ModernClient/src/protocol/delphi_protocol_map.hpp`.

## Current Protocol Decision

- Keep `client_v1` as the modern client/server contract.
- Do not reintroduce Delphi `#seq...!` framing in the ModernClient.
- Add compatibility at the semantic message level: one legacy `Send*` or
  `ClientGet*` entry must map to a typed message, an app handler, or an explicit
  planned/deprecated entry in the map.
- Treat low-level Delphi helpers such as `SendSocket` and `SendClientMessage` as
  transport internals, not gameplay features.

## Locked Counts

- `TFrmMain.Send*`: 56 entries, including low-level `SendSocket` and legacy
  timer `SendTimeTimerTimer`.
- `TFrmMain.ClientGet*`: 39 entries.
- `modern_client_protocol_map_smoke` asserts both counts and validates the
  select-server plus account-update messages.

## Implemented In This Slice

- `SendSelectServer` now maps to `client_v1::SelectServerRequest`.
- `ClientGetSelectServer` now maps to `ServerList` plus the C++ `server_select`
  scene.
- The selection result is explicit: `client_v1::SelectServerResult` confirms the
  selected server and supplies the character gateway address/port plus a
  one-shot `lobby_token`.
- The client no longer jumps directly from `ServerList` to
  `CharacterListRequest` unless autoplay is active.
- After `SelectServerResult`, the client closes the login-gateway socket,
  reconnects to the character phase, sends `ClientHello`, then sends
  `CharacterListRequest{lobby_token}`. Character create/delete refreshes reuse
  the same typed entrypoint instead of sending an empty request.
- Delphi connection stages are now represented in C++ state:
  `cnsLogin -> ConnectionPhase::login`,
  `cnsSelChr -> ConnectionPhase::select_character`,
  `cnsReSelChr -> ConnectionPhase::reselect_character` (reserved for the later
  soft reselect flow), and `cnsPlay -> ConnectionPhase::play`.
- `SendNewAccount` now carries the legacy account profile fields through
  `client_v1::AccountProfile` instead of only account/password/display name.
- `ClientGetNeedUpdateAccount` now maps to `client_v1::NeedUpdateAccount` and
  reopens the login scene in account-update mode with the ID field locked.
- `SendUpdateAccount` now maps to `client_v1::UpdateAccountRequest`; successful
  updates continue to the server-list step.

## Next Protocol Targets

1. Login notice parity:
   `stLoginNotice` and `CM_LOGINNOTICEOK` equivalents before world entry.
2. Gameplay message families:
   action/combat, inventory/equipment, NPC/merchant/storage, trade/group/guild,
   minimap, and bonus allocation.
