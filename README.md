# Fast Media Explorer

Built for people working with large media folders who need speed, clarity, and control.

Fast Media Explorer is a lightweight native Windows app for browsing, filtering, previewing, and analyzing large media collections without heavyweight catalog software.

## Why This Project Exists

FastMediaExplorer was created to solve a real workflow problem: browsing large media folders quickly.

It targets the gap between:
- Windows Explorer (simple, but limited for large media workflows),
- Adobe Bridge / Lightroom (powerful, but heavier).

## Key Features

- Async folder scanning (`current folder` / `recursive`).
- Switch root folder via drag & drop or `File -> Open folder...`.
- Virtual list view (`LVS_OWNERDATA`) for large result sets.
- Icon/details modes with async thumbnails.
- Image preview panel.
- Search + type/date filters (including custom date range).
- Duplicate analysis in multiple modes:
  - quick (size only),
  - exact hash,
  - exact hash + name/ext.
- Context menu and keyboard workflow (`Ctrl+F`, `F5`, `Enter`, `Delete`).

## Project Status

- Version: `v0.3.0-alpha`
- License: MIT

### What Works Now

- Core browsing and filtering workflow.
- Async scan and responsive UI on large folders.
- Thumbnail queue + cache + cancel.
- Duplicate analysis from application menu.
- Signed release packaging workflow.

### Planned Next

- Dedicated duplicate groups panel with keep-policy actions.
- Recycle-bin-safe batch delete.
- Persistent index cache.
- Export reports (CSV / JSON).

## Design Principles

- Native Windows UI.
- Predictable resource usage.
- Direct filesystem access.
- Scalable architecture.
- Minimal dependencies.

## Performance Design

- `LVS_OWNERDATA` virtualization for list scaling.
- Async scanning on worker thread.
- Generation tokens to ignore stale async results.
- Thumbnail queue with viewport priority.
- LRU thumbnail cache with memory limit.
- Adaptive thumbnail throttling.

## Screenshot

![Fast Media Explorer screenshot](docs/screenshots/app-main.png)

> Place your latest screenshot at `docs/screenshots/app-main.png`.

## Build and Run

See: [docs/BUILD_AND_RUN.md](docs/BUILD_AND_RUN.md)

## Documentation

- [docs/PROJECT_OVERVIEW.md](docs/PROJECT_OVERVIEW.md)
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
- [docs/CURRENT_STATUS.md](docs/CURRENT_STATUS.md)
- [docs/ROADMAP.md](docs/ROADMAP.md)
- [docs/PRODUCT_BACKLOG.md](docs/PRODUCT_BACKLOG.md)
- [docs/TEST_CHECKLIST.md](docs/TEST_CHECKLIST.md)
- [docs/VERSION_DECISION.md](docs/VERSION_DECISION.md)
- [VERSIONING_POLICY.md](VERSIONING_POLICY.md)
- [CHANGELOG.md](CHANGELOG.md)

## License

MIT License - see [LICENSE](LICENSE).
