# Current Status

## Version Snapshot

- Product: Fast Media Explorer
- Version: `v0.3.0-alpha`
- License: MIT

## Implemented

- Async scan (recursive/non-recursive).
- Root switch by drag & drop folder onto app window.
- `File -> Open folder...` using system folder picker.
- Virtualized ListView (`LVS_OWNERDATA`).
- Search + filters (type/date/custom range/size presets).
- Details/Icons views.
- Async thumbnails with queue priority and LRU cache.
- Preview panel for images.
- Context menu and keyboard shortcuts.
- Duplicate analysis from `Tools` menu in 3 modes.
- About dialog with creator/company/product info.
- Portable signed release packaging.

## Duplicate Analysis Notes

- `Exact hash` mode prioritizes performance over cryptographic guarantees.
- Current result presentation applies as filtered list.
- Dedicated grouped duplicate panel is planned.

## Known Limits

- No persistent index cache yet.
- No recycle-bin-safe batch duplicate cleanup yet.
- No advanced metadata extraction (EXIF/camera/resolution) yet.

## Next

1. Duplicate groups panel + keep policy engine.
2. Recycle bin delete path via `IFileOperation`.
3. Export duplicate reports (CSV/JSON).
