# ModernServer

Modern C++20 headless host for Mir2 service modules.

## What is included

- `mir2_host` single-process runtime with module lifecycle management
- typed local bus with bounded queues
- fixed-step logic runtime and per-map actor model
- protocol gateway skeleton with legacy packet framing
- SQLite persistence service and schema bootstrap
- legacy asset importer for `!SetUp.txt`, `!servertable.txt`, `StartPoint.txt`,
  `mapinfo.txt`, `MonZen.txt`, `merchant.txt`, `Npcs.txt`, and optional MDB via ODBC

## Build

```powershell
cmake -S F:\mir2\ModernServer -B F:\mir2\ModernServer\build -G Ninja -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe
cmake --build F:\mir2\ModernServer\build
ctest --test-dir F:\mir2\ModernServer\build --output-on-failure
```

## Run

```powershell
F:\mir2\ModernServer\build\mir2_host.exe --config-root F:\mir2\ModernServer\config --run-seconds 10
```

## Import legacy assets

```powershell
F:\mir2\ModernServer\build\mir2_host.exe `
  --config-root F:\mir2\ModernServer\config `
  --legacy-root F:\mir2\Release\Mir200 `
  --import-only
```
