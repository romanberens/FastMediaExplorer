# Test Checklist (Manual Regression)

## Core

- [ ] App starts and UI layout is correct.
- [ ] Tree selection updates list.
- [ ] Recursive scan mode works.

## Query

- [ ] Search debounce works.
- [ ] Type filter works.
- [ ] Date filter works.
- [ ] Custom range works.
- [ ] Sorting works for all columns.

## Rendering

- [ ] Details mode stable on large folders.
- [ ] Icons mode shows thumbnails.
- [ ] Overlay/progress behaves correctly and disappears when done.

## Preview and Actions

- [ ] Image preview renders on selection.
- [ ] Non-image clears preview.
- [ ] `Enter` open, `Delete` confirm/remove.
- [ ] Context menu actions work.

## Duplicate Analysis

- [ ] Tools -> Quick (size only).
- [ ] Tools -> Exact (hash).
- [ ] Tools -> Exact + name/ext.
- [ ] Last result re-applies filter.
- [ ] Clear result restores normal view.

## About / License

- [ ] About text reflects `v0.2 alpha` and MIT.
- [ ] `LICENSE` file is present and correct.

## Release Validation

- [ ] Clean release build succeeds.
- [ ] Binary signed.
- [ ] `signtool verify /pa /v` passes.
- [ ] ZIP and SHA256 checksums generated.
