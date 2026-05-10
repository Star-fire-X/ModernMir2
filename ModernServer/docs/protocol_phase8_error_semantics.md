# Phase 8: Legacy Error Semantics

Phase 8 centralizes login, lobby, character selection, and enter-world error semantics in
`CanonicalLoginErrorMapping`. The helper is protocol neutral: it does not send packets, access
storage, or advance login state. Legacy framed and client_v1 services still own their wire
responses, but they now read the same error table before sending those responses.

This phase intentionally keeps wire shapes unchanged. Legacy framed responses still use the
existing `SM_*FAIL` packets. client_v1 keeps its current result messages and `DisconnectReason`
messages; where a result has a `code` field, that field now uses the legacy-equivalent code from
the same mapping table.

## Canonical Mapping

| Semantic error | Legacy response | client_v1 response |
| --- | --- | --- |
| Empty or missing credentials | `SM_PASSWDFAIL.recog=-4` | `LoginResult{code=-4,error="login_failed"}` |
| Bad password | `SM_PASSWDFAIL.recog=-1` | `LoginResult{code=-1,error="login_failed"}` |
| Password lock | `SM_PASSWDFAIL.recog=-2` | `LoginResult{code=-2,error="login_failed"}` |
| Banned account | `SM_PASSWDFAIL.recog=-5` | `LoginResult{code=-5,error="login_failed"}` |
| Duplicate login | `SM_PASSWDFAIL.recog=-3` | `LoginResult{code=-3,error="login_failed"}` |
| Create account failed | `SM_NEWIDFAIL.recog=0` | `CreateAccountResult{code=0,error="create_account_failed"}` |
| Update account failed | `SM_UPDATEIDFAIL.recog=-1` | `UpdateAccountResult{code=-1,error="update_account_failed"}` |
| Change password failed | `SM_CHGPASSWDFAIL.recog=0/-1/-2` | `ChangePasswordResult{code=0/-1/-2,error="change_password_failed"}` |
| Select server illegal stage | `SM_PASSWDFAIL.recog=-4` | `DisconnectReason{401,"not_authenticated"}` |
| Query characters illegal stage | `SM_QUERYCHRFAIL.series=1` | `DisconnectReason{401,"not_authenticated"}` |
| Create character illegal stage | `SM_NEWCHRFAIL.recog=0` | `DisconnectReason{401,"not_authenticated"}` |
| Invalid character name | `SM_NEWCHRFAIL.recog=0` | `CreateCharacterResult{code=0,error="invalid_character_name"}` |
| Character name exists | `SM_NEWCHRFAIL.recog=2` | `CreateCharacterResult{code=2,error="create_character_failed"}` |
| Character slots full | `SM_NEWCHRFAIL.recog=3` | `CreateCharacterResult{code=3,error="character_slots_full"}` |
| Character create persist failure | `SM_NEWCHRFAIL.recog=4` | `CreateCharacterResult{code=4,error="create_character_failed"}` |
| Delete character illegal stage/failure | `SM_DELCHRFAIL.recog=0` | `DisconnectReason{401,"not_authenticated"}` or `DeleteCharacterResult{code=0,error="delete_character_failed"}` |
| Select character illegal stage/missing | `SM_STARTFAIL.recog=0` | `DisconnectReason{401,"not_authenticated"}` or `character_not_found` |
| Enter token invalid | none | `DisconnectReason{401,"invalid_enter_world_token"}` |
| Duplicate enter world | none | `DisconnectReason{409,"already_entered_world"}` |
| Missing client hello | none | `DisconnectReason{400,"missing_client_hello"}` |
| Protocol version mismatch | none | `DisconnectReason{426,"protocol_version_mismatch"}` |

## Boundaries

- Gameplay failures, item/NPC/trade business prompts, and GM command results remain outside this
  phase.
- client_v1 response structures are not extended. Responses without a `code` field continue to
  carry only their existing text or disconnect reason.
- `DisconnectReason.code` remains a client_v1 transport/session code, not a legacy `recog` value.
- Login state, legacy byte string handling, input budgets, attack cadence, and trade stable-window
  behavior are unchanged.
