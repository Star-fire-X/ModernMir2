# Trade Phase 1 Delphi Trace

This document records the Delphi player-to-player deal semantics that must be
preserved before the C++ trade implementation is changed further. The goal is
compatibility with the original Mir2 behavior, not a redesigned MMO trade
system.

## Scope

- Player-to-player face-to-face deal flow.
- Legacy `CM_DEAL*` request and `SM_DEAL*` response ids.
- Item, gold, cancel, confirmation, and commit ordering.
- C++ migration gaps in the current `MapActor` and client_v1 bridge.

NPC merchant buy, sell, repair, and storage flows are separate systems. They
share inventory and gold primitives but do not use `DealList`, `DealGold`, or
`BoDealSelect`.

## Delphi Source Evidence

Primary files:

- `Source/Common/Grobal2.pas`
  - `SM_DEALMENU = 673`
  - `SM_DEALTRY_FAIL = 674`
  - `SM_DEALADDITEM_OK = 675`
  - `SM_DEALADDITEM_FAIL = 676`
  - `SM_DEALDELITEM_OK = 677`
  - `SM_DEALDELITEM_FAIL = 678`
  - `SM_DEALCANCEL = 681`
  - `SM_DEALREMOTEADDITEM = 682`
  - `SM_DEALREMOTEDELITEM = 683`
  - `SM_DEALCHGGOLD_OK = 684`
  - `SM_DEALCHGGOLD_FAIL = 685`
  - `SM_DEALREMOTECHGGOLD = 686`
  - `SM_DEALSUCCESS = 687`
  - `CM_DEALTRY = 1025`
  - `CM_DEALADDITEM = 1026`
  - `CM_DEALDELITEM = 1027`
  - `CM_DEALCANCEL = 1028`
  - `CM_DEALCHGGOLD = 1029`
  - `CM_DEALEND = 1030`
- `Source/M2Server/ObjBase.pas`
  - `MAXDEALITEM = 10`
  - `BoExchangeAvailable`
  - `BoDealing`
  - `DealItemChangeTime`
  - `DealCret`
  - `DealList`
  - `DealGold`
  - `BoDealSelect`
  - `ServerGetDealTry`
  - `ResetDeal`
  - `StartDeal`
  - `BrokeDeal`
  - `AddDealItem`
  - `DelDealItem`
  - `ServerGetDealAddItem`
  - `ServerGetDealDelItem`
  - `ServerGetDealChangeGold`
  - `ServerGetDealEnd`
- `Source/Client/ClMain.pas`
  - `SendDealTry`
  - `SendCancelDeal`
  - `SendAddDealItem`
  - `SendDelDealItem`
  - `SendChangeDealGold`
  - `SendDealEnd`
  - client handlers for `SM_DEAL*`

`Source/Mir200/ObjBase.pas` contains the same trade implementation and can be
used as a comparison copy when auditing local edits.

## Delphi State Fields

`BoDealing` marks that the player is currently in a deal.

`DealCret` points to the peer creature. Valid trade operations require this to
still be present and facing the player where the source checks it.

`DealList` stores `PTUserItem` pointers temporarily moved out of `ItemList`.
This is a temporary removal from the bag, not a separate reservation flag.

`DealGold` stores gold already removed from `Gold`. Changing the gold amount
first credits back the old `DealGold` through the expression
`Gold + DealGold >= dgold`, then writes the new reserved amount.

`BoDealSelect` means the player pressed the deal confirmation button. The
original protocol has one confirmation request, `CM_DEALEND`; there is no
separate lock and final-confirm request in the legacy protocol.

`DealItemChangeTime` is updated when either side changes items or gold.
`ServerGetDealEnd` cancels the deal when either side changed contents within
the last 1000 ms.

## Legacy Protocol Table

Requests:

| Request | Id | Fields |
| --- | ---: | --- |
| `CM_DEALTRY` | 1025 | body is `EncodeString(who)` from the client, but the server uses `GetFrontCret` as the authority. |
| `CM_DEALADDITEM` | 1026 | `recog=MakeIndex`, body is item name. |
| `CM_DEALDELITEM` | 1027 | `recog=MakeIndex`, body is item name. |
| `CM_DEALCANCEL` | 1028 | no body. |
| `CM_DEALCHGGOLD` | 1029 | `recog=gold`. |
| `CM_DEALEND` | 1030 | no body. |

Responses:

| Response | Id | Fields |
| --- | ---: | --- |
| `SM_DEALMENU` | 673 | body is peer `UserName`. |
| `SM_DEALTRY_FAIL` | 674 | empty body. |
| `SM_DEALADDITEM_OK` | 675 | empty body. |
| `SM_DEALADDITEM_FAIL` | 676 | empty body. |
| `SM_DEALDELITEM_OK` | 677 | empty body. |
| `SM_DEALDELITEM_FAIL` | 678 | empty body. |
| `SM_DEALCANCEL` | 681 | empty body. |
| `SM_DEALREMOTEADDITEM` | 682 | `recog=integer(self)`, `series=1`, body is encoded `TClientItem`. |
| `SM_DEALREMOTEDELITEM` | 683 | `recog=integer(self)`, `series=1`, body is encoded `TClientItem`. |
| `SM_DEALCHGGOLD_OK` | 684 | `recog=DealGold`, `param=Loword(Gold)`, `tag=Hiword(Gold)`. |
| `SM_DEALCHGGOLD_FAIL` | 685 | same field shape as ok, using current values. |
| `SM_DEALREMOTECHGGOLD` | 686 | `recog=DealGold`. |
| `SM_DEALSUCCESS` | 687 | empty body. |

## Start Semantics

`ServerGetDealTry` exits if the requester is already dealing.

The authoritative target is the creature in front of the requester:

- requester front creature exists;
- target is not requester;
- target front creature is requester;
- target is not already dealing;
- target race is `RC_USERHUMAN`;
- target `BoExchangeAvailable` is true.

When all checks pass, both sides receive system messages, then:

1. requester `StartDeal(target)`;
2. target `StartDeal(requester)`.

`StartDeal` sets `BoDealing`, sets `DealCret`, calls `ResetDeal`, sends
`SM_DEALMENU`, and records `DealItemChangeTime`.

There is no separate accept or reject packet in the Delphi deal start path.

## Item Offer Semantics

`ServerGetDealAddItem`:

- requires `DealCret <> nil`;
- trims the item name at the first space with `GetValidStr3`;
- refuses changes when the peer has already selected (`not DealCret.BoDealSelect`);
- scans only `ItemList`, not equipment;
- rejects `TAIWANEVENTITEM`;
- matches both `MakeIndex` and standard item name;
- caps the offer at `MAXDEALITEM`;
- moves the item pointer from `ItemList` to `DealList`;
- sends `SM_DEALADDITEM_OK` to self;
- sends `SM_DEALREMOTEADDITEM` with encoded `TClientItem` to the peer;
- updates both sides' `DealItemChangeTime`.

`ServerGetDealDelItem` performs the reverse move from `DealList` back to
`ItemList`, then sends `SM_DEALDELITEM_OK` to self and
`SM_DEALREMOTEDELITEM` to the peer.

The original client moves items optimistically in the UI before the server
reply. The server still owns the authoritative `ItemList` to `DealList` move.

## Gold Offer Semantics

`ServerGetDealChangeGold`:

- rejects negative gold with `SM_DEALCHGGOLD_FAIL`;
- requires `GetFrontCret = DealCret`;
- refuses changes when the peer has already selected;
- checks `Gold + DealGold >= dgold`;
- writes `Gold := (Gold + DealGold) - dgold`;
- writes `DealGold := dgold`;
- sends `SM_DEALCHGGOLD_OK` to self with updated bag gold split into low/high
  words;
- sends `SM_DEALREMOTECHGGOLD` to the peer;
- updates both sides' `DealItemChangeTime`.

Gold is therefore removed before final commit. Cancel returns `DealGold` in
`ResetDeal`.

## Confirmation and Commit Semantics

`ServerGetDealEnd` immediately sets `BoDealSelect := TRUE`.

If either side changed contents within the last 1000 ms, the server sends a
system message and calls `BrokeDeal`.

If the peer has not selected yet, the server sends system messages telling the
players that another confirmation is required. No trade contents move.

If both sides selected:

1. validate the requester has enough bag slots for peer `DealList`;
2. validate requester gold capacity for peer `DealGold`;
3. validate peer has enough bag slots for requester `DealList`;
4. validate peer gold capacity for requester `DealGold`;
5. move requester `DealList` items to peer with `AddItem` and `SendAddItem`;
6. add requester `DealGold` to peer and call `GoldChanged`;
7. move peer `DealList` items to requester with `AddItem` and `SendAddItem`;
8. add peer `DealGold` to requester and call `GoldChanged`;
9. send peer `SM_DEALSUCCESS`;
10. clear peer trade state;
11. send requester `SM_DEALSUCCESS`;
12. clear requester trade state.

If validation fails, `BrokeDeal` cancels and returns each side's own reserved
items and gold.

## Cancel and Boundary Semantics

`BrokeDeal`:

1. sets `BoDealing := FALSE`;
2. sends `SM_DEALCANCEL` to self;
3. clears the peer's `DealCret` and recursively breaks the peer deal;
4. clears local `DealCret`;
5. calls `ResetDeal`;
6. sends a system message;
7. updates `DealItemChangeTime`.

`ResetDeal` moves all local `DealList` entries back into `ItemList`, clears
`DealList`, adds `DealGold` back to `Gold`, clears `DealGold`, and clears
`BoDealSelect`.

Confirmed cancellation triggers in the audited source:

- explicit `CM_DEALCANCEL`;
- save/logout path through `ReadySave`;
- player operate loop when `GetFrontCret <> DealCret`, `DealCret = self`, or
  `DealCret = nil`;
- pickup is blocked while `BoDealing`;
- take-off equipment is blocked while `BoDealing`;
- NPC click is blocked while `BoDealing`;
- merchant buy path is blocked while `BoDealing`.

Pending source checks:

- exact death path that calls or implies `BrokeDeal`;
- attack and struck paths, to determine whether combat actively cancels or only
  the next operate/front-facing check cancels;
- map transfer path outside the audited snippets;
- RunGate packet ordering around multiple decoded `CM_DEAL*` messages.

## C++ Current-State Gaps

Current C++ trade locations:

- `ModernServer/src/world/map_actor.hpp`
  - `TradeOffer`
  - `TradeSession`
  - `trade_sessions_`
  - `trade_session_by_actor_`
- `ModernServer/src/world/map_actor.cpp`
  - `trade_session_for`
  - `trade_offer_for`
  - `trade_peer_offer_for`
  - `can_receive_trade_items`
  - `cancel_trade_for`
  - `commit_trade`
- `ModernServer/src/world/map_actor_mail.hpp`
  - `ActorMailKind::trade_try`
  - `trade_cancel`
  - `trade_add_item`
  - `trade_remove_item`
  - `trade_set_gold`
  - `trade_accept`
- `shared/protocol/client_v1/protocol.hpp`
  - `TradeTryRequest`
  - `TradeCancelRequest`
  - `TradeAddItemRequest`
  - `TradeRemoveItemRequest`
  - `TradeSetGoldRequest`
  - `TradeAcceptRequest`
  - `TradeState`

Compatibility gaps:

- `ModernServer/src/protocol/legacy_types.hpp` does not define the legacy
  `CM_DEAL*` or `SM_DEAL*` ids yet.
- `ModernServer/src/protocol/canonical_legacy_command.cpp` does not decode
  legacy `CM_DEAL*` packets yet.
- Current C++ `trade_try` uses a client-provided target name and
  `in_interaction_range`; Delphi uses mutual front-facing creatures.
- Current C++ opens a `TradeSession` and sends system notices, but does not
  send `SM_DEALMENU`.
- Current C++ `trade_set_gold` records offered gold but does not deduct it
  immediately from the player's bag gold.
- Current C++ add/remove item sends generic inventory packets instead of
  `SM_DEALADDITEM_OK`, `SM_DEALDELITEM_OK`, and remote deal packets.
- Current C++ `cancel_trade_for` can fail when returning items to a full bag;
  Delphi cannot hit this path because the items originated from that same bag.
- Current C++ `commit_trade` sends generic completion system notices instead
  of `SM_DEALSUCCESS` in the Delphi order.
- `ClientV1GameGatewayService` owns a shadow trade UI state and can send
  `TradeState` before the authoritative world actor accepts the command.

## C++ Migration Guidance

Keep the authoritative trade state in `MapActor`, not in the gateway and not as
cross-thread player component state.

Use `LegacyUserItem` in `TradeOffer.items` to mirror Delphi's moved
`PTUserItem` entries. A separate reservation abstraction is not needed for the
legacy-compatible path.

Handle trade commands inside the existing legacy player command path:

`session -> WorldService -> LogicRuntime::route_logic_command -> MapActor::enqueue_legacy_player_command -> process_user_humans -> handle_mail(..., from_legacy_operate=true)`

Do not settle trades from socket callbacks or gateway receive drains. Same
player command ordering must remain FIFO, and different players must settle in
the map actor's deterministic player operate order.

The client_v1 bridge should become a protocol adapter. It may render a typed
`TradeState`, but that state must be derived from world-authoritative trade
events or translated legacy deal packets, not from gateway-local predictions.

## Expected Successful Trace

The canonical successful trace is stored in
`ModernServer/tests/golden/trade_phase1/deal_sequence_cases.json`.

High-level order:

1. A sends `CM_DEALTRY`.
2. Server sends system messages.
3. A receives `SM_DEALMENU`.
4. B receives `SM_DEALMENU`.
5. A sends `CM_DEALADDITEM`.
6. A receives `SM_DEALADDITEM_OK`.
7. B receives `SM_DEALREMOTEADDITEM`.
8. B sends `CM_DEALCHGGOLD`.
9. B receives `SM_DEALCHGGOLD_OK`.
10. A receives `SM_DEALREMOTECHGGOLD`.
11. A sends `CM_DEALEND`.
12. B sends `CM_DEALEND`.
13. B receives A's item via `SM_ADDITEM`.
14. B receives `SM_GOLDCHANGED` if gold changed.
15. A receives B's item via `SM_ADDITEM`.
16. A receives `SM_GOLDCHANGED` if gold changed.
17. B receives `SM_DEALSUCCESS`.
18. A receives `SM_DEALSUCCESS`.

## Acceptance Criteria for Later PRs

- Legacy constants match Delphi ids.
- Legacy decoder maps all `CM_DEAL*` messages to canonical trade commands.
- `CM_DEALTRY` success requires mutual front-facing players.
- Successful start sends `SM_DEALMENU` to both players in Delphi order.
- Add/remove item uses `DealList` semantics and `SM_DEAL*` packets.
- Gold changes deduct immediately and return on cancel.
- `CM_DEALEND` enforces the 1000 ms stable window.
- Both-side confirmation commits atomically or cancels without duplication.
- Cancel, logout, death, movement/front-facing break, and map transfer return
  each player's own reserved items and gold.
- client_v1 has no authoritative shadow trade state that can diverge from
  `MapActor`.
