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
- [x] `virtual void close() = 0`
- [x] `virtual bool isOpen() const = 0`
- [x] `OpenMode openMode() const` — returns current mode
- [x] `bool isReadable() const`, `bool isWritable() const`
- [x] `virtual int64_t read(void *data, int64_t maxSize) = 0`
- [x] `virtual int64_t write(const void *data, int64_t maxSize) = 0`
- [x] `virtual int64_t bytesAvailable() const` — default returns 0
- [x] `virtual bool waitForReadyRead(unsigned int timeoutMs = 0)` — default returns false
- [x] `virtual bool waitForBytesWritten(unsigned int timeoutMs = 0)` — default returns false
- [x] `virtual bool isSequential() const` — default returns false
- [x] `virtual bool seek(int64_t pos)` — default returns false
- [x] `virtual int64_t pos() const` — default returns 0
- [x] `virtual int64_t size() const` — default returns 0
- [x] `virtual bool atEnd() const` — default returns `pos() >= size()`
- [x] `PROMEKI_SIGNAL(readyRead)` — emitted when data available
- [x] `PROMEKI_SIGNAL(bytesWritten, int64_t)` — emitted after write completes
- [x] `PROMEKI_SIGNAL(errorOccurred, Error)` — emitted on error
- [x] `PROMEKI_SIGNAL(aboutToClose)` — emitted before close
- [x] Error state: `error()` returns last Error, `clearError()`, protected `setError()`

### Option System

Runtime-registered key-value option mechanism for device-specific configuration. Options use `Variant` values and runtime-allocated type IDs (lock-free atomic counter, same pattern as `Event`). Devices declare supported options by registering them; unsupported options return `Error::NotSupported`.

- [x] `using Option = uint32_t` — runtime-assigned option type ID
- [x] `static constexpr Option InvalidOption = 0`
- [x] `static Option registerOptionType()` — allocates unique ID, thread-safe
- [x] Built-in option types: `DirectIO`, `Synchronous`, `NonBlocking`, `Unbuffered`
- [x] `Error setOption(Option opt, const Variant &value)` — set option value. Returns `NotSupported` if device doesn't support it. Calls `onOptionChanged()`.
- [x] `Result<Variant> option(Option opt) const` — query current value. Returns `NotSupported` error if unsupported.
- [x] `bool optionSupported(Option opt) const` — returns true if device supports this option
- [x] `using OptionList = std::initializer_list<std::pair<const Option, Variant>>`
- [x] Constructor `IODevice(OptionList opts, ObjectBase *parent = nullptr)` — declare supported options via initializer list
- [x] Protected `registerOption(Option, Variant)` — for dynamic registration in subclass constructors
- [x] Protected `virtual void onOptionChanged(Option, const Variant &)` — override to react to option changes (e.g., toggle `O_DIRECT`). Default does nothing.

**Design note:** The original plan specified an enum-based hint system. The implemented option system is superior: runtime type registration allows subclasses and new libraries to define options without modifying the base enum. Socket-specific options (NoDelay, KeepAlive, etc.) will be registered by the socket classes in Phase 3.

### Doctest
- [x] Test via concrete subclass (MemoryIODevice: in-memory buffer IODevice)
- [x] Test option system: set/get, unsupported returns NotSupported, onOptionChanged callback, initializer list constructor, overwrite
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

The existing `File` class (`include/promeki/core/file.h`, `src/file.cpp`) is a low-level file I/O wrapper with its own `open()`, `read()`, `write()`, `seek()`, `close()`. Refactor it to derive from `BufferedIODevice`, making it a full IODevice citizen. This enables File to be used anywhere an IODevice is expected (DataStream, TextStream, socket-like APIs).

**Files:**
- [ ] Modify `include/promeki/core/file.h`
- [ ] Modify `src/file.cpp`
- [ ] Update `tests/file.cpp`

**Implementation checklist:**
- [ ] Change `File` to derive from `BufferedIODevice` instead of being standalone
- [ ] Map existing `File::Flags` to `IODevice::OpenMode` (ReadOnly, WriteOnly, ReadWrite)
- [ ] Retain `File`-specific flags as extensions: `Create`, `Append`, `Truncate`, `Exclusive`
- [ ] Register options via initializer list: `DirectIO`, `Synchronous`, `NonBlocking`
- [ ] Override `open(OpenMode)` — delegate to existing POSIX `::open()` logic
- [ ] Add `open(OpenMode, Flags)` overload for file-specific flags (Create, Append, Truncate, Exclusive)
- [ ] Override `read()` — delegate to existing `::read()` logic
- [ ] Override `write()` — delegate to existing `::write()` logic
- [ ] Override `close()` — delegate to existing `::close()` logic
- [ ] Override `seek()`, `pos()`, `size()`, `atEnd()` — delegate to existing implementations
- [ ] Override `isSequential()` — return `false`
- [ ] Emit `readyRead`, `bytesWritten`, `errorOccurred` signals at appropriate points
- [ ] Override `onOptionChanged()` to apply DirectIO/Sync/NonBlocking via fcntl
- [ ] Remove `isDirectIO()` / `setDirectIOEnabled()` — replaced by option system
- [ ] Preserve `truncate()` as File-specific API (not an option — it's an operation)
- [ ] Migrate `FileBytes` return type to `int64_t` (match IODevice convention)
- [ ] Ensure all existing call sites still compile
- [ ] Doctest: verify File works as IODevice (pass to DataStream, TextStream), existing File tests still pass

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
