# Phase 7: Legacy String Semantics

Phase 7 fixes the remaining string semantics drift at protocol boundaries. Fields that affect
legacy gameplay or account identity are treated as byte payloads, not as Unicode text.

## Legacy Byte Fields

These fields are legacy byte strings in server-side semantics:

- Account id and password on legacy framed auth and client_v1 auth requests.
- Character names for create, delete, select, summaries, and enter-world identity.
- Canonical gameplay `text`: chat, NPC selections, merchant/storage item names, item use/equip/drop
  names, and trade target/item names.
- Legacy edcode string bodies decoded from `#...!` game packets.

`LegacyString`/`LegacyStringView` preserve `std::string` bytes. They do not validate UTF-8, convert
GBK/ANSI, normalize Unicode, fold case, trim whitespace, or count characters. Length checks are byte
length checks.

## Validation Rules

- Account ids use the current legacy rule: non-empty, ASCII bytes 48..122, or a GBK-like byte pair
  with first byte `0xB0..0xC8` and second byte `0xA1..0xFE`.
- Character names use the current legacy create-character boundary: byte length 3..14 and ASCII
  alphanumeric bytes only.
- Passwords remain byte strings. This phase does not add new password length or content validation.

## client_v1 Wire Policy

client_v1 wire strings remain `2-byte length + payload bytes`. The shared protocol comments now avoid
describing these payloads as server-side UTF-8 text. A modern client may render UI strings as UTF-8,
but legacy-sensitive bytes are not reinterpreted by the server when deciding account, character,
chat, NPC, item, or trade behavior.

## Out Of Scope

- No SQLite schema migration; existing `TEXT` columns continue to be used as before.
- No server-side UTF-8 <-> GBK/ANSI conversion.
- No wire error-code unification or response format change.
- No change to canonical command kinds, login state guard, input budget, attack cadence, or trade
  stable-window behavior.
