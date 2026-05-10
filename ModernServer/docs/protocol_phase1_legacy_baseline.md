# Protocol Phase 1 Legacy Baseline

This phase freezes the current Delphi legacy wire compatibility layer. It does
not introduce CanonicalLegacyCommand and does not change WorldService,
client_v1, gameplay, or rate limiting behavior.

## Scope

- `legacy_protocol` owns `#...!` frame boundaries, half packets, sticky
  packets, multi-packet drains, empty-frame skipping, and noise before the next
  `#` marker.
- `legacy_edcode` owns Delphi-compatible 6-bit message, string, and buffer
  encoding. Strings are treated as legacy bytes, not Unicode text.
- `legacy_game_codec` owns `LegacyDefaultMessage + body` packing and unpacking.
  It preserves `ident`, `recog`, `param`, `tag`, `series`, and the body bytes.

## Frozen Behavior

- The old protocol in this repository is the Mir legacy framed protocol, not a
  plain newline-delimited text protocol.
- `LegacyProtocolCodec::drain_packets` may return multiple packets from one TCP
  receive buffer and preserves their FIFO order.
- A partial frame does not produce a packet until the closing `!` is present.
- Bytes before the next `#` are discarded as frame noise.
- Empty `#!` frames are consumed and ignored.
- `legacy_encode_string` and `legacy_decode_string` preserve non-null legacy
  bytes such as ASCII delimiters, spaces, tabs, and GBK-like high bytes.
- `legacy_encode_buffer` and `legacy_decode_buffer` preserve raw bytes including
  zero bytes.

## Out Of Scope

- client_v1 is not normalized in this phase.
- `WorldService::decode_game_command` is not refactored or exported.
- No gameplay rate limiter or per-frame command budget is changed.
- No database or string storage policy is changed.
- No UTF-8 interpretation is added to legacy protocol fields.

## Golden Data

The golden cases live in:

`ModernServer/tests/golden/protocol_phase1/legacy_command_cases.json`

The smoke test driven by this data is:

`ModernServer/tests/legacy_protocol_command_golden_smoke.cpp`

These cases are intended to be reused by the next phase when
CanonicalLegacyCommand is introduced. If a future canonical decoder cannot
round-trip these legacy cases, it has changed the legacy wire semantics.
