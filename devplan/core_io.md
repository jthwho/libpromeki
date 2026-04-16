
## Known Issues

- **`readBulk` and non-blocking file descriptors.** The `readFull()` helper loops on partial reads but treats EAGAIN as a fatal error (returns -1). On a non-blocking fd this means (1) the read fails instead of indicating "try again", and (2) if EAGAIN occurs after some bytes have already been read in a multi-portion transfer (head/DIO/tail), those bytes are lost. The `readFromDevice()` and `write()` paths handle this correctly. A fix would require `readFull()` to distinguish EAGAIN from real errors and to track partial progress across portions.

---

## Windows File stub

Tracked in `fixme.md`. The Windows `#ifdef` branch of `src/core/file.cpp` is a stub and needs a real `CreateFile`/`ReadFile`/`WriteFile` implementation.
