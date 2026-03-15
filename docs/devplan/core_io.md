# IO Abstractions and Filesystem

**Phase:** 2
**Dependencies:** Phase 1 (Mutex, Future for async patterns)
**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

---

## IODevice Base Class — COMPLETE

Abstract ObjectBase-derived class — the common interface for files, sockets, pipes, serial ports. All methods are virtual so WASM backends can provide alternative implementations. Every blocking `waitFor*()` has a signal-based async path.

**Files:**
- [x] `include/promeki/core/iodevice.h`
- [x] `src/iodevice.cpp`
- [x] `tests/iodevice.cpp`

**Implementation checklist:**
- [x] Header guard, includes, namespace
- [x] Derive from `ObjectBase`, use `PROMEKI_OBJECT`
- [x] `enum OpenMode { NotOpen, ReadOnly, WriteOnly, ReadWrite }`
- [x] `virtual Error open(OpenMode mode) = 0`
- [x] `virtual Error close() = 0` — returns Error (not void)
- [x] `virtual bool isOpen() const = 0`
- [x] `OpenMode openMode() const` — returns current mode
- [x] `bool isReadable() const`, `bool isWritable() const`
- [x] `virtual int64_t read(void *data, int64_t maxSize) = 0`
- [x] `virtual int64_t write(const void *data, int64_t maxSize) = 0`
- [x] `virtual int64_t bytesAvailable() const` — default returns 0
- [x] `virtual bool waitForReadyRead(unsigned int timeoutMs = 0)` — default returns false
- [x] `virtual bool waitForBytesWritten(unsigned int timeoutMs = 0)` — default returns false
- [x] `virtual bool isSequential() const` — default returns false
- [x] `virtual Error seek(int64_t pos)` — default returns `Error::NotSupported`
- [x] `virtual int64_t pos() const` — default returns 0
- [x] `virtual Result<int64_t> size() const` — default returns `makeResult<int64_t>(0)`
- [x] `virtual bool atEnd() const` — default checks `pos() >= size()` (handles Result)
- [x] `PROMEKI_SIGNAL(readyRead)` — emitted when data available
- [x] `PROMEKI_SIGNAL(bytesWritten, int64_t)` — emitted after write completes
- [x] `PROMEKI_SIGNAL(errorOccurred, Error)` — emitted on error
- [x] `PROMEKI_SIGNAL(aboutToClose)` — emitted before close
- [x] Error state: `error()` returns last Error, `clearError()`, protected `setError()`

**Design note:** The original plan included a runtime option system (registerOptionType, setOption, option, onOptionChanged) on IODevice. This was removed in favor of direct setter/getter methods on concrete subclasses (e.g., `File::setDirectIO()`, `BufferedIODevice::setUnbuffered()`). Direct methods are simpler, type-safe, and avoid the Variant boxing overhead. Device-specific configuration belongs on the device class, not a generic key-value store.

### Doctest
- [x] Test via concrete subclass (MemoryIODevice: in-memory buffer IODevice)
- [x] Test signals: aboutToClose, bytesWritten
- [x] Test all open modes, seek, position, bytesAvailable, atEnd, partial read, read/write mode restrictions

---

## FilePath — COMPLETE

Wraps `std::filesystem::path`. Header-only simple value type.

**Files:**
- [x] `include/promeki/core/filepath.h`
- [x] `tests/filepath.cpp`

**Implementation checklist:**
- [x] Header guard, includes (`<filesystem>`), namespace
- [x] Wrap `std::filesystem::path` as private member
- [x] Constructors: from `String`, from `const char *`, from `std::filesystem::path`
- [x] `toString()` — full path as String
- [x] `fileName()` — file name with extension
- [x] `baseName()` — file name without extension
- [x] `suffix()` — extension (without dot)
- [x] `completeSuffix()` — all extensions (e.g., "tar.gz")
- [x] `absolutePath()` — resolved absolute path
- [x] `parent()` — parent directory as FilePath
- [x] `join(const FilePath &)` — append path component
- [x] `operator/(const FilePath &)` — alias for join
- [x] `operator/(const String &)` — join with String
- [x] `operator/(const char *)` — join with C string
- [x] `isEmpty()` — returns bool
- [x] `exists()` — returns bool (checks filesystem)
- [x] `isAbsolute()`, `isRelative()` — returns bool
- [x] `operator==`, `operator!=`, `operator<` (for sorting/maps)
- [x] `toStdPath()` — returns `const std::filesystem::path &`
- [x] Doctest: construction, decomposition, join, exists, absolute/relative, edge cases (dotfiles, empty, root parent)

---

## BufferedIODevice — COMPLETE

Adds read buffering on top of IODevice. Uses an internal `Buffer` (default 8192 bytes, lazily allocated). Subclasses implement `readFromDevice()` for raw I/O; remaining pure virtuals (open, close, isOpen, write) stay pure for concrete subclasses like File.

**Files:**
- [x] `include/promeki/core/bufferediodevice.h`
- [x] `src/bufferediodevice.cpp`
- [x] `tests/bufferediodevice.cpp`

**Implementation checklist:**
- [x] Derive from `IODevice`, use `PROMEKI_OBJECT`
- [x] Private: internal read buffer (`Buffer`), lazy allocation via `ensureReadBuffer()`
- [x] `readLine(size_t maxLength = 0)` — returns `Buffer` up to newline. 0 = no limit.
- [x] `readAll()` — reads all available data, returns `Buffer`
- [x] `readBytes(size_t maxBytes)` — reads up to N bytes, returns `Buffer`
- [x] `canReadLine()` — returns true if buffer contains a complete line (false when unbuffered)
- [x] `peek(void *buf, size_t maxBytes)` — read without consuming (returns 0 when unbuffered)
- [x] `peek(size_t maxBytes)` — returns `Buffer` without consuming (empty when unbuffered)
- [x] `setReadBuffer(Buffer &&buf)` — replace internal buffer (must be host-accessible, device must be closed)
- [x] `readBuffer()` / `readBufferSize()` — inspect current buffer (`readBufferSize()` uses `availSize()`)
- [x] Override `read()` — serves from buffer, large reads bypass buffer, fills/compacts automatically. When unbuffered, all reads go directly to `readFromDevice()`.
- [x] Override `bytesAvailable()` to include buffered data + `deviceBytesAvailable()` (only `deviceBytesAvailable()` when unbuffered)
- [x] Protected `readFromDevice()` pure virtual for subclass raw I/O
- [x] Protected `deviceBytesAvailable()` virtual (default 0)
- [x] Protected `ensureReadBuffer()` / `resetReadBuffer()` for subclass open/close hooks

### Unbuffered Mode
- [x] `setUnbuffered(bool enable)` — enables/disables unbuffered mode. When switching to unbuffered while open, drains/resets the read buffer. When switching back to buffered while open, re-ensures the read buffer.
- [x] `bool isUnbuffered() const` — returns unbuffered state
- [x] `bool _unbuffered = false` private member

### Doctest
- [x] Buffered read, readLine (with/without newline, maxLength), readAll, readBytes
- [x] canReadLine, peek (void* and Buffer overloads), bytesAvailable
- [x] Large read bypass, setReadBuffer (before open, while open, secure memory)
- [x] Buffer reuse across close/reopen, stale data leak prevention
- [x] Write then read across reopen, readLine/peek reset across sessions
- [x] Edge cases: empty device, non-open device, write-only mode, readBytes(0)
- [x] Unbuffered mode: verify reads go directly to device, verify buffer is not used, verify switching unbuffered while open drains buffer, verify switching back to buffered re-enables buffer

---

## Dir — COMPLETE

Directory operations. Simple utility class (not ObjectBase). Uses `std::filesystem` with `fnmatch()` for glob filtering.

**Files:**
- [x] `include/promeki/core/dir.h`
- [x] `src/dir.cpp`
- [x] `tests/dir.cpp`

**Implementation checklist:**
- [x] Header guard, includes, namespace
- [x] Constructor from `FilePath`, `String`, or `const char *`
- [x] Default constructor
- [x] `path()` — returns `FilePath`
- [x] `exists()` — returns bool
- [x] `entryList()` — returns `List<FilePath>` of contents
- [x] `entryList(const String &filter)` — glob-filtered entries via `fnmatch()`
- [x] `Error mkdir()` — create directory
- [x] `Error mkpath()` — create directory and all parents
- [x] `Error remove()` — remove empty directory
- [x] `Error removeRecursively()` — remove directory and all contents
- [x] `isEmpty()` — returns true if directory is empty (non-existent = empty)
- [x] Static `current()` — returns Dir for current working directory
- [x] Static `home()` — returns Dir for user home (`$HOME`)
- [x] Static `temp()` — returns Dir for temp directory
- [x] Static `setCurrent(const FilePath &)` — change working directory

### Doctest
- [x] Static accessors: current, home, temp
- [x] mkdir/remove, mkpath/removeRecursively
- [x] entryList (unfiltered and glob-filtered)
- [x] isEmpty, non-existent directory, setCurrent, construction from String/FilePath

---

## Refactor File to Derive from BufferedIODevice — COMPLETE

The existing `File` class (`include/promeki/core/file.h`, `src/file.cpp`) was a low-level file I/O wrapper. Refactored to derive from `BufferedIODevice`, making it a full IODevice citizen usable anywhere an IODevice is expected (DataStream, TextStream, socket-like APIs).

**Files:**
- [x] Modify `include/promeki/core/file.h`
- [x] Modify `src/file.cpp`
- [x] Update `tests/file.cpp`

### Direct I/O and Buffering

Direct I/O (`O_DIRECT`) bypasses the OS page cache and requires properly aligned buffers. BufferedIODevice's internal read buffer is incompatible with Direct I/O because (a) it defeats the purpose of bypassing the cache, and (b) the buffer may not satisfy alignment requirements. When direct I/O is enabled via `setDirectIO(true)`, it implicitly forces unbuffered mode so that BufferedIODevice's `read()` delegates directly to `readFromDevice()` (which calls POSIX `::read()`). The caller is responsible for providing properly aligned buffers to `read()` and `write()`.

When direct I/O is disabled, the previous unbuffered state is restored (saved before DirectIO forced it on). This means if the user independently called `setUnbuffered(true)` before enabling direct I/O, disabling direct I/O preserves that setting.

### Implementation checklist
- [x] Change `File` to derive from `BufferedIODevice` instead of being standalone
- [x] Map existing `File::Flags` to `IODevice::OpenMode` (ReadOnly, WriteOnly, ReadWrite)
- [x] Retain `File`-specific flags as extensions: `Create`, `Append`, `Truncate`, `Exclusive`
- [x] Override `open(OpenMode)` — delegates to `open(OpenMode, NoFlags)`
- [x] Add `open(OpenMode, int)` overload for file-specific flags (Create, Append, Truncate, Exclusive)
- [x] Implement `readFromDevice()` — delegates to POSIX `::read()`
- [x] Override `write()` — delegates to POSIX `::write()`, emits `bytesWritten`
- [x] Override `close()` — emits `aboutToClose`, calls `::close()`, resets state, returns Error
- [x] Override `seek()` (returns `Error`), `pos()`, `size()` (returns `Result<int64_t>`), `atEnd()` — delegate to POSIX implementations
- [x] Implement `deviceBytesAvailable()` — uses lseek64 to compute remaining bytes
- [x] Override `isSequential()` — returns `false`
- [x] Emit `bytesWritten`, `aboutToClose`, `errorOccurred` signals at appropriate points
- [x] `setDirectIO(bool)` / `isDirectIO()` — apply/remove `O_DIRECT` via `fcntl()`. On enable: save unbuffered state, force `setUnbuffered(true)`. On disable: restore saved unbuffered state.
- [x] `setSynchronous(bool)` / `isSynchronous()` — apply/remove `O_SYNC` via `fcntl()`
- [x] `setNonBlocking(bool)` / `isNonBlocking()` — apply/remove `O_NONBLOCK` via `fcntl()`
- [x] Add `bool _savedUnbuffered = false` — stores unbuffered state prior to DirectIO forcing it on
- [x] Preserve `truncate()` as File-specific API
- [x] Preserve `seekFromCurrent()` (returns `Result<int64_t>`) and `seekFromEnd()` (returns `Result<int64_t>`) as File-specific API
- [x] Remove `FileBytes` typedef — use `int64_t` directly (IODevice convention)
- [x] Add `const char *` constructor to resolve ambiguity with String/FilePath
- [x] Add `FilePath` constructor, `setFilename()`, `handle()` accessors
- [x] No existing call sites to break (File only used in its own tests)

### Direct I/O Bulk Read
- [x] `Result<size_t> directIOAlignment() const` — returns filesystem block size via fstat (alignment requirement for DIO buffers, offsets, and transfer sizes)
- [x] `Error readBulk(Buffer &buf, int64_t size)` — reads bulk data using direct I/O for the aligned interior and normal I/O for unaligned head/tail bytes. Calls `shiftData()` on the buffer so the DIO portion lands on an aligned address. Caller allocates with `directIOAlignment()` alignment and `size + directIOAlignment()` capacity. Falls back to normal read if region is smaller than one alignment block or DIO is unsupported. Sets `buf.size()` to actual bytes read (may be less than requested at EOF).

### Doctest
- [x] Verify File works as IODevice (polymorphic use via IODevice pointer)
- [x] Existing File tests updated and pass (open, read, write, seek, truncate)
- [x] DirectIO forces Unbuffered on, restores on disable
- [x] DirectIO save/restore: set Unbuffered=true, enable DirectIO, disable DirectIO → Unbuffered still true
- [x] DirectIO, Synchronous, NonBlocking: setter/getter with correct defaults
- [x] NonBlocking toggle on open file
- [x] Signals: bytesWritten emitted on write, aboutToClose emitted on close
- [x] Buffered readLine and readAll work correctly
- [x] size(), atEnd(), isSequential()
- [x] Open already-open file returns AlreadyOpen error
- [x] Close on non-open file is safe
- [x] Read on write-only / write on read-only returns -1
- [x] pos/seek/size on closed file
- [x] Truncate on closed file returns error

### Known Issues

- **FIXME: readBulk and non-blocking file descriptors.** The `readFull()` helper used by `readBulk()` loops on partial reads but treats EAGAIN as a fatal error (returns -1). On a non-blocking fd, this means: (1) the read fails instead of indicating "try again", and (2) if EAGAIN occurs after some bytes have already been read in a multi-portion transfer (head/DIO/tail), those bytes are lost. The `readFromDevice()` and `write()` paths handle this correctly (they report the real errno via `Error::syserr()`), so callers can detect `TryAgain` and retry. A fix for `readBulk` would require `readFull()` to distinguish EAGAIN from real errors and to track partial progress across portions so nothing is lost on retry.

---

## Buffer Size Model Update — COMPLETE

Buffer now tracks three distinct sizes: `allocSize()` (total allocation, constant), `availSize()` (usable space from `data()` to end of allocation, reduced by `shiftData()`), and `size()` (logical content size, user-set via `setSize()`). Previously `size()` returned what is now `availSize()`.

**Files:**
- [x] Modify `include/promeki/core/buffer.h`
- [x] Update `tests/buffer.cpp`

**Implementation checklist:**
- [x] Add `mutable size_t _size = 0` private member
- [x] `size_t size() const` — returns logical content size (defaults to 0 after allocation)
- [x] `void setSize(size_t s) const` — sets logical content size (asserts `s <= availSize()`)
- [x] `size_t availSize() const` — returns usable space (was previously `size()`)
- [x] `shiftData()` resets `_size` to 0
- [x] Copy constructor/assignment copies `_size`
- [x] Move constructor/assignment transfers `_size`, zeroes source
- [x] `fill()` uses `availSize()` (fills entire available space, not just logical content)

---

## Error Enhancements — COMPLETE

Extended `Error` with new error codes and system error translation overloads.

**Files:**
- [x] Modify `include/promeki/core/error.h`
- [x] Modify `src/error.cpp`

**Implementation checklist:**
- [x] Add `BufferTooSmall` error code
- [x] Add `static Error syserr(const std::error_code &ec)` — translates `std::error_code` (handles both POSIX and Windows categories)
- [x] Add `static Error syserr(DWORD winErr)` (Windows only) — translates Windows `GetLastError()` codes
- [x] Expand Windows error mappings: `ERROR_HANDLE_DISK_FULL`, `ERROR_SEM_TIMEOUT`, `ERROR_INVALID_HANDLE`, `ERROR_TOO_MANY_OPEN_FILES`, `ERROR_NOT_SUPPORTED`, `ERROR_SHARING_VIOLATION`, `ERROR_LOCK_VIOLATION`, `ERROR_DIR_NOT_EMPTY`, `ERROR_DIRECTORY`, `ERROR_FILE_TOO_LARGE`, `ERROR_BROKEN_PIPE`, `ERROR_NO_DATA`, `ERROR_OPERATION_ABORTED`

---

## Terminal Error Reporting Update — COMPLETE

Terminal methods now return `Error` or `Result<int>` instead of `bool`/`int` for consistent error reporting.

**Files:**
- [x] Modify `include/promeki/core/terminal.h`
- [x] Modify `src/terminal.cpp`

**Changes:**
- [x] `enableRawMode()` / `disableRawMode()` — return `Error` instead of `bool`
- [x] `readInput()` — returns `Result<int>` instead of `int`
- [x] `windowSize()` — returns `Error` instead of `bool`
- [x] `enableMouseTracking()` / `disableMouseTracking()` — return `Error` instead of `bool`
- [x] `enableBracketedPaste()` / `disableBracketedPaste()` — return `Error` instead of `bool`
- [x] `enableAlternateScreen()` / `disableAlternateScreen()` — return `Error` instead of `bool`
- [x] `writeOutput()` — returns `Result<int>` instead of `int`

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

---

## Update FileInfo — COMPLETE

- [x] Add `FileInfo(const FilePath &)` constructor
- [x] Add `FileInfo(const char *)` constructor
- [x] Add `filePath()` — returns `FilePath`
- [x] Ensure existing `String`-based constructor still works
- [x] Update tests (FilePath constructor, filePath accessor, round-trip)

---

## Process

Wraps subprocess execution. Won't be available on WASM — use `#ifdef` platform guards, `start()` returns error on unsupported platforms.

**Files:**
- [ ] `include/promeki/core/process.h`
- [ ] `src/process.cpp`
- [ ] `tests/process.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] Derive from `ObjectBase`, use `PROMEKI_OBJECT`
- [ ] `void setProgram(const String &program)`
- [ ] `void setArguments(const List<String> &args)`
- [ ] `void setWorkingDirectory(const FilePath &dir)`
- [ ] `void setEnvironment(const Map<String, String> &env)`
- [ ] `Error start(const String &program, const List<String> &args)` — convenience. Returns error if process cannot be started (not found, no permissions, WASM platform).
- [ ] `Error start()` — uses previously set program/args
- [ ] `Error waitForStarted(unsigned int timeoutMs = 0)`
- [ ] `Error waitForFinished(unsigned int timeoutMs = 0)`
- [ ] `int exitCode() const`
- [ ] `bool isRunning() const`
- [ ] `void kill()` — SIGKILL
- [ ] `void terminate()` — SIGTERM
- [ ] `ssize_t writeToStdin(const void *buf, size_t bytes)`
- [ ] `Buffer readAllStdout()`
- [ ] `Buffer readAllStderr()`
- [ ] `void closeWriteChannel()` — close stdin pipe
- [ ] `PROMEKI_SIGNAL(started)`
- [ ] `PROMEKI_SIGNAL(finished, int)` — exit code
- [ ] `PROMEKI_SIGNAL(readyReadStdout)`
- [ ] `PROMEKI_SIGNAL(readyReadStderr)`
- [ ] `PROMEKI_SIGNAL(errorOccurred, Error)`
- [ ] Platform guard: `#ifdef __EMSCRIPTEN__` — `start()` returns error
- [ ] Implementation: `pipe()` + `fork()` + `exec()` on POSIX
- [ ] Doctest: start simple process (e.g., `echo`), read stdout, exit code, waitForFinished
