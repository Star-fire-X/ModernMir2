# Protocol Phase 2 Canonical Legacy Command

Phase 2 introduces a production canonical command layer for legacy framed
gameplay packets. It keeps behavior equivalent to the previous
`WorldService::decode_game_command` path.

## Scope

- `CanonicalLegacyCommand` is defined in `src/protocol`.
- Legacy framed gameplay packets are decoded by
  `decode_legacy_game_command`.
- `WorldService` converts successful canonical commands back to the existing
  `LogicCommand` type before routing to `LogicRuntime`.
- Legacy byte strings remain `std::string` byte payloads. No UTF-8, GBK, or
  Unicode normalization is introduced.

## Frozen Behavior

- `CM_TURN`, `CM_WALK`, and `CM_RUN` still derive `x/y` from `recog` and `dir`
  from `tag`.
- Attack command idents still map to a single attack command while preserving
  the original `LegacyDefaultMessage.ident`.
- `CM_SPELL` still keeps its raw body in `text`.
- Chat, NPC, merchant, storage, and item text fields still decode through
  `legacy_decode_string`.
- Unsupported legacy idents return `unsupported_ident`; malformed packets return
  `malformed_packet`.
- `WorldService` still falls through to the existing raw `ENTER/MOVE/ATTACK`
  fallback when no canonical gameplay command is produced.

## Out Of Scope

- client_v1 still constructs `LogicCommand` directly.
- Login, server selection, character selection, and enter-game state machines
  are unchanged.
- No input budget, rate limiter, or player inbox behavior is changed.
- No database or string storage migration is performed.

## Tests

`mir2_canonical_legacy_command_smoke` reuses the Phase 1 golden command cases
and validates both canonical fields and the `to_logic_command` adapter.
