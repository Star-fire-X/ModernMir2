# Delphi Auth UI Golden Baseline

`auth_ui_manifest.json` is generated from `Source/Client/FState.dfm`, `Source/Client/ClMain.dfm`,
`Source/Client/FState.pas`, and `Source/Client/IntroScn.pas` with:

```powershell
python F:\mir2\ci\scripts\extract_delphi_auth_ui_manifest.py `
  --repo-root F:\mir2 `
  --output F:\mir2\ModernClient\tests\golden\delphi_ui\auth_ui_manifest.json
```

The DFM parser reads binary `TPF0` object streams. Runtime assignments from the Pascal sources
override initial DFM coordinates where the original client moves controls during startup.

`auth_ui_screenshot_golden.txt` is an asset-free render baseline. It verifies rectangles, layering,
modal coverage, and key pixels without depending on WIL/WIX resources.

`auth_ui_real_asset_screenshot_golden.txt` is generated from local real assets and is only checked by
the local resource suite. Do not commit WIL/WIX files or generated screenshots.

`auth_animation_timeline_golden.txt` freezes the Delphi auth animation semantics without assets:
login-door frame timing, fade flag/clamp behavior, and selected-character freeze/unfreeze/idle
frame sequences.

`auth_animation_real_asset_frame_golden.txt` is a local-resource checksum baseline for the real
`ChrSel.wil` animation frames used by login door, effects, idle, freeze, and unfreeze rendering.
