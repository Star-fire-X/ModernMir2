# Delphi Auth Trace Capture

This document records how to capture startup and login-flow traces from the
legacy Delphi client. The instrumentation lives in `Source/Client` and is
disabled unless `MIR2_DELPHI_AUTH_TRACE_FILE` is set.

## Enable Trace

```powershell
New-Item -ItemType Directory -Force F:\mir2\out\delphi-auth | Out-Null
$env:MIR2_DELPHI_AUTH_TRACE_FILE = "F:\mir2\out\delphi-auth\login_success.trace"
```

Launch the Delphi client normally. The trace file is append-only, so delete or
rename an old file before capturing a new scenario.

Normalize a captured trace before comparing or committing it:

```powershell
pwsh F:\mir2\ci\scripts\normalize_delphi_auth_trace.ps1 `
  -InputPath F:\mir2\out\delphi-auth\login_success.trace `
  -OutputPath F:\mir2\ModernClient\tests\golden\delphi_auth\login_success.trace
```

## Trace Format

Each line is ASCII key-value text:

```text
seq=1|tick=1234|event=send_login|conn=cnsLogin|scene=stLogin|ident=2001|recog=20040415|param=0|tag=0|series=0|note=uid_len=4;passwd_len=6
```

`tick` is removed by the normalizer. Socket payloads, modal text, character
names, server names, and packet bodies are represented as lengths and hex
samples. Passwords are never written as text.

## Scenarios

Capture these scenarios as normalized fixtures under
`ModernClient/tests/golden/delphi_auth/`:

- `startup_only.trace`: start the client and stop once the login scene is visible.
  Expected order: `application_initialize_begin`, form creation,
  `startup_form_create_begin/end`, `dx_initialize_begin/end`, `show_login`.

- `login_success.trace`: submit account/password and receive
  `SM_PASSOK_SELECTSERVER`. Expected order: `password_enter_send_login`,
  `send_login`, `recv_login_success`, `client_get_select_server`,
  `show_server_select`.

- `login_failure_bad_password.trace`: receive `SM_PASSWD_FAIL(-1)`.
  Expected order: `send_login`, `recv_login_failure`, `modal_enter`,
  `modal_ok`, `password_fail_restore_login_box`, `focus_login_id`.

- `select_server_success.trace`: click a server, receive `SM_SELECTSERVER_OK`,
  connect to the character gate, query characters, and receive `SM_QUERYCHR`.
  Expected order: `click_server_select`, `send_select_server`,
  `recv_select_server_ok`, `connection_step_change(to=cnsSelChr)`,
  `connect_character_gate`, `send_query_character`, `recv_query_character`.

- `select_server_failure.trace`: while waiting for `SM_SELECTSERVER_OK`, make
  the remote side close or fail the connection. The Delphi source has no clear
  main-link `SM_SELECTSERVER_FAIL`; record the actual disconnect/modal/close
  behavior instead of inventing a synthetic packet.

- `select_character_failure.trace`: select a character and receive
  `SM_STARTFAIL`. Expected order: `send_select_character`, `recv_start_fail`,
  `hide_login_box`, selected-server-full modal, close.

- `login_notice_accept.trace`: after `SM_STARTPLAY`, connect RunGate, enter
  login notice, receive `SM_SENDNOTICE`, press OK, send `CM_LOGINNOTICEOK`,
  receive `SM_LOGON`, and switch to world. Expected order:
  `recv_start_play`, `connect_run_gate`, `show_login_notice`,
  `recv_login_notice`, `login_notice_ok`, `recv_logon`, `show_world`.

- `disconnect_login.trace`, `disconnect_select_character.trace`,
  `disconnect_play.trace`: remote close in `cnsLogin`, `cnsSelChr`, and
  `cnsPlay`. Preserve modal result, scene, and `ConnectionStep`.

## Comparison Policy

ModernClient still uses `client_v1`; do not compare Delphi `#...!` wire bytes
with C++ frames. Compare semantic event order, scene transitions,
`ConnectionStep`, modal/focus behavior, and gate boundaries. If a normalized
Delphi fixture disagrees with the C++ golden trace, file a follow-up PR for the
C++ behavior delta.
