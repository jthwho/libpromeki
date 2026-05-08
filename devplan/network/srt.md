# SRT (Secure Reliable Transport) — COMPLETE

**Phase:** 3D
**Library:** `promeki` (feature flag `PROMEKI_ENABLE_SRT`, requires `_NETWORK` + `_TLS`)

SRT support has shipped. The full vendoring + symbol-isolation pipeline, public API, and unit tests are merged. See `docs/srt.md` for the architecture reference.

**Shipped:**
- Two submodules: `thirdparty/srt` (libsrt 1.5.5) and `thirdparty/srt-mbedtls` (mbedTLS 3.6.6 LTS)
- CMake bundle pipeline (`cmake/promeki_srt_bundle.cmake`): partial-links libsrt + mbedTLS-3.6 archives, `objcopy --localize-symbols` seals mbedTLS-3.6 inside the bundle, `--exclude-libs` hides `srt_*` / `haicrypt_*` from `libpromeki.so` dynsym
- `SrtSocket` (IODevice subclass) — caller-mode connect, options, stats, groupHandle
- `SrtServer` — listen + accept with optional listen callback for streamid-based routing/auth
- `SrtSocketTransport` — PacketTransport adapter with Caller / Listener / Rendezvous modes
- `SrtEpoll` — RAII wrapper: pull (`wait`) + push (`setCallback` + `dispatchOnce` + `run`/`stop`)
- `SrtGroup` (IODevice) — caller-side Broadcast / Backup bonded groups + adopting ctor
- 19 doctest cases covering: open/close, option validators, loopback handshake, encryption (match + mismatch), listen-callback reject, rendezvous, SrtEpoll dispatch, SrtGroup lifecycle + Backup type + adopting ctor, groupHandle
- Demo: `demos/srt-demo` — encrypted loopback example with worker-thread dispatch

---

## Deferred Items

- [ ] **SrtMediaIO backend** — expose SRT as a source/sink for the MediaIO framework (wraps `SrtSocketTransport` so `RtpSession` can ride over SRT unchanged)
- [ ] **Listener-side bonded handshake** — managed `SrtGroup` built from `SrtSocket::groupHandle()` on the first accepted member of an incoming bonded caller; libsrt auto-creates the group mirror but nothing promotes it to an owned `SrtGroup` yet
- [ ] **Data-path functional tests** — SRT live-mode TSBPD timing is too racy at unit scope; full send/receive coverage belongs in `utils/promeki-test/`
- [ ] **`PROMEKI_USE_SYSTEM_SRT`** — the system-libsrt path (`find_package(SRT)`) compiles but symbol-isolation guarantees only apply to the vendored bundle; document the trade-off
