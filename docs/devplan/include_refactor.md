# Include Path Refactor

## Goal

Refactor all `#include` directives to use the `<promeki/[lib]/header.h>` convention, making it explicit which library each header belongs to. This eliminates accidental dependencies on libraries the consumer isn't linking against.

**Current:** `#include <promeki/header.h>` (flat namespace, library membership is ambiguous)
**Target:** `#include <promeki/core/header.h>`, `#include <promeki/proav/header.h>`, `#include <promeki/tui/header.h>`, etc.

## Rationale

- A flat include namespace hides which library a header belongs to. Users can unknowingly include a header from `promeki-proav` while only linking `promeki-core`, leading to confusing link errors.
- Explicit paths make library boundaries visible at the `#include` site. Yes, it's a bit more typing, but it saves time in practice by preventing mistakes and making dependencies self-documenting.
- Aligns with conventions used by well-structured C++ libraries (e.g., `<boost/asio/ip/tcp.hpp>`, `<Qt/QtCore/QString>`).

## Library-to-Directory Mapping

| Library | Include prefix | Source dir |
|---|---|---|
| `promeki-core` | `promeki/core/` | `include/promeki/core/` |
| `promeki-proav` | `promeki/proav/` | `include/promeki/proav/` |
| `promeki-tui` | `promeki/tui/` | `include/promeki/tui/` |
| `promeki-music` | `promeki/music/` | `include/promeki/music/` |
| `promeki-network` | `promeki/network/` | `include/promeki/network/` |

## Implementation

### Step 1: Directory restructure

- [ ] Create subdirectories under `include/promeki/`: `core/`, `proav/`, `tui/`, `music/`
- [ ] Move each header into the appropriate subdirectory based on which library it belongs to
- [ ] Update CMakeLists.txt to reflect the new header locations

### Step 2: Update all internal includes

- [ ] Update every `#include` in the codebase (headers and sources) to use the new paths
- [ ] Update every `#include` in test files
- [ ] Update every `#include` in demos and utilities

### Step 3: Update Doxygen `@file` paths

- [ ] Audit and fix `@file` directives in moved headers to reflect new paths

### Step 4: CMake install rules

- [ ] Ensure `install(DIRECTORY ...)` rules preserve the `promeki/[lib]/` structure so downstream consumers get the correct layout

### Step 5: Verify

- [ ] Full build passes (`build`)
- [ ] All test executables pass
- [ ] Demos compile and run
- [ ] Doxygen generates without warnings on moved files

## Compatibility shims (optional, time-limited)

If desired, old-path forwarding headers can be left temporarily at `include/promeki/header.h` containing:

```cpp
#pragma once
#warning "Use <promeki/core/header.h> instead of <promeki/header.h>"
#include <promeki/core/header.h>
```

These should be removed after one release cycle. This is optional and can be skipped if there are no external consumers yet.

## Notes

- This is a cross-cutting refactor that touches every file. Best done early (before many new files are added by other phases) or as a dedicated pass.
- The `network` library doesn't exist yet, but its include prefix is reserved here for consistency when it is created (Phase 3).
