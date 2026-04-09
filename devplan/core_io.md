# IO Abstractions and Filesystem — COMPLETE

**Phase:** 2
**Dependencies:** Phase 1

All IO abstractions, filesystem utilities, in-memory IO, the resource filesystem (cirf with `:/.PROMEKI/` built-in set), and the File DIO helpers (`writeBulk`, `sync`, `writev`, `preallocate`, `setDirectIO` consistency fix) are implemented, tested, and merged. See git history for details.

---

## Known Issues

- **`readBulk` and non-blocking file descriptors.** The `readFull()` helper loops on partial reads but treats EAGAIN as a fatal error (returns -1). On a non-blocking fd this means (1) the read fails instead of indicating "try again", and (2) if EAGAIN occurs after some bytes have already been read in a multi-portion transfer (head/DIO/tail), those bytes are lost. The `readFromDevice()` and `write()` paths handle this correctly. A fix would require `readFull()` to distinguish EAGAIN from real errors and to track partial progress across portions.

---

## Windows File stub

Tracked in `fixme.md`. The Windows `#ifdef` branch of `src/core/file.cpp` is a stub and needs a real `CreateFile`/`ReadFile`/`WriteFile` implementation.
