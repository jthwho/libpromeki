
## Landed (2026-06-04)

- **`BufferedIODevice` write buffering** — `setWriteBuffered` / `isWriteBuffered` /
  `setWriteBufferCapacity` / `writeBufferCapacity` / `bufferedBytesPending()`.  Abstract
  `writeToDevice(data, maxSize)` virtual added; all concrete subclasses (`File`, test
  harness `BufferedMemoryDevice`) migrated from overriding `write()` to `writeToDevice()`.
  Auto-flush at capacity; single large write bypasses buffer; `setWriteBuffered(false)`
  flushes before switching.  Motivating use-case: `AnsiStream` emitting multi-byte
  escape sequences one piece at a time via `operator<<` without a system call per piece.
  Tests: 6 new cases in `tests/unit/bufferediodevice.cpp`.

- **`File(FileHandle, OpenMode, ownsHandle)` fd-adoption constructor** — wraps an
  existing OS file descriptor (tty, pipe, inherited fd, one of the standard streams)
  with the full `File` interface including optional write buffering.  `ownsHandle=false`
  leaves the descriptor open on `File::close()`/destruction.  `pos()` accounts for
  pending buffered bytes.  `writeToDevice()` override replaces the old `write()` override.
  Tests: 3 new POSIX pipe-based cases in `tests/unit/file.cpp` (non-owned + owned).

---

## Known Issues

- **`readBulk` and non-blocking file descriptors.** The `readFull()` helper loops on partial reads but treats EAGAIN as a fatal error (returns -1). On a non-blocking fd this means (1) the read fails instead of indicating "try again", and (2) if EAGAIN occurs after some bytes have already been read in a multi-portion transfer (head/DIO/tail), those bytes are lost. The `readFromDevice()` and `write()` paths handle this correctly. A fix would require `readFull()` to distinguish EAGAIN from real errors and to track partial progress across portions.

---

## Windows File stub

Tracked in [`fixme/windows-file.md`](../fixme/windows-file.md). The Windows `#ifdef` branch of `src/core/file.cpp` is a stub and needs a real `CreateFile`/`ReadFile`/`WriteFile` implementation.
