# IO Abstractions and Filesystem

**Phase:** 2
**Dependencies:** Phase 1 (Mutex, Future for async patterns)
**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

---

## IODevice Base Class

Abstract ObjectBase-derived class — the common interface for files, sockets, pipes, serial ports. All methods are virtual so WASM backends can provide alternative implementations. Every blocking `waitFor*()` has a signal-based async path.

**Files:**
- [ ] `include/promeki/core/iodevice.h`
- [ ] `src/iodevice.cpp`
- [ ] `tests/iodevice.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] Derive from `ObjectBase`, use `PROMEKI_OBJECT`
- [ ] `enum OpenMode { NotOpen, ReadOnly, WriteOnly, ReadWrite }`
- [ ] `virtual Error open(OpenMode mode) = 0`
- [ ] `virtual void close() = 0`
- [ ] `virtual bool isOpen() const = 0`
- [ ] `OpenMode openMode() const` — returns current mode
- [ ] `virtual ssize_t read(void *buf, size_t maxBytes) = 0`
- [ ] `virtual ssize_t write(const void *buf, size_t bytes) = 0`
- [ ] `ssize_t write(const Buffer &buf)` — convenience overload
- [ ] `virtual ssize_t bytesAvailable() const` — default returns 0
- [ ] `virtual Error waitForReadyRead(unsigned int timeoutMs = 0)` — blocking wait, 0 = indefinite. Returns `Error::Ok` or `Error::Timeout`.
- [ ] `virtual Error waitForBytesWritten(unsigned int timeoutMs = 0)` — blocking wait. Returns `Error::Ok` or `Error::Timeout`.
- [ ] `virtual bool isSequential() const` — true for pipes/sockets, false for files
- [ ] `virtual Error seek(int64_t pos)` — default returns error (sequential devices don't support seeking)
- [ ] `virtual int64_t pos() const` — default returns -1
- [ ] `virtual int64_t size() const` — default returns -1
- [ ] `virtual bool atEnd() const`
- [ ] `PROMEKI_SIGNAL(readyRead)` — emitted when data available
- [ ] `PROMEKI_SIGNAL(bytesWritten, ssize_t)` — emitted after write completes
- [ ] `PROMEKI_SIGNAL(errorOccurred, Error)` — emitted on error
- [ ] `PROMEKI_SIGNAL(aboutToClose)` — emitted before close
- [ ] Error state: `error()` returns last Error, `clearError()`

### Hint System

A generic key-value hint mechanism for passing device-specific configuration through the abstract interface. Hints are advisory — a device that doesn't understand a hint ignores it silently. This keeps device-specific concerns (Direct I/O, TCP_NODELAY, read-ahead, write-through, memory mapping, buffer sizing, etc.) out of the base class API while letting callers configure any device uniformly.

- [ ] `enum Hint` — extensible set of well-known hint keys. Subclasses can define additional keys starting from `UserHint`.
  ```
  enum Hint {
      // File hints
      DirectIO,           // bool: bypass OS page cache (O_DIRECT)
      SyncIO,             // bool: synchronous writes (O_SYNC)
      ReadAhead,          // size_t: OS read-ahead size in bytes
      WriteThrough,       // bool: write-through (no write buffering in OS)

      // Socket hints
      NoDelay,            // bool: TCP_NODELAY (disable Nagle)
      KeepAlive,          // bool: SO_KEEPALIVE
      ReuseAddress,       // bool: SO_REUSEADDR
      SendBufferSize,     // size_t: SO_SNDBUF
      RecvBufferSize,     // size_t: SO_RCVBUF
      MulticastTTL,       // int: IP_MULTICAST_TTL
      DSCP,               // uint8_t: IP_TOS / DSCP field

      // General hints
      NonBlocking,        // bool: non-blocking I/O mode
      BufferSize,         // size_t: preferred internal buffer size

      // Extension point
      UserHint = 0x10000  // subclasses define hints from here
  };
  ```
- [ ] `virtual Error setHint(Hint hint, const Variant &value)` — apply a hint. Returns `Error::Ok` if applied, or a descriptive error (e.g., unsupported hint, invalid value, wrong device state, insufficient permissions). Default implementation returns an error for all hints.
- [ ] `virtual Variant hint(Hint hint) const` — query current hint value. Returns invalid Variant if unsupported.
- [ ] `bool hasHint(Hint hint) const` — returns true if the device supports this hint (shorthand for `hint(h).isValid()`)
- [ ] `virtual List<Hint> supportedHints() const` — returns list of hints this device understands. Default returns empty list.
- [ ] Hints can be set before or after `open()`. Devices document which hints must be set before open (e.g., DirectIO) vs. can be changed at runtime (e.g., NoDelay).
- [ ] `PROMEKI_SIGNAL(hintChanged, Hint)` — emitted when a hint value changes (optional, for reactive configuration)

### Doctest
- [ ] Test via concrete subclass (e.g., a simple memory buffer IODevice for testing)
- [ ] Test hint system: set/get known hints, verify unknown hints return false/invalid, supportedHints()

---

## BufferedIODevice

Adds read buffering on top of IODevice.

**Files:**
- [ ] `include/promeki/core/bufferediodevice.h`
- [ ] `src/bufferediodevice.cpp`
- [ ] `tests/bufferediodevice.cpp`

**Implementation checklist:**
- [ ] Derive from `IODevice`
- [ ] Private: internal read buffer (`Buffer` or `Deque<uint8_t>`)
- [ ] `readLine(size_t maxLength = 0)` — returns `Buffer` up to newline. 0 = no limit.
- [ ] `readAll()` — reads all available data, returns `Buffer`
- [ ] `readBuffer(size_t maxBytes)` — reads up to N bytes, returns `Buffer`
- [ ] `canReadLine()` — returns true if buffer contains a complete line
- [ ] `peek(void *buf, size_t maxBytes)` — read without consuming
- [ ] `peek(size_t maxBytes)` — returns `Buffer` without consuming
- [ ] `setReadBufferSize(size_t size)` — limit internal buffer size (0 = unlimited)
- [ ] Override `bytesAvailable()` to include buffered data
- [ ] Doctest: readLine, readAll, peek, canReadLine, buffer size limits

---

## FilePath

Wraps `std::filesystem::path`. Simple value type.

**Files:**
- [ ] `include/promeki/core/filepath.h`
- [ ] `tests/filepath.cpp`

**Implementation checklist:**
- [ ] Header guard, includes (`<filesystem>`), namespace
- [ ] Wrap `std::filesystem::path` as private member
- [ ] Constructors: from `String`, from `const char *`, from `std::filesystem::path`
- [ ] `toString()` — full path as String
- [ ] `fileName()` — file name with extension
- [ ] `baseName()` — file name without extension
- [ ] `suffix()` — extension (without dot)
- [ ] `completeSuffix()` — all extensions (e.g., "tar.gz")
- [ ] `absolutePath()` — resolved absolute path
- [ ] `parent()` — parent directory as FilePath
- [ ] `join(const String &)` — append path component
- [ ] `operator/(const String &)` — alias for join
- [ ] `operator/(const FilePath &)` — alias for join
- [ ] `isEmpty()` — returns bool
- [ ] `exists()` — returns bool (checks filesystem)
- [ ] `isAbsolute()`, `isRelative()` — returns bool
- [ ] `operator==`, `operator!=`, `operator<` (for sorting/maps)
- [ ] `toStdPath()` — returns `const std::filesystem::path &`
- [ ] Doctest: construction, decomposition (fileName, baseName, suffix), join, exists, absolute/relative

---

## Dir

Directory operations. Utility class.

**Files:**
- [ ] `include/promeki/core/dir.h`
- [ ] `src/dir.cpp`
- [ ] `tests/dir.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] Constructor from `FilePath` or `String`
- [ ] `path()` — returns `FilePath`
- [ ] `exists()` — returns bool
- [ ] `entryList()` — returns `List<FilePath>` of contents
- [ ] `entryList(const String &filter)` — glob-filtered entries
- [ ] `Error mkdir()` — create directory
- [ ] `Error mkpath()` — create directory and all parents
- [ ] `Error remove()` — remove empty directory
- [ ] `Error removeRecursively()` — remove directory and all contents
- [ ] `isEmpty()` — returns true if directory is empty
- [ ] Static `current()` — returns Dir for current working directory
- [ ] Static `home()` — returns Dir for user home
- [ ] Static `temp()` — returns Dir for temp directory
- [ ] Static `setCurrent(const FilePath &)` — change working directory
- [ ] Doctest: mkdir/mkpath, entryList, exists, remove, static accessors

---

## Refactor File to Derive from BufferedIODevice

The existing `File` class (`include/promeki/core/core/file.h`, `src/file.cpp`) is a low-level file I/O wrapper with its own `open()`, `read()`, `write()`, `seek()`, `close()`. Refactor it to derive from `BufferedIODevice`, making it a full IODevice citizen. This enables File to be used anywhere an IODevice is expected (DataStream, TextStream, socket-like APIs).

**Files:**
- [ ] Modify `include/promeki/core/core/file.h`
- [ ] Modify `src/file.cpp`
- [ ] Update `tests/file.cpp`

**Implementation checklist:**
- [ ] Change `File` to derive from `BufferedIODevice` instead of being standalone
- [ ] Map existing `File::Flags` to `IODevice::OpenMode` (ReadOnly, WriteOnly, ReadWrite)
- [ ] Retain `File`-specific flags as extensions: `Create`, `Append`, `Truncate`, `Exclusive`, `DirectIO`, `Sync`, `NonBlocking`
- [ ] Override `open(OpenMode)` — delegate to existing POSIX `::open()` logic
- [ ] Add `open(OpenMode, Flags)` overload for file-specific flags (Create, Append, Truncate, Exclusive)
- [ ] Override `read()` — delegate to existing `::read()` logic
- [ ] Override `write()` — delegate to existing `::write()` logic
- [ ] Override `close()` — delegate to existing `::close()` logic
- [ ] Override `seek()`, `pos()`, `size()`, `atEnd()` — delegate to existing implementations
- [ ] Override `isSequential()` — return `false`
- [ ] Emit `readyRead`, `bytesWritten`, `errorOccurred` signals at appropriate points
- [ ] Migrate Direct I/O, Sync, NonBlocking to hint system:
  - [ ] Override `setHint()` to handle `DirectIO`, `SyncIO`, `NonBlocking`, `ReadAhead`, `WriteThrough`
  - [ ] Override `hint()` to return current values
  - [ ] Override `supportedHints()` to advertise supported hints
  - [ ] Remove `isDirectIO()` / `setDirectIOEnabled()` — replaced by `setHint(DirectIO, true)` / `hint(DirectIO)`
- [ ] Preserve `truncate()` as File-specific API (not a hint — it's an operation)
- [ ] Preserve existing `FileBytes` return type or migrate to `ssize_t` (match IODevice convention)
- [ ] Ensure all existing call sites still compile
- [ ] Doctest: verify File works as IODevice (pass to DataStream, TextStream), existing File tests still pass

---

## Refactor AudioFile to Use IODevice

`AudioFile` currently manages its own file I/O through the `Impl` pimpl pattern. Refactor so the `Impl` backends use `IODevice` (specifically `File`) for their underlying I/O, enabling future backends that read from network streams or memory buffers.

**Files:**
- [ ] Modify `include/promeki/core/proav/audiofile.h`
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

## Update FileInfo

- [ ] Add `FileInfo(const FilePath &)` constructor
- [ ] Add `filePath()` — returns `FilePath`
- [ ] Ensure existing `String`-based constructor still works
- [ ] Update tests

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
