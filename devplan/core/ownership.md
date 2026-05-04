# Heap Ownership Migration — Phase C

**Standards:** All fixes must follow `CODING_STANDARDS.md`, specifically the [Heap Ownership and Pointer Types](../CODING_STANDARDS.md#heap-ownership-and-pointer-types) section.  Every change requires updated unit tests.  See `devplan/README.md` for full requirements.

---

## Context

Phases A and B of the heap-ownership migration eliminated `std::unique_ptr` and most raw `new`/`delete` owning patterns from the tree in favor of `SharedPtr<T>` / `UniquePtr<T>`.  The items below are the remainder — each one hit a real design constraint that made a mechanical swap to `UniquePtr` either impossible or incorrect.  They are intentionally held for a dedicated pass so the options can be evaluated case by case instead of being force-fit.

Rough order of attack (most value / least risk first):

1. Trivial wins: `audiotestpattern.cpp _chanGens`, `terminal.cpp _origState`, `sdleventpump.cpp` event transfer.
2. Factory cleanup normalization: `imagefileio_jpeg.cpp`, `imagefileio_jpegxs.cpp`, `mediapipeline.cpp`, `mediapipelineplanner.cpp`.
3. CoW + UniquePtr conflict: `AudioFile::Impl` / `QuickTime::Impl` family (needs a library-wide decision).
4. Parent-child overlap: `DebugMediaFile::_file` (design choice between two ownership models).
5. Global-state migrations: `logger.cpp`, `signalhandler.cpp` (touchier; schedule last).

---

## 1. AudioFile::Impl + QuickTime::Impl — SharedPtr CoW vs. UniquePtr member

**Files:**
- `include/promeki/audiofile.h:198-199` (`IODevice *_device`, `bool _ownsDevice`)
- `src/proav/audiofile.cpp:61-66`, `src/proav/audiofile_libsndfile.cpp:240-292`
- `include/promeki/quicktime.h:451-452` (`IODevice *_device`, `bool _ownsDevice`)
- `src/proav/quicktime.cpp:21-26`
- `src/proav/quicktime_writer.h:175` (`File *_file`), `src/proav/quicktime_writer.cpp:308-312, 405-411`
- `src/proav/quicktime_reader.h:197` (`File *_metaFile`), `src/proav/quicktime_reader.cpp:104-108, 157-171`

**Problem:** `AudioFile::Impl` and `QuickTime::Impl` (plus its `QuickTimeReader` / `QuickTimeWriter` subclasses) are held inside their owning classes via `SharedPtr<Impl>` with copy-on-write enabled.  `PROMEKI_SHARED_BASE` emits a `_promeki_clone()` method that calls `promekiCloneOrAbort<BASE>(this)`, which aborts at runtime for non-copyable types.  With CoW enabled, copying the outer holder and then calling `modify()` on the copy would reach that abort path — so the Impl must remain copy-constructible.  Adding a `UniquePtr<IODevice>` / `UniquePtr<File>` member deletes the copy constructor and would hit the abort at runtime.

The migration was attempted in Phase B, compilation failed with:
```
error: use of deleted function 'UniquePtr<T>::UniquePtr(const UniquePtr<T>&)'
```
and the changes were reverted.  Left-behind patterns: `_device` + `_ownsDevice` flag (AudioFile/QuickTime), raw `_metaFile` / `_file` (QuickTimeReader/Writer).

**Options:**

- **(a) Disable CoW on the holder.**  Change `SharedPtr<Impl> d;` to `SharedPtr<Impl, /*CopyOnWrite=*/false> d;` in `AudioFile` and `QuickTime`.  With CoW disabled, `modify()` is a direct mutable accessor — `_promeki_clone()` is never reached.  Because `PROMEKI_SHARED_BASE` now uses `promekiCloneOrAbort<BASE>`, a non-copyable Impl compiles fine (the static_assert is gone); the abort path is just never exercised.  Subclass copy constructors can still be `= delete` since they're unreachable.  Effort: low.  Risk: need to verify no caller relies on CoW detach (every `d.modify()->foo()` call site becomes a plain "modify in place" — which is likely the intended behavior anyway, since opening an audio file is state-ful).
- **(b) Manual clone that skips the UniquePtr member.**  Override `_promeki_clone` in Impl to copy everything except the device pointer, and rebuild the device on demand.  Effort: medium.  Semantically questionable — a cloned Impl with a missing device is a half-constructed object.  Not recommended.
- **(c) Convert `AudioFile` / `QuickTime` to UniquePtr-on-the-outside.**  The outer class is really single-owner (you don't share an open audio file between threads via refcount — you hand off the `Audio::Ptr` samples it produces).  A deeper refactor would replace `SharedPtr<Impl>` with `UniquePtr<Impl>` on the outer class, and audit the copy semantics of `AudioFile` / `QuickTime` themselves.  Effort: high.  Scope: touches every user of these types.

Recommendation: **(a)**.  CoW on a stateful I/O object was never the right default; disabling it unblocks the Phase B cleanup with minimal churn.

**Tasks:**

- [ ] Change `AudioFile::d` to `SharedPtr<Impl, /*CopyOnWrite=*/false>`.  Verify all `d.modify()->...` call sites still make sense (they should — `modify()` without CoW is just a mutable accessor).
- [ ] Change `QuickTime::d` similarly.
- [ ] Migrate `AudioFile::Impl::_device` / `_ownsDevice` to the dual-pointer pattern (`IODevice *_device` + `IODevice::UPtr _ownedDevice`).  Apply to `audiofile_libsndfile.cpp` call sites.
- [ ] Migrate `QuickTime::Impl::_device` / `_ownsDevice` similarly.
- [ ] Migrate `QuickTimeWriter::_file` to `File::UPtr` (the subclass inherits the CoW-disabled holder, so its copy ctor no longer needs to compile either).  Remove manual `delete _file` from `QuickTimeWriter::~QuickTimeWriter` and `close()`.
- [ ] Migrate `QuickTimeReader::_metaFile` similarly.  Update `activeDevice()` to return `_metaFile.get()`.
- [ ] Rerun full test suite; audit for any SharedPtr<Impl> detach semantics that were actually in use.

---

## 2. DebugMediaFile::_file — ObjectBase parent/child vs. smart pointer ownership

**File:** `src/proav/debugmediafile.cpp:444, 450, 455, 479, 490, 495, 499, 515`; `include/promeki/debugmediafile.h:249`

**Problem:** `DebugMediaFile` allocates its `File *_file` member with the **widget parenting idiom** — `new File(filename, this);` — so the `ObjectBase` parent/child destruction tree already destroys the `File` when `DebugMediaFile` is destroyed.  But the close path also manually deletes `_file` to release the OS handle before `DebugMediaFile` itself goes away.

Wrapping `_file` in `File::UPtr` would cause a **double-free**: the UniquePtr destructor deletes the File, then the ObjectBase parent destructor iterates children and tries to delete it again.

The current code works but encodes the ownership contract in two places (the `new File(filename, this)` call and the `delete _file` in close), which is exactly the pattern this migration is trying to eliminate.

**Options:**

- **(a) Drop the ObjectBase parent link; let UniquePtr own the File.**  Change `new File(filename, this)` to `File::UPtr::create(filename)`.  `File::UPtr _file;` handles destruction.  The `File` no longer shows up in `DebugMediaFile`'s child list — minor loss of introspection via `ObjectBase::children()`, but probably irrelevant for a PMDF file handle.
- **(b) Keep the parent link; add `_file.release()` before letting ObjectBase reclaim.**  Complex, error-prone, and defeats the purpose of smart ownership.
- **(c) Change `close()` to call `_file->close()` without deleting, and let the parent-child destructor finalize.**  Leaves an unused `File` object alive for the remaining life of `DebugMediaFile`, which may not be long.  Slight resource hold.

Recommendation: **(a)**.  Parent/child makes sense for widget-like trees where `ObjectBase::findChild` or signal propagation is useful; it's overkill for an owned I/O device in a composed class.

**Tasks:**

- [ ] Change `_file` to `File::UPtr` in `debugmediafile.h`.
- [ ] Replace `_file = new File(filename, this);` with `_file = File::UPtr::create(filename);` in `debugmediafile.cpp:444`.
- [ ] Replace every `delete _file; _file = nullptr;` with `_file.clear();` (lines 450, 455, 479, 490, 495, 499, 515).
- [ ] Check whether any code walks `DebugMediaFile`'s child list expecting to find the File; fix if so.
- [ ] Verify the PMDF round-trip tests still pass (`tests/unit/debugmediafile.cpp`).

---

## 3. Terminal::_origState — POSIX `::termios` struct

**File:** `src/core/terminal.cpp:63`

**Problem:** `_origState = new ::termios;` allocates a POSIX C struct (not a promeki class) and later `delete _origState`.  This is a trivial case — there's no inheritance, no copy semantics, no ownership ambiguity.

**Options:**

- **(a) `UniquePtr<::termios>`.**  Works fine — UniquePtr is type-agnostic.  Cleanest option.
- **(b) Inline the struct directly in `Terminal`'s private section.**  Eliminates the heap allocation entirely.  Requires changing the member from a pointer to a value, so all `*_origState` / `_origState->` accesses need updating.

Recommendation: **(b)** if the Terminal object's size isn't a concern (`::termios` is on the order of 60 bytes); **(a)** otherwise.  The inline form removes the allocation entirely and avoids having a `UniquePtr<::termios>` visible in the Terminal class.

**Tasks:**

- [ ] Inspect `Terminal`'s use of `_origState` and pick (a) or (b).
- [ ] If (b): change `::termios *_origState` to `::termios _origState;` in `terminal.h`, update all usages (`*_origState` → `_origState`, `_origState->` → `_origState.`, `new` / `delete` removed).
- [ ] If (a): change to `UniquePtr<::termios> _origState;` and `_origState = UniquePtr<::termios>::create();`.  Wherever the raw struct is passed to `tcsetattr`/`tcgetattr`, use `_origState.ptr()`.
- [ ] Run `unittest-promeki -tc='Terminal*'` (if any; otherwise verify interactively).

---

## 4. sdleventpump.cpp — event ownership transfer

**File:** `src/sdl/sdleventpump.cpp:217, 239`

**Problem:** The SDL event pump allocates SDL-side mouse events as raw `new MouseEvent(...)`, hands them to an `ObjectBase` event handler, and deletes them if the handler didn't consume them.  This is a classic "ownership transfer on accept" pattern.

**Options:**

- **(a) Migrate to `UniquePtr<MouseEvent>`.**  The handler API needs to be able to take ownership — either via `MouseEvent::UPtr &&` or by releasing from the UniquePtr after a successful accept.  Since event dispatch crosses into `ObjectBase::event()` which is a framework API, the cleaner signature is probably to pass the raw pointer (current behavior) but have the pump hold a `UniquePtr<MouseEvent>` locally, then `release()` on accept.
- **(b) Leave as-is and document the pattern.**  Not every heap allocation should be migrated; this is an ownership-transfer protocol that works and is localized to one file.

Recommendation: **(a)**, local UniquePtr that releases on accept.  Keeps the library's no-manual-delete invariant without changing the event API.

**Tasks:**

- [ ] In `sdleventpump.cpp`, wrap each `new MouseEvent(...)` in a `UniquePtr<MouseEvent>`.
- [ ] On successful `postEvent` (event accepted), call `release()`; on rejection, let the UniquePtr destructor clean up.
- [ ] No destructor changes needed — ownership scope is the local stack frame.

---

## 5. imagefileio_jpeg.cpp / imagefileio_jpegxs.cpp — factory conditional cleanup

**Files:** `src/proav/imagefileio_jpeg.cpp:348, 359, 365`; `src/proav/imagefileio_jpegxs.cpp:351, 362, 368`

**Problem:** Both files follow the pattern:
```cpp
auto *enc = new JpegEncoder(...);
if(cfg-error) { delete enc; return err; }
if(init-error) { delete enc; return err; }
return enc;   // success: caller takes ownership
```

This works but each `delete enc;` on an error branch is easy to miss.

**Options:**

- **(a) Migrate to `UniquePtr<Encoder>`; `release()` on success.**  The function returns a raw pointer (contract defined by callers), so the body uses a local UniquePtr and `release()` on the final success path.
- **(b) Change the return type to `Encoder::UPtr`.**  Cleaner, but changes the signature.  May cascade into MediaIOTask_ImageFile's consumption side.

Recommendation: **(a)** for now.  Once the broader `ImageFile` encoder/decoder factory API is audited, consider **(b)** as a follow-up.

**Tasks:**

- [ ] `imagefileio_jpeg.cpp`: wrap `new JpegEncoder(...)` in `UniquePtr<JpegEncoder>` locally; `release()` on the success path.  Remove the three `delete enc;` calls.
- [ ] `imagefileio_jpegxs.cpp`: same treatment for its encoder factory.
- [ ] Round-trip tests already cover the happy path; verify the error paths via existing or new tests.

---

## 6. mediapipeline.cpp / mediapipelineplanner.cpp — `delete io` on cleanup failure

**Files:** `src/proav/mediapipeline.cpp:142`; `src/proav/mediapipelineplanner.cpp:63, 575`

**Problem:** Same factory pattern as #5 — local construction of a `MediaIO *` that is either adopted into the pipeline (ownership transfers) or deleted on failure.

**Options:** Same as #5.  Use a local `MediaIO::UPtr` (the alias was added in Phase B); `release()` on adopt-success.

**Tasks:**

- [ ] `mediapipeline.cpp:142`: identify the surrounding factory; wrap in `MediaIO::UPtr`, remove manual `delete`.
- [ ] `mediapipelineplanner.cpp:63, 575`: same.
- [ ] Pipeline planner tests cover the happy paths; ensure error paths are exercised (`MediaPipelinePlanner_OpenFailurePropagates` or similar).

---

## 7. audiotestpattern.cpp — `List<AudioGen *>` per-element delete

**File:** `src/proav/audiotestpattern.cpp:66` (`delete _chanGens[i]`), plus the matching `_chanGens.pushToBack(new AudioGen(...))` in the configure path; `include/promeki/audiotestpattern.h` for the member declaration.

**Problem:** `_chanGens` is a `List<AudioGen *>` of owned pointers that `clearGenerators()` walks and deletes.  Classic container-of-raw-owners.

**Options:**

- **(a) `List<UniquePtr<AudioGen>>`.**  Each list element owns its generator.  `clear()` on the list destroys all generators automatically; `clearGenerators()` collapses to `_chanGens.clear();`.  Changes the push path to `_chanGens.pushToBack(UniquePtr<AudioGen>::create(...));` and any read path from `_chanGens[i]->foo()` (unchanged) or `_chanGens[i]` (now a UniquePtr, so use `.ptr()` or let auto conversions handle it).
- **(b) Define `AudioGen::UPtr` and use `List<AudioGen::UPtr>`.**  Same mechanism with a cleaner spelling per the template-alias policy.

Recommendation: **(b)** — follows the `UPtr` alias convention.

**Tasks:**

- [ ] Add `using UPtr = UniquePtr<AudioGen>;` to `AudioGen`'s `public:` section.
- [ ] Change `List<AudioGen *> _chanGens;` to `List<AudioGen::UPtr> _chanGens;` in `audiotestpattern.h`.
- [ ] Replace `new AudioGen(...)` push sites with `AudioGen::UPtr::create(...)` (or `::takeOwnership` if the factory still hands raw).
- [ ] Collapse `clearGenerators()` body to `_chanGens.clear();`.  Keep the function for symmetry if it's already in the public API; otherwise remove.
- [ ] Update any `_chanGens[i]->member` usages — `operator->` on `UPtr` forwards transparently, so no change should be needed at call sites.
- [ ] Run `unittest-promeki -tc='AudioTestPattern*'`.

---

## 8. logger.cpp — singleton self-deletion + owned devices

**File:** `src/core/logger.cpp:305, 306, 328, 337`

**Problem:** The logger singleton (`Logger::self()`) contains:
- `delete logFile;` — an owned `File *` for rolling log output.
- `delete self;` — the singleton itself, on shutdown.
- `delete existing;` — when swapping in a replacement IODevice, the previous owned one is deleted.
- `delete dev;` — parameter cleanup paths.

Self-deletion is a specific pattern (`delete this` or equivalent via a held pointer) that smart pointers can't cleanly model.  `logFile` / `existing` / `dev` follow the generic owned-pointer pattern.

**Options:**

- **(a) Incremental: migrate owned devices to `IODevice::UPtr`; leave the singleton self-delete alone.**  `logFile` becomes `File::UPtr`.  Replacement-device swaps use `std::swap` or move-assign and let the old UniquePtr's destructor handle cleanup.  The `delete self;` in singleton teardown stays as-is — singletons are a special case.
- **(b) Full rework into a non-`delete-self` singleton.**  Use a static local instance instead of `new`/`delete`, eliminating the self-delete.  Larger surgery; affects shutdown ordering.

Recommendation: **(a)** for this pass.  Option (b) is a separate cleanup if the singleton pattern itself is to be revisited.

**Tasks:**

- [ ] Audit `src/core/logger.cpp`: identify which pointer members correspond to which `delete` lines.
- [ ] Migrate the owned `File` / `IODevice` pointers to UniquePtr per the dual-pointer pattern if there's any non-owning / external case, otherwise direct `IODevice::UPtr`.
- [ ] Leave the singleton self-delete annotated with a comment explaining why it stays raw.
- [ ] Run logger tests.

---

## 9. signalhandler.cpp — global watcher pointer + pipe fd cleanup

**File:** `src/core/signalhandler.cpp:452` (`delete g_watcher`), plus the surrounding `g_wakePipe[2]` fd management.

**Problem:** Process-global signal watcher held in a file-scope `Watcher *g_watcher = nullptr;` that's allocated at install time and deleted on teardown.  Also manages two file descriptors via `::pipe` / `::close` — not relevant to the UniquePtr migration (fds are not heap allocations).

**Options:**

- **(a) `UniquePtr<Watcher> g_watcher;` at namespace scope.**  Works; the static destructor runs at program teardown.  Introduces static-initialization-order concerns if any static destructor runs before `g_watcher`'s.  Low risk in practice because the signal handler is installed at `Application` startup, not during static init.
- **(b) Keep as raw pointer, document as "global state, lifetime matches process."**  Conservative; defensible given the teardown rarely matters (most programs exit without tearing down the signal handler).

Recommendation: **(a)** for consistency, but add a comment explaining the static-destruction ordering concern.

**Tasks:**

- [ ] Change `Watcher *g_watcher = nullptr;` to `UniquePtr<Watcher> g_watcher;`.
- [ ] `g_watcher = new Watcher(...);` → `g_watcher = UniquePtr<Watcher>::create(...);`.
- [ ] `delete g_watcher; g_watcher = nullptr;` → `g_watcher.clear();`.
- [ ] Leave the `::pipe` / `::close` fd management untouched — fds are not heap.
- [ ] Run `unittest-promeki -tc='SignalHandler*'`.

---

## 10. ObjectBase parent/child `delete child;` — leave as-is

**File:** `include/promeki/objectbase.h:426`

**Problem:** `ObjectBase::~ObjectBase` iterates `_children` and deletes each raw pointer.  This is the fundamental ownership mechanism for functional objects — by design, children are raw pointers owned by the parent's child list.

**Decision:** Do not migrate.  ObjectBase's parent/child tree is a deliberate design; wrapping children in smart pointers would duplicate ownership (parent already owns them) and break the existing lifetime contract.  `CODING_STANDARDS.md` already documents this as the one case where raw pointers are the ownership model.

**Tasks:**

- [ ] None.  This entry is documented here so future audits don't flag it as a migration target.

---

## Non-goals

- `include/promeki/sharedptr.h` internal `delete _data;` / `delete _object;` — part of SharedPtr's own implementation; intentionally raw.
- Doc-example `delete io;` snippets in various MediaIOTask headers (e.g. `mediaiotask_csc.h:71`) — these are illustrative pre-migration examples that should be updated to the `MediaIO::UPtr` spelling on a docs-pass, but not heap-ownership bugs per se.
- Any `std::unique_ptr<char, void(*)(void*)>` custom-deleter pattern (currently `src/core/system.cpp:33`) — UniquePtr does not support custom deleters; `std::unique_ptr` stays.

---

## Completion criteria

- All items above either migrated or explicitly marked "won't migrate" with rationale.
- `grep -rn '^\s*delete\s\+_[a-zA-Z0-9_]\+' src/ include/` returns only ObjectBase's child-destruction path, SharedPtr internals, and documented exceptions.
- `grep -rn 'new\s\+[A-Z]' src/ include/` free of owning `new` calls outside `UniquePtr::create` / `takeOwnership` / SharedPtr factories.
- All existing unit tests pass; no new memory leaks under valgrind / ASan.
- CODING_STANDARDS.md's Heap Ownership section is the single source of truth for the final state.
