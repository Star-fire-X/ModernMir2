# Delphi Auth Trace Fixtures

Place normalized Delphi auth traces here after running
`ci/scripts/normalize_delphi_auth_trace.ps1`.

Expected fixture names:

- `startup_only.trace`
- `login_success.trace`
- `login_failure_bad_password.trace`
- `select_server_success.trace`
- `select_server_failure.trace`
- `select_character_failure.trace`
- `login_notice_accept.trace`
- `disconnect_login.trace`
- `disconnect_select_character.trace`
- `disconnect_play.trace`

Do not commit raw traces with `tick=` fields or local paths.
