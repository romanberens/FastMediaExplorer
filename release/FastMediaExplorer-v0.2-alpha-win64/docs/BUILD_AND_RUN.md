# Build and Run

## Requirements

- Windows 10/11
- Visual Studio Community 2022 (Desktop C++)
- CMake

## Project Structure

```text
src/
  core/
  ui/
  media/          # logical area (implemented via ui helpers currently)
docs/
release/
```

## Build in Visual Studio (recommended)

1. `File -> Open -> Folder...`
2. Open `FastMediaExplorer` root.
3. Select config `x64-Debug` or `x64-Release`.
4. Build and run target `FastMediaExplorer`.

## CLI Build

```powershell
cd C:\Users\roman\source\FastMediaExplorer
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
cmake --build build --config Release
```

## Clean Build

```powershell
cd C:\Users\roman\source\FastMediaExplorer
rmdir build -Recurse -Force
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## Troubleshooting

### If CMake cannot find Windows SDK

- Install/repair Visual Studio component: `Desktop development with C++`.
- Verify SDK in VS Installer (Windows 10/11 SDK).
- Regenerate build dir (`rmdir build ...` then `cmake -S . -B build ...`).

### If `signtool` is missing

Use path from Windows Kits, e.g.:
`C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe`

### If app is not responsive in huge folders

- Prefer `Details` mode for very large sets.
- Use narrower filters before enabling recursive scan.

## Performance Characteristics

- `10k+ files`: supported with virtual list and async pipeline.
- `100k+ files`: usable with strict filtering + progressive rendering; dedicated index cache is planned.

## Release Pipeline

1. build
2. sign
3. verify
4. zip
5. checksums
