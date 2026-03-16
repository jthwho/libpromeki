# IO Abstractions and Filesystem

**Phase:** 2
**Dependencies:** Phase 1 (Mutex, Future for async patterns)
**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

**Maintenance note:** Completed items are removed from this document once merged. Only retain completed items when they provide context needed by a future phase in this same document. If something is done, trust the code and git history as the source of truth.

---

## Completed

IO abstractions, filesystem utilities, and in-memory IO are all implemented, tested, and merged: IODevice, BufferedIODevice, FilePath, Dir, File (BufferedIODevice subclass), FileInfo, Process, BufferIODevice, Buffer size model, Error enhancements, Terminal error reporting, StringIODevice (seekable IODevice over String*), FileIODevice (stdio FILE* adapter with optional ownership via OwnsFile flag and takeFile()).

### Known Issues

- **FIXME: readBulk and non-blocking file descriptors.** The `readFull()` helper used by `readBulk()` loops on partial reads but treats EAGAIN as a fatal error (returns -1). On a non-blocking fd, this means: (1) the read fails instead of indicating "try again", and (2) if EAGAIN occurs after some bytes have already been read in a multi-portion transfer (head/DIO/tail), those bytes are lost. The `readFromDevice()` and `write()` paths handle this correctly (they report the real errno via `Error::syserr()`), so callers can detect `TryAgain` and retry. A fix for `readBulk` would require `readFull()` to distinguish EAGAIN from real errors and to track partial progress across portions so nothing is lost on retry.

---

## Refactor AudioFile to Use IODevice

`AudioFile` currently manages its own file I/O through the `Impl` pimpl pattern. Refactor so the `Impl` backends use `IODevice` (specifically `File`) for their underlying I/O, enabling future backends that read from network streams or memory buffers.

**Files:**
- [ ] Modify `include/promeki/proav/audiofile.h`
- [ ] Modify `src/audiofile.cpp`
- [ ] Modify `src/audiofile_libsndfile.cpp`
- [ ] Update `tests/audiofile.cpp`

**Implementation checklist:**
- [ ] Add `AudioFile` constructor/factory that accepts `IODevice *` instead of filename
- [ ] Refactor libsndfile backend to use `sf_open_virtual()` with IODevice callbacks
- [ ] Preserve existing filename-based API as convenience (creates internal File)
- [ ] Enables reading audio from network streams, memory buffers, etc. (Phase 4 pipeline use)
- [ ] Doctest: read/write via File IODevice, read from Buffer IODevice
