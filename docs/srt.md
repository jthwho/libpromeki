# SRT (Secure Reliable Transport) {#srt}

libpromeki ships an embedded
[libsrt](https://github.com/Haivision/srt) 1.5.5 stack so callers
can carry RTP, MPEG-TS, or any opaque packet stream over a
reliable, low-latency transport that survives the public internet.
The implementation lives entirely under `include/promeki/srt*.h`
and `src/network/srt*.cpp`; nothing in the SRT API leaks onto
consumers' include path.

## Why two mbedTLS versions {#srt_two_mbedtls}

libpromeki's main TLS stack is **mbedTLS 4.1.0**, which retires
the legacy classical-crypto headers (`mbedtls/aes.h`,
`ctr_drbg.h`, …) in favour of the PSA Crypto API. SRT 1.5.5's
own `haicrypt/cryspr-mbedtls.c` still uses those legacy headers
directly. Patching SRT or downgrading the project's main mbedTLS
were both unattractive; instead we vendor a **second** mbedTLS
pinned to **3.6 LTS** that exists only inside the SRT bundle and
is otherwise invisible to the rest of the library.

The two copies coexist in one `libpromeki.so` because the
mbedTLS-3.6 globals are flipped from `STB_GLOBAL` to `STB_LOCAL`
by an `objcopy --localize-symbols` pass during the build, so they
cannot collide with mbedTLS-4.x at static link time. After the
final link, every `mbedtls_*` / `psa_*` symbol from the 3.6 copy
is a private intra-archive reference.

## The bundle pipeline {#srt_bundle}

Three submodules feed it:

| Submodule                  | Pinned tag    | Role                       |
|----------------------------|---------------|----------------------------|
| `thirdparty/srt`           | `v1.5.5`      | The libsrt source tree     |
| `thirdparty/srt-mbedtls`   | `mbedtls-3.6.6` | Private mbedTLS for SRT   |

The build steps (CMake-driven, all in
`CMakeLists.txt` and `cmake/promeki_srt_bundle.cmake`):

1. `ExternalProject_Add(srt_mbedtls_ep)` builds the 3.6 LTS into
   an isolated prefix (`${CMAKE_BINARY_DIR}/srt-prefix`) with
   `-fvisibility=hidden`.
2. `ExternalProject_Add(srt_ep)` builds libsrt against the
   isolated 3.6 install (`USE_ENCLIB=mbedtls`, `MBEDTLS_PREFIX=…`).
   Compiled with `-fvisibility=hidden`; SRT's own `SRT_API` macro
   keeps the public C API default-visible.
3. `cmake/promeki_srt_bundle.cmake` (run via `cmake -P`) bundles
   them:
   - `nm -g --defined-only` enumerates every global the three
     mbedTLS-3.6 archives define.
   - `ld -r --whole-archive libsrt.a --no-whole-archive
     libmbedtls.a libmbedx509.a libmbedcrypto.a -o bundle.o`
     partial-links them into one object.
   - `objcopy --localize-symbols=<file>` flips every
     mbedTLS-3.6 global symbol to local in the bundle.
   - `ar rcs libpromeki_srt_bundle.a bundle.o` wraps it.
4. `target_link_libraries(promeki PRIVATE promeki_srt_bundle)` +
   `LINKER:--exclude-libs=libpromeki_srt_bundle.a` so the
   bundle's remaining `srt_*` / `haicrypt_*` globals also stay
   out of the shared library's `.dynsym`.

After the final link of `libpromeki.so`:

- `0` `mbedtls_*` / `psa_*` symbols from 3.6 in dynsym
- `0` `srt_*` / `haicrypt_*` / `hcrypt_*` symbols in dynsym
- `917` `mbedtls_*` from the main 4.x stack — unchanged
- `promeki::Srt*` C++ API exported normally

## Public API surface {#srt_api}

| Class                | File                               | Role                                                          |
|----------------------|------------------------------------|---------------------------------------------------------------|
| `SrtSocket`          | `include/promeki/srtsocket.h`      | Connection-oriented socket (caller-mode or accepted)          |
| `SrtServer`          | `include/promeki/srtserver.h`      | Listener + accept; supports a stream-id pre-accept callback   |
| `SrtSocketTransport` | `include/promeki/srtsockettransport.h` | `PacketTransport` adapter: Caller / Listener / Rendezvous |
| `SrtEpoll`           | `include/promeki/srtepoll.h`       | Multiplexer for many sockets; pull `wait()` or push `run()`   |
| `SrtGroup`           | `include/promeki/srtgroup.h`       | Caller-side bonded group: Broadcast / Backup multi-path       |

The headers reference an opaque `int` handle; only the matching
`.cpp` files include `<srt/srt.h>`. Consumers of `libpromeki`
never see SRT's API.

## Threading model {#srt_threading}

- `SrtSocket`, `SrtServer`, `SrtGroup` inherit @ref IODevice or
  @ref ObjectBase semantics — thread-affine. One instance per
  thread.
- `SrtEpoll` is also single-threaded. Run multiple instances on
  different threads if you need parallelism.
- `srt_startup` is called lazily on first `SrtSocket::open()`
  via an atomic guard; `srt_cleanup` is registered with
  `std::atexit`. Same pattern as @ref SslContext's
  `ensurePsaCrypto` initializer.

## EventLoop integration {#srt_eventloop}

`SrtEpoll` deliberately does *not* couple to the project
@ref EventLoop. Two integration shapes are supported:

- **Pump from your event loop** — register a recurring timer (or
  zero-delay tick) that calls @ref SrtEpoll::dispatchOnce. Lowest
  added latency when SRT is one of several I/O sources.
- **Dedicated worker thread** — call @ref SrtEpoll::run on a
  worker; user callbacks fire on that thread and forward work to
  the main thread via Promeki signals (cross-thread dispatch is
  built into @ref ObjectBase::connect).

## Encryption {#srt_encryption}

Set a passphrase via @ref SrtSocket::setPassphrase (10–79 bytes)
on both peers before connect / listen. Optional
@ref SrtSocket::setEncryptionKeyLength selects AES key length
(0 = auto, 16 = AES-128, 24 = AES-192, 32 = AES-256). Empty
passphrase disables encryption.

Encryption is exercised end-to-end by the
`SrtSocket: encrypted handshake with shared passphrase` and
`SrtSocket: mismatched passphrase is rejected` unit tests — both
land directly on the isolated mbedTLS-3.6 path.

## Build flags {#srt_build}

- `PROMEKI_ENABLE_SRT` — default `ON`. Requires
  `PROMEKI_ENABLE_NETWORK` and `PROMEKI_ENABLE_TLS` (the main
  4.x mbedTLS is *only* used by the rest of the library; SRT
  uses its private 3.6 copy, but we share the precondition for
  consistency).
- `PROMEKI_USE_SYSTEM_SRT` — default `OFF`. When `ON`, looks
  for a system libsrt via `find_package(SRT)` or pkg-config
  instead of the vendored bundle. The symbol-isolation
  guarantees only apply to the vendored path.

## Still deferred {#srt_deferred}

Items not in scope yet:

- **MediaIO backend** — an `SrtMediaIO` that exposes SRT as a
  source / sink for the @ref mediaio framework, similar to the
  RTP backend. Wraps `SrtSocketTransport` so `RtpSession` can
  ride over SRT unchanged.
- **Listener-side bonded handshake** — libsrt's group-mirror
  feature where a single listener auto-creates a matching
  `SrtGroup` for incoming bonded callers. The accepted
  `SrtSocket::groupHandle()` exposes the group ID, but no
  managed `SrtGroup` is built from it yet.
- **Data-path unit tests** — SRT live-mode TSBPD timing is too
  racy at unit-test scope. Smoke coverage in this directory
  stops at handshake; functional tests live under
  `utils/promeki-test/`.

## File map {#srt_files}

- `thirdparty/srt/` — vendored libsrt 1.5.5 (submodule)
- `thirdparty/srt-mbedtls/` — vendored mbedTLS 3.6.6 LTS (submodule)
- `cmake/promeki_srt_bundle.cmake` — partial-link + objcopy
  pipeline, run via `cmake -P` from a custom command
- `CMakeLists.txt` — both `ExternalProject_Add` calls live in
  the `if(PROMEKI_ENABLE_SRT)` block; `target_link_options`
  applies `--exclude-libs` on the `promeki` link
- `include/promeki/srt*.h` — public API
- `src/network/srt*.cpp` — implementation (only files that
  include `<srt/srt.h>`)
- `tests/unit/network/srtsocket.cpp` — 18 unit tests covering
  open/close, options, handshake, encryption, listen-callback,
  rendezvous, epoll dispatch, and SrtGroup surface
