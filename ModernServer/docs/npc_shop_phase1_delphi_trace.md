# NPC and Shop Phase 1 Delphi Trace

This document records the Delphi NPC and merchant shop semantics that must be
preserved before the C++ NPC/shop implementation is changed further. The target
is Mir2 legacy compatibility, not a redesigned NPC, quest, or marketplace
system.

## Scope

- NPC loading, map binding, appearance, click, dialog, and menu selection.
- Merchant shop list, detail list, buy, sell, normal repair, special repair,
  and storage entry points.
- Legacy NPC script file parsing and first-pass command compatibility.
- Legacy `CM_*`, `RM_*`, and `SM_*` ids, field usage, and visible client order.
- Current C++ migration gaps in `MapActor`, NPC script runtime, merchant
  transaction logic, persistence, and the client_v1 typed bridge.

Full quest, warehouse, equipment, economy, and UI rewrites are out of scope.
Those systems are only covered where NPC/shop flows call into them.

## Delphi Source Evidence

Primary server files:

- `Source/M2Server/ObjNpc.pas`
  - `TNormNpc`, `TMerchant`, `TGuildOfficial`, and `TCastleManager`.
  - NPC script loading, condition/action execution, dialog text generation, and
    merchant buy/sell/repair/storage behavior.
- `Source/M2Server/ObjBase.pas`
  - `TUserHuman` client command dispatch.
  - `ServerGetClickNpc`, `ServerGetMerchantDlgSelect`,
    `ServerGetUserMenuBuy`, `ServerGetUserSellItem`,
    `ServerGetUserRepairItem`, and storage handlers.
- `Source/M2Server/LocalDB.pas`
  - `Merchant.txt`, `Npcs.txt`, `Market_Def`, `Npc_def`,
    `Market_Saved`, and `Market_Prices` constants and loaders.
- `Source/M2Server/UsrEngn.pas`
  - NPC and merchant list initialization, map binding, and user message
    enqueueing.
- `Source/M2Server/Envir.pas`
  - map object insertion through `AddToMap`.
- `Source/Common/Grobal2.pas`
  - legacy `CM_*`, `RM_*`, `SM_*`, script condition, and script action ids.

Primary client files:

- `Source/Client/ClMain.pas`
  - click dispatch, merchant request packets, and `SM_*` handlers.
- `Source/Client/FState.pas`
  - NPC dialog, merchant menu, sell, repair, and storage UI state.
- `Source/Client/PlayScn.pas`
  - NPC actor representation.

The legacy source and data contain local-codepage text. Compatibility work must
compare bytes or fixture files, not terminal-reencoded text.

## Boundary Classification

### A. Must Not Change

| Boundary | Reason | Breakage if changed | C++ implementation guidance | Verification |
| --- | --- | --- | --- | --- |
| NPC script file format, labels, menus, comments, and commands | Production NPC scripts depend on Delphi parsing quirks. | Existing scripts fail or branch differently. | Keep a Delphi-compatible parser in config import/runtime; preserve original tokens and bytes. | Real script fixtures plus no unexpected `unsupported_action` or `unsupported_condition` traces. |
| NPC click entry rule | The client only sends `CM_CLICKNPC` and waits for the server. | Dialog windows open early or at the wrong label. | Route click to `@main` through the player legacy command FIFO. | Click trace must produce only server-driven dialog output. |
| NPC id, map, distance, visibility, and state checks | The server is authoritative for the target NPC. | Cross-map or long-range NPC interactions become possible. | Resolve stable NPC handles server-side, then check map and the 15-tile rectangle unless a confirmed legacy exception applies. | Invalid id, cross-map, and out-of-range fuzz cases. |
| Dialog text and menu wire format | The legacy client parses `npc_name/text` and `<caption/@label>` itself. | Menus become unclickable or show wrong text. | Send `SM_MERCHANTSAY` with the same body and newline byte behavior. | Dialog body golden trace. |
| Merchant goods list body | The client parses `name/submenu/price/stock/`. | Shop rows, detail menus, and buy parameters break. | Preserve `SM_SENDGOODSLIST` and `SM_SENDDETAILGOODSLIST` fields exactly. | Shop list golden trace. |
| Price formulas and item durability effects | Shop economy depends on Delphi formulas. | Buy, sell, repair, and special repair prices diverge. | Port `GetPrice`, `GetGoodsPrice`, `GetSellPrice`, `GetBuyPrice`, and `QueryRepairCost` semantics before changing behavior. | Price formula unit tests against fixtures. |
| Buy commit order | The legacy client expects item add before buy-success gold/menu update. | UI order changes and duplicate/lost item bugs become harder to detect. | Validate first, then add item, deduct gold, send add item, then send buy success. | `SM_ADDITEM` before `SM_BUYITEM_SUCCESS`. |
| Sell commit order | The legacy client keeps a pending item outside the bag until OK/fail. | Failures may lose pending items or duplicate them. | Validate and add gold first, send sell OK, add goods to merchant, then remove the bag item. | Sell success/fail trace. |
| Repair commit order | The legacy client restores the pending item from the repair OK packet. | Dura or gold display changes in the wrong order. | Deduct gold, modify dura fields, then send repair OK with gold/dura/dura max. | Repair success/fail trace. |
| `CM_*`, `RM_*`, and `SM_*` ids, fields, and ordering | The old client is hard-coded to these ids and fields. | Legacy client compatibility breaks. | Treat legacy packets as the source of truth; typed protocols are adapters only. | Protocol constants golden. |
| Per-player FIFO processing | Delphi processes player messages in order. | Same-frame buy/sell/drop/trade races can duplicate or lose items. | Queue all NPC/shop commands into the player's legacy command stream and process serially. | High-rate same-frame fuzz. |
| Server authority for price, item, gold, coordinate, and NPC identity | The client can forge all request fields. | Economic exploits. | Recalculate every price and resolve every object from server state. | Forged price/id/quantity tests. |

### B. Preserve Legacy Behavior With Modern Encapsulation

| Boundary | Reason | Breakage if changed | C++ implementation guidance | Verification |
| --- | --- | --- | --- | --- |
| NPC object model | Delphi stores NPCs as map objects with runtime state. | Visibility, interaction, and tick order drift. | Keep `Npc` as a `GameObject` owned by `MapActor`; split definition from instance state only internally. | NPC appear/disappear smoke. |
| Merchant runtime state | Stock, dynamic prices, and saved goods are mutable. | Shop inventory resets or reorders incorrectly. | Keep mutable goods, prices, and upgrade state on the NPC instance and persist snapshots. | Merchant persistence replay test. |
| NPC script runtime | Modern types are useful, but execution must remain synchronous. | Rewards and dialog pages are delayed or reordered. | Use a typed context that executes in the player logic phase, without async callbacks. | Script branch golden trace. |
| Dialog context | Cross-NPC replay must be rejected without breaking same-NPC legacy labels. | Either exploits remain or old scripts stop working. | Store player current NPC id, map, and generation; apply stricter label checks only where golden traces prove compatibility. | Replay fuzz. |
| Script command table | Command dispatch can be table-driven. | Unknown command tolerance changes. | Normalize command names case-insensitively but preserve original arguments. | Malformed script fixtures. |
| Inventory and gold operations | Transactions prevent duplication. | Partial updates create lost items or gold. | Use small internal transaction guards that commit visible packets in Delphi order. | Economic consistency tests. |
| Protocol adapter | client_v1 needs typed data. | Two protocols diverge behaviorally. | Decode client_v1 requests into canonical legacy commands and derive typed responses from legacy SM semantics. | legacy/client_v1 paired smoke. |
| Illegal packet defense | C++ should not trust clients. | Forged shop operations succeed. | Reject invalid state before mutating, then trace the failure. | Fuzz. |

### C. May Be Modernized If Proven Equivalent

| Boundary | Allowed modernization | Constraint | Verification |
| --- | --- | --- | --- |
| C++ ownership and RAII | Use value types, handles, and RAII-managed resources. | External NPC ids and frame-visible state must not change unexpectedly. | Reload and stale-handle tests. |
| Script preparse cache | Cache parsed labels and command tokens. | Cache must preserve Delphi whitespace, case, and error tolerance. | Parser golden fixtures. |
| Shop item index cache | Cache item lookups for speed. | Send order, detail order, stock count, and refresh time must match legacy. | Goods list diff. |
| Typed command parameters | Convert tokens into typed values internally. | Invalid or odd Delphi input must not be rejected unless Delphi rejects it. | Script fuzz. |
| Trace, metrics, and diagnostics | Add frame-level trace and counters. | No new visible client messages or delayed behavior. | Trace-only tests. |
| Lint and config validation tools | Warn about damaged scripts and goods lists. | Delphi-runnable content must not hard-fail by default. | Damaged config smoke. |
| Performance caches | Cache std item and price results. | Prices, stock refresh, and item attributes must reflect the same frame state as Delphi. | Buy/sell/repair golden trace. |

## Delphi NPC Object Model

`TNormNpc` is derived from `TAnimal`, which is derived from `TCreature`.
`TMerchant` derives from `TNormNpc`. `TGuildOfficial` and `TTrainer` derive
from `TNormNpc`; `TCastleManager` derives from `TMerchant`.

`TNormNpc.Create` initializes the NPC as an immortal server NPC:

- `NeverDie := TRUE`
- `RaceServer := RC_NPC`
- `Light := 2`
- `StickMode := TRUE`
- `BoInvisible := FALSE`
- `BoUseMapFileName := TRUE`
- service flags such as `CanSell`, `CanBuy`, `CanStorage`, `CanRepair` false

`TMerchant` adds:

- `MarketName`
- `MarketType`
- `PriceRate`, defaulting to `100`
- `NoSeal`
- `DealGoods`
- `ProductList`
- `GoodsList`
- `PriceList`
- `UpgradingList`

`Npcs.txt` race values create specialized NPC classes, but ordinary script NPCs
are often still represented by `TMerchant`.

## Delphi Loading and Map Binding

Startup loads maps and environment data before loading merchants and NPCs.
`LoadMerchants` parses:

```text
marketname map x y seller face appearance castle
```

Each merchant is later bound to a `PEnvir` through `GrobalEnvir.GetEnvir`, then
initialized into the map and loaded from:

```text
Market_Def\<MarketName>-<MapName>.txt
Market_Saved\<MarketName>-<MapName>.sav
Market_Prices\<MarketName>-<MapName>.prc
```

`LoadNpcs` parses:

```text
name race map x y face body
```

Script NPC definitions are loaded from:

```text
Npc_def\<NpcName>-<MapName>.txt
```

NPC map insertion runs through the same creature appearance path and
`PEnvir.AddToMap(CX, CY, OS_MOVINGOBJECT, self)`. A failed map insertion marks
initialization error.

## Script File Semantics

Important parser behavior:

- `Merchant.txt`, `Npcs.txt`, `Market_Def`, and `Npc_def` are legacy text files.
- Blank lines are ignored.
- Lines beginning with `;` are comments.
- `LoadMarketDef` also ignores `/` comment lines in the script body.
- `#SETHOME`, `#DEFINE`, `#INCLUDE`, and `#CALL` are supported.
- `@HOME` defaults to `@main`.
- Labels are bracket sections such as `[Goods]` or `[@main]`.
- `#IF`, `#ACT`, `#SAY`, `#ELSEACT`, and `#ELSESAY` define condition, action,
  say, else-action, and else-say blocks.
- `\` is converted to Delphi newline byte `char($a)` in dialog text.
- Menu links are ordinary dialog text parsed by the client as `<caption/@label>`.
- Service flags are discovered by scanning loaded dialog text for labels such
  as `@buy`, `@sell`, `@storage`, `@getback`, `@repair`, `@makedrug`, and
  `@upgradenow`.

Current unknowns that require source or runtime confirmation before changing
behavior are marked with the required compatibility marker:

- `待源码核对`: `#DEFINE` substitution appears constrained by parser state in
  Delphi; the C++ preprocessor currently applies replacements more broadly.
- `待源码核对`: `#CALL` expansion order must be checked against nested call
  fixtures.
- `待源码核对`: `TMerchant(npc).ActivateNpcUtilitys` is called on a base NPC
  parameter in the Delphi loader; whether every live caller is actually
  merchant-compatible is pending verification.

## Script Execution Semantics

NPC click enters `@main`. `NpcSayTitle` selects the active section, evaluates
conditions, appends say text, and executes actions immediately. Actions can
give or take gold/items, move maps, set variables, close dialog, spawn monsters,
or jump to other sections.

Visible behavior to preserve:

- Rewards and item removals happen during script execution, not later.
- A non-empty accumulated dialog sends `RM_MERCHANTSAY`.
- `CLOSE` sends `RM_MERCHANTDLGCLOSE`.
- `MAPMOVE` can send hide/move messages before further dialog behavior.
- `GIVE` for non-gold items creates a real item; when the bag cannot accept it,
  Delphi drops the item near the player instead of silently losing it.
- `TAKE` scans and deletes matching bag items directly; exact partial-failure
  behavior must remain bug-compatible once confirmed.

## Core Script Command Compatibility Table

| Command | Purpose | Parameters | Side effects | C++ target | Must preserve | Pending checks |
| --- | --- | --- | --- | --- | --- | --- |
| `CHECKLEVEL` | Level condition | comparator/value style legacy tokens | none | `map_actor_npc.hpp` | yes | exact comparator aliases |
| `CHECKJOB` | Job condition | job name or id | none | `map_actor_npc.hpp` | yes | localized job names |
| `CHECKGOLD` | Gold condition | amount | none | `map_actor_npc.hpp` | yes | gold cap constants |
| `CHECKITEM` | Bag item condition | name count | none | `map_actor_npc.hpp` | yes | name byte comparison |
| `CHECKITEMW` | Weighted item condition | name count | none | `map_actor_npc.hpp` | yes | weight semantics |
| `CHECKBAGGAGE` | Bag capacity condition | optional item/count | none | `map_actor_npc.hpp` | yes | grid vs weight priority |
| `CHECK` | Quest flag condition | flag id/name | none | `map_actor_npc.hpp` | yes | flag persistence scope |
| `GIVE` | Give item or gold | name count | adds gold, adds item, or drops item if full | `map_actor_npc.hpp` | yes | drop location |
| `TAKE` | Take item or gold | name count | removes gold or bag items | `map_actor_npc.hpp` | yes | partial delete behavior |
| `TAKEW` | Take weighted item | name count | removes weighted items | `map_actor_npc.hpp` | yes | exact weight match |
| `GIVEEXP` | Give experience | amount | player exp/level messages | pending | yes | level-up packet order |
| `MAPMOVE` | Teleport player | map x y | hide/move state changes | `map_actor_npc.hpp` | yes | packet order |
| `ADDSKILL` | Teach skill | skill level | skill state changes | pending | yes | refresh packet |
| `SENDMSG` | System message | channel text | visible message | packet adapter | yes | color/channel mapping |
| `MOV`/`INC`/`DEC`/`SUM`/`MOVR` | Script variables | variable/value | variable mutation | `map_actor_npc.hpp` | yes | variable lifetime |
| `GOTO` | Jump label | label | section switch | `map_actor_npc.hpp` | yes | recursion behavior |
| `CLOSE` | Close NPC dialog | none | sends close dialog | packet adapter | yes | sell window side effects |

## Merchant Capabilities and Goods Definitions

Merchant script text enables services:

- `@buy` -> `CanBuy`
- `@sell` -> `CanSell`
- `@storage` -> `CanStorage`
- `@getback` -> `CanGetBack`
- `@repair` -> `CanRepair`
- `@makedrug` -> `CanMakeDrug`
- `@upgradenow` -> `CanUpgrade`

Merchant definition script behavior:

- `%` lines set `PriceRate`, accepted by Delphi when the value is at least 55.
- `+` lines add tradable standard item modes into `DealGoods`.
- `[Goods]` lines define replenished goods by item name, target count, and
  refresh interval.
- `Market_Saved` stores concrete `TUserItem` instances grouped by item index.
- `Market_Prices` stores dynamic prices.

Goods list display sends one row per goods group:

```text
name/submenu/price/stock/
```

`SubMenu` is `0` for simple goods and `1` for detail goods. Delphi treats
`StdMode <= 4`, `StdMode = 31`, and `StdMode = 42` as simple goods.

## Price Semantics

The Delphi merchant price chain is:

```text
GetPrice(std_item_index)
GetGoodsPrice(user_item)
GetSellPrice(player, price)
GetBuyPrice(price)
QueryRepairCost(player, user_item)
```

Important details:

- `GetPrice` checks the merchant `PriceList` first, then the standard item
  price if the merchant deals that item mode.
- `GetGoodsPrice` adjusts for durability and upgraded attributes.
- Some `StdMode` values such as `40` and `43` have special handling.
- `GetSellPrice` applies `PriceRate` and castle discount behavior.
- `GetBuyPrice` is `Round(price / 2)`.
- Normal repair cost is based on roughly one third of the sell price scaled by
  lost durability.
- Special repair cost is normal repair cost multiplied by three.

The current C++ price helpers are known to be incomplete for some item modes and
upgrade attributes. Porting the Delphi formula is required before PRs that claim
price compatibility.

## Client UI Semantics

The legacy client does not open NPC or shop UI optimistically. A click on an
actor with `RCC_MERCHANT` sends `CM_CLICKNPC` and waits.

Visible UI order:

1. `SM_MERCHANTSAY` opens the NPC dialog.
2. Clicking a dialog link sends `CM_MERCHANTDLGSELECT`.
3. `@buy` returns dialog text and then `SM_SENDGOODSLIST`.
4. `SM_SENDGOODSLIST` opens the merchant menu and moves the bag window.
5. `@sell` returns `SM_SENDUSERSELL` and opens the sell dialog.
6. `@repair` returns `SM_SENDUSERREPAIR` and opens the repair dialog.
7. Local close buttons close UI locally; there is no merchant close request.
8. Server close uses `SM_MERCHANTDLGCLOSE`.

Sell and repair UI use a pending item slot. On failure the client returns the
pending item to the bag locally. On success the success packet clears or updates
that pending item.

## Legacy Protocol Constants

Client requests:

| Request | Id | Fields |
| --- | ---: | --- |
| `CM_CLICKNPC` | 1010 | `recog=npc_id` |
| `CM_MERCHANTDLGSELECT` | 1011 | `recog=npc_id`, body is selected label text |
| `CM_MERCHANTQUERYSELLPRICE` | 1012 | `recog=npc_id`, `param/tag=MakeIndex`, body is item name |
| `CM_USERSELLITEM` | 1013 | `recog=npc_id`, `param/tag=MakeIndex`, body is item name |
| `CM_USERBUYITEM` | 1014 | `recog=npc_id`, `param/tag=item server index`, body is item name |
| `CM_USERGETDETAILITEM` | 1015 | `recog=npc_id`, detail menu/top index fields |
| `CM_USERREPAIRITEM` | 1023 | `recog=npc_id`, `param/tag=MakeIndex`, body is item name |
| `CM_MERCHANTQUERYREPAIRCOST` | 1024 | `recog=npc_id`, `param/tag=MakeIndex`, body is item name |
| `CM_USERSTORAGEITEM` | 1031 | `recog=npc_id`, `param/tag=MakeIndex`, body is item name |
| `CM_USERTAKEBACKSTORAGEITEM` | 1032 | storage withdraw request |
| `CM_USERMAKEDRUGITEM` | 1034 | make-drug request |

Server responses:

| Response | Id | Fields |
| --- | ---: | --- |
| `SM_MERCHANTSAY` | 643 | `recog=npc_id`, `param=face`, body is `npc_name/text` |
| `SM_MERCHANTDLGCLOSE` | 644 | closes merchant dialog |
| `SM_SENDGOODSLIST` | 645 | `recog=npc_id`, `param=count`, body is goods rows |
| `SM_SENDUSERSELL` | 646 | opens sell dialog |
| `SM_SENDBUYPRICE` | 647 | `recog=price` or `0` |
| `SM_USERSELLITEM_OK` | 648 | `recog=gold` |
| `SM_USERSELLITEM_FAIL` | 649 | sell failed |
| `SM_BUYITEM_SUCCESS` | 650 | `recog=gold`, `param/tag=item server index` |
| `SM_BUYITEM_FAIL` | 651 | `recog=reason code` |
| `SM_SENDDETAILGOODSLIST` | 652 | detail goods item buffer |
| `SM_GOLDCHANGED` | 653 | gold changed |
| `SM_SENDUSERREPAIR` | 668 | opens repair dialog |
| `SM_USERREPAIRITEM_OK` | 669 | `recog=gold`, `param=dura`, `tag=dura_max` |
| `SM_USERREPAIRITEM_FAIL` | 670 | repair failed |
| `SM_SENDREPAIRCOST` | 671 | `recog=cost` or `-1` |

## Expected Operation Ordering

### NPC Click and Dialog

```text
CM_CLICKNPC
server resolves NPC
server checks map and 15-tile range
server executes @main immediately
SM_MERCHANTSAY
client opens NPC dialog
```

### Open Shop

```text
CM_MERCHANTDLGSELECT body=@buy
server validates same NPC
server executes @buy dialog text
SM_MERCHANTSAY, when @buy has say text
SM_SENDGOODSLIST
client opens merchant menu and bag
```

### Buy

```text
CM_USERBUYITEM
server validates NPC, goods, price, gold, weight, and bag capacity
server adds item to player
server deducts gold
SM_ADDITEM
optional legacy weight refresh
SM_BUYITEM_SUCCESS
client shows item first, then updates gold/shop sold-out state
```

Failure order:

```text
CM_USERBUYITEM
server performs validation without mutating durable state
SM_BUYITEM_FAIL with reason 1, 2, or 3
client keeps shop open
```

### Sell

```text
CM_MERCHANTQUERYSELLPRICE
SM_SENDBUYPRICE
client places item into pending sell slot
CM_USERSELLITEM
server validates bag item and merchant buy rules
server adds gold
SM_USERSELLITEM_OK
server adds a copy of the item to merchant goods
server removes item from player bag
optional legacy weight refresh
client clears pending item and updates gold
```

### Repair

```text
CM_MERCHANTQUERYREPAIRCOST
SM_SENDREPAIRCOST
client places item into pending repair slot
CM_USERREPAIRITEM
server validates item and cost
server deducts gold
server modifies Dura and DuraMax
SM_USERREPAIRITEM_OK
client restores pending item with updated dura fields and updates gold
```

Special repair uses the same visible result packet but must not reduce
`DuraMax`.

## Main Loop Compatibility

Delphi receives socket data, enqueues player messages, then processes each
player's messages in the user run path before merchant and NPC periodic runs.

The C++ implementation should keep:

```text
socket decode
canonical legacy command enqueue
player command logic
monster/map logic
merchant periodic logic
npc periodic logic
event/timer logic
frame-end dispatch
```

NPC click, dialog selection, buy, sell, repair, and storage commands must be
player-command-stage operations. Merchant replenishment belongs in the merchant
periodic stage. Frame-end dispatch is allowed only if it preserves the exact
per-player packet append order.

## Current C++ Gap Summary

Known implementation risks:

- `compute_goods_price` is simpler than Delphi `GetGoodsPrice` and does not yet
  cover all special item modes and upgrade attribute pricing.
- Buy currently risks removing merchant goods before all validations; rollback
  can change goods order.
- Sell currently risks deleting a bag item before matching Delphi's add-gold
  first ordering.
- Repair can emit extra visible gold refreshes that Delphi client handlers do
  not require for repair success.
- `client_v1` merchant goods mapping drops legacy `submenu` and `stock` for
  simple goods.
- `client_v1` lacks dedicated buy/sell/repair success and failure result
  messages matching legacy semantics.
- Script preprocessing and unknown-command handling need more Delphi fixtures.
- Gold cap handling must be unified across pickup, script `GIVE Gold`, and
  merchant sell.

## Required Golden Trace Artifacts

This phase freezes the expected baseline in:

```text
ModernServer/tests/golden/npc_shop_phase1/npc_shop_protocol_constants.json
ModernServer/tests/golden/npc_shop_phase1/npc_shop_sequence_cases.json
```

Later PRs must either pass tests derived from these traces or update this
document with new source evidence explaining why the trace was wrong.

## Pending Source Checks

- `待源码核对`: Exact death-state behavior for NPC click and merchant selection.
- `待源码核对`: Whether invisible NPCs intentionally bypass map/range checks during menu
  selection.
- `待源码核对`: `#DEFINE` and `#CALL` edge cases with nested include/call
  fixtures.
- `待源码核对`: `ZenHour` unit mismatch between initial product timestamp and
  refill checks.
- `待源码核对`: `PriceUp` and `PriceDown` apparent local-value bugs.
- `待源码核对`: Whether sell failure can visibly send duplicate fail packets.
- `待源码核对`: Exact storage deposit/withdraw packet order relative to item
  delete and weight refresh.
- `待源码核对`: Exact system message text and order for buy, sell, repair, and storage
  failures.

## PR-1 Acceptance Criteria

This phase is complete when:

- The source paths and Delphi entry points above are documented.
- The must-not-change, preserve-with-wrapper, and may-modernize boundaries are
  documented.
- The click, dialog, goods-list, buy, sell, repair, and storage expected traces
  are documented in this file and in the golden fixture JSON files.
- Current C++ reuse points and gaps are documented.
- Every uncertain Delphi behavior found in this audit is explicitly marked
  `待源码核对`.
- No runtime C++ behavior is changed in this PR.
