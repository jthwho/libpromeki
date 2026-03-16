# IO Abstractions and Filesystem

**Phase:** 2
**Dependencies:** Phase 1 (Mutex, Future for async patterns)
**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

**Maintenance note:** Completed items are removed from this document once merged. Only retain completed items when they provide context needed by a future phase in this same document. If something is done, trust the code and git history as the source of truth.

---

## Completed

IO abstractions, filesystem utilities, and in-memory IO are all implemented, tested, and merged: IODevice, BufferedIODevice, FilePath, Dir, File (BufferedIODevice subclass), FileInfo, Process, BufferIODevice, Buffer size model, Error enhancements, Terminal error reporting, StringIODevice (seekable IODevice over String*), FileIODevice (stdio FILE* adapter with optional ownership via OwnsFile flag and takeFile()).

FileFormatFactory\<Product\> generic factory template, AudioFileFactory migration to FileFormatFactory\<AudioFile\>, and AudioFile IODevice refactor (sf_open_virtual + factory migration) are all implemented, tested, and merged. computeDesc() now properly translates SF_INFO format/subtype/endian into AudioDesc::DataType for reading. File::pos() correctly accounts for BufferedIODevice read-ahead so that sf_open_virtual callbacks (and all other users of File) see accurate logical positions.

### Known Issues

- **FIXME: readBulk and non-blocking file descriptors.** The `readFull()` helper used by `readBulk()` loops on partial reads but treats EAGAIN as a fatal error (returns -1). On a non-blocking fd, this means: (1) the read fails instead of indicating "try again", and (2) if EAGAIN occurs after some bytes have already been read in a multi-portion transfer (head/DIO/tail), those bytes are lost. The `readFromDevice()` and `write()` paths handle this correctly (they report the real errno via `Error::syserr()`), so callers can detect `TryAgain` and retry. A fix for `readBulk` would require `readFull()` to distinguish EAGAIN from real errors and to track partial progress across portions so nothing is lost on retry.
