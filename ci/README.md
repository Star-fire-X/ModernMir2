# CI Guard Rails

This project keeps PR checks small and migration-compatible first.

## Required PR Gates

GitHub branch protection requires the existing `policy` and `windows-fast` checks.
`windows-fast` is an aggregate check: it keeps the required check name stable while `server-fast`
and `client-fast` run only when the changed paths need them.

Run the same gates locally with:

```powershell
pwsh ci/scripts/check_no_delphi_changes.ps1
pwsh ci/scripts/check_resource_format.ps1
pwsh ci/scripts/check_unsafe_policy.ps1
pwsh ci/scripts/check_change_scope.ps1
pwsh ci/scripts/check_text_hygiene.ps1
pwsh ci/scripts/resolve_fast_ci_scope.ps1
pwsh ci/scripts/check_build.ps1 -Project server -Config Debug -Suite phase2-fast
pwsh ci/scripts/run_compat_tests.ps1 -Project server -Suite phase2-fast -Config Debug
pwsh ci/scripts/check_build.ps1 -Project client -Config Debug -Suite phase2-fast
pwsh ci/scripts/run_compat_tests.ps1 -Project client -Suite phase2-fast -Config Debug
```

`resolve_fast_ci_scope.ps1` runs both fast jobs for shared CI/build files, only the server job for
server-only paths, only the client job for client-only paths, and neither for docs-only changes.
The fast build jobs also restore/save the vcpkg binary package cache used by `sqlite3:x64-windows`.

## Nightly Compatibility

`nightly.yml` runs the larger Windows-only `phase3-nightly` suite on schedule and by manual dispatch.
It is intentionally not a required PR check.

Useful local commands:

```powershell
pwsh ci/scripts/run_compat_tests.ps1 -Suite phase3-nightly -List
pwsh ci/scripts/check_build.ps1 -Project all -Config Debug -Suite phase3-nightly
pwsh ci/scripts/run_compat_tests.ps1 -Suite phase3-nightly -Config Debug -TimeoutSeconds 180
```

Known unstable or real-resource-only tests are listed by `-List` under `Quarantined tests`.
Do not remove tests from CI just to make a failure disappear; either fix the behavior in a focused PR or add a clear quarantine reason.

## Local Real Resource Tests

Real `Legend of Mir` client assets are not committed and are not downloaded in GitHub CI.
Keep them local and run:

```powershell
pwsh ci/scripts/check_build.ps1 -Project client -Config Debug -Suite phase3-nightly
pwsh ci/scripts/run_local_resource_tests.ps1 -AssetRoot "F:\mir2\Legend of Mir"
```

The local resource script checks for `Data/` and `Map/`, then runs the client tests that require real WIL/WIX/MAP/WAV assets.
