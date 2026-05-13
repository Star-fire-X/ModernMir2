# ModernClient

Modern C++20 Win32 client skeleton for the Mir2 rewrite.

## Current Slice

- Win32 window shell with a fixed `800x600` logical surface
- Single-threaded runtime loop
- CPU software renderer with D3D11 present-only output
- Retained-mode custom UI tree
- `client_v1` TCP protocol client
- Explicit Delphi protocol migration map for all 57 `Send*`, 39 `ClientGet*`,
  87 `CM_*`, and 220 `SM_*` entries
- Full `client_v1` account profile payloads for create-account and account-update flows
- Real `Legend of Mir` `WIL + map` loading for tiles, objects, and player body frames
- Explicit scene flow:
  `Boot -> Login -> ServerSelect -> CharacterSelect -> Loading -> World`
- Real map tile/object rendering in `WorldScene` plus real `Hum.wil` actor frames
- Movement intent submission against the placeholder world gateway

## Build

```powershell
cmake -S F:\mir2\ModernClient -B F:\mir2\ModernClient\build -G Ninja -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe
cmake --build F:\mir2\ModernClient\build
```

## Run

Start the server first:

```powershell
F:\mir2\ModernServer\build-clientv1\mir2_host.exe --config-root F:\mir2\ModernServer\config
```

Then run the client:

```powershell
F:\mir2\ModernClient\build\modern_mir2_client.exe
```

## Default Connection

`ModernClient\config\client.ini`

- login host: `127.0.0.1`
- login port: `5600`
- asset root: `F:\mir2\Legend of Mir`
- character gateway: issued by the login gateway from `ModernServer`
- game gateway: issued after character selection

## Asset Smoke Test

```powershell
F:\mir2\ModernClient\build\modern_client_asset_smoke.exe
```

Expected output is a single line describing decoded map and frame sizes.

## Controls

- Login scene:
  enter account and password, then click `Login`; create-account and required
  profile-update dialogs include the legacy name, birthday, security questions,
  phone, mobile, and email fields
- Server select scene:
  choose a server from the gateway-provided list; the login gateway returns the
  character gateway endpoint and a lobby token before the client reconnects
- Character select:
  choose a slot, then use `Start`, `New`, or `Erase`
- World scene:
  arrow keys move the placeholder actor
  hold `Shift` to send `run` mode

## Notes

- This is the first rewrite slice, not full feature parity.
- `WIL + .map` is live for the current `Legend of Mir` asset pack.
- `WZL`, `FIR`, full actor layering (`hair/weapon`), minimap, inventory, chat UI, combat, and legacy visual parity are still staged follow-up work.
