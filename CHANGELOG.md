# Changelog

All notable changes to this project are documented in this file.

This project follows the versioning policy in `VERSIONING_POLICY.md`.

## 0.3.0-alpha

### Added
- Root folder switch via drag and drop onto the main window.
- `File -> Open folder...` menu action with system folder picker.
- Keyboard shortcut `Ctrl+O` for opening a new root folder.

### Changed
- Unified root switching path for startup, menu action, and drag and drop.
- Tree root replacement now triggers immediate async scan in the same workflow.

### Fixed
- Global COM initialization lifecycle in app runtime (`RunApp` init/uninit).
- Thumbnail queue/cache reset on root change to avoid stale work from previous folders.
- Duplicate analysis state reset on root change to prevent stale duplicate indicators.
- Multi-drop handling now selects the first dropped folder and ignores the rest.

### Docs
- Added formal versioning rules in `VERSIONING_POLICY.md`.
- Updated docs and README references for versioning and changelog.
- Added version decision record in `docs/VERSION_DECISION.md`.

## 0.2.0-alpha

### Added
- Initial alpha baseline of Fast Media Explorer workflow.
- Async folder scanning, virtual list, filters, preview, thumbnails, duplicate analysis.

### Docs
- Added and formalized versioning policy (`VERSIONING_POLICY.md`).
