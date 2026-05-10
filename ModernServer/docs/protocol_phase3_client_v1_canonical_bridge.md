# Protocol Phase 3 client_v1 Canonical Bridge

Phase 3 routes client_v1 gameplay requests through `CanonicalLegacyCommand`
before they re-enter the existing `LogicCommand` pipeline. The intent is to
make client_v1 a transport format for the same gameplay semantics, not a
separate gameplay entry point.

## Scope

- client_v1 gameplay requests now decode in
  `client_v1_legacy_command_decoder`.
- The gateway still owns session state checks, merchant/trade UI state,
  pending action state, response messages, and session sequencing.
- `post_canonical_command` adapts canonical commands back through
  `to_logic_command` and then uses the existing `post_logic_command` path.
- `EnterWorldRequest` remains a direct `LogicCommandKind::enter_world` path.

## Migrated Commands

- Turn, walk, run, attack, spell, pickup, use item, equip, unequip, drop item,
  and revive.
- NPC click, NPC dialog selection, merchant buy/sell/query/repair, and storage
  deposit/withdraw.
- Bag and storage refresh requests emitted while translating legacy server
  packets for client_v1.
- Trade try/cancel/add item/remove item/set gold/accept.
- Chat send to `say`.

## Preserved Behavior

- client_v1 strings remain `std::string` payloads; no UTF-8, GBK, or Unicode
  normalization is introduced.
- Existing session gates and silent ignore behavior stay in the gateway.
- `gateway` and `session_seq` are still assigned by `post_logic_command`.
- One receive drain can still produce multiple client_v1 frames, and downstream
  player inbox processing is unchanged.
- Attack legacy ident normalization still preserves the existing client_v1
  snapshot behavior.

## Out Of Scope

- Login, server selection, character selection, admission tokens, login notice,
  and disconnect state machines are unchanged.
- `MagicKeyChangeRequest`, group, guild, minimap, ping, and other gateway-local
  responses are not canonical gameplay commands.
- No gameplay validation, rate limiter, per-frame input budget, error-code
  compatibility, or encoding migration is added.

## Tests

- `mir2_client_v1_canonical_command_smoke` validates direct client_v1 decoder
  output and the `to_logic_command` adapter.
- `mir2_client_v1_game_command_bridge_smoke` remains the wire-level snapshot
  that verifies gateway output is unchanged.
- The new canonical client_v1 smoke is included in the server `phase2-fast`
  compatibility suite.

## Next Phase Candidates

- Unified login and enter-game state machine.
- Legacy-compatible client_v1 error timing and error-code mapping.
- Legacy byte string policy for account, character, chat, NPC, and GM fields.
- Batch/frame fairness, player input budgets, and legacy rate limiters.
