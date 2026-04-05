# IO Abstractions and Filesystem — COMPLETE

**Phase:** 2
**Dependencies:** Phase 1

All IO abstractions, filesystem utilities, and in-memory IO implemented, tested, and merged. See git history for details.

### Known Issues

- **FIXME: readBulk and non-blocking file descriptors.** The `readFull()` helper used by `readBulk()` loops on partial reads but treats EAGAIN as a fatal error (returns -1). On a non-blocking fd, this means: (1) the read fails instead of indicating "try again", and (2) if EAGAIN occurs after some bytes have already been read in a multi-portion transfer (head/DIO/tail), those bytes are lost. The `readFromDevice()` and `write()` paths handle this correctly (they report the real errno via `Error::syserr()`), so callers can detect `TryAgain` and retry. A fix for `readBulk` would require `readFull()` to distinguish EAGAIN from real errors and to track partial progress across portions so nothing is lost on retry.
