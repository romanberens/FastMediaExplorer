# Architecture

## Architecture Priorities

- Lightweight native UI.
- Responsiveness on large media sets.
- Clear separation of concerns.
- Async-safe state transitions.

## Layers

### `core/`

Responsibilities:
- filesystem scan and media typing,
- query engine (search/filter/sort),
- duplicate analysis engine.

Key modules:
- `FileIndexer`
- `MediaQuery`
- `DuplicateAnalysis`

### `ui/`

Responsibilities:
- WinAPI window/control lifecycle,
- command routing,
- rendering orchestration,
- background worker coordination.

Modules:
- `main_window.cpp` (orchestrator)
- `list_panel.cpp`
- `thumb_service.cpp`

### `media/` (logical area)

Responsibilities:
- image/video thumbnail generation,
- image preview loading/scaling.

## Core Data Model (current + target)

Current runtime model:

```cpp
struct MediaFile {
    std::wstring full_path;
    std::wstring name;
    std::uintmax_t size;
    std::filesystem::file_time_type last_write_time;
    MediaType type;
};
```

Planned extended model:

```cpp
struct MediaFile {
    std::wstring full_path;
    std::wstring name;
    uintmax_t size;

    std::filesystem::file_time_type created_time;
    std::filesystem::file_time_type last_write_time;

    MediaType type;

    uint64_t hash = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};
```

## Master Index Pattern

Target flow:
- `scan -> master_index`
- `query -> filtered view`

Model:

```cpp
std::vector<MediaFile> master_index;
std::vector<size_t> filtered_view;
```

## Thumbnail Queue Priority

Priority levels:
1. visible rows,
2. near viewport,
3. background prefetch.

## Duplicate Analysis Model

Current structure in code:
- groups of file indices + group size metadata.

Target enriched structure:

```cpp
struct DuplicateGroup {
    uint64_t group_id;
    size_t file_count;
    uint64_t reclaimable_size;

    std::vector<int> file_indices;
};
```

## Duplicate Policy Engine (planned)

```cpp
enum class KeepPolicy
{
    Newest,
    Oldest,
    ShortestPath,
    LongestPath,
    FirstSeen
};
```

## Safe Delete Strategy (planned)

Delete should default to recycle bin, not hard delete.

WinAPI target:
- `IFileOperation`
- `FOF_ALLOWUNDO`

## Threading Model

- UI thread: interaction/render.
- Scan worker: folder traversal.
- Thumbnail worker: thumbnail generation queue.
- Duplicate worker: duplicate analysis.

## Performance Mechanisms

- `LVS_OWNERDATA` virtual list.
- async scanning.
- generation tokens.
- thumbnail queue + cache.
- LRU memory cap.
- adaptive throttle.
