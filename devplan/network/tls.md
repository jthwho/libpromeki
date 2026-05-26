# TLS / mbedTLS — open work

Tracks open items from the mbedTLS audit and related TLS-stack work.
Completed work is captured in commit history and inline documentation
(`include/promeki/sslcontext.h`, `cmake/promeki_mbedtls_user_config.h`,
`docs/debugging.md`) — this file lists what's still on the table.

## Open

### OCSP stapling — deferred until mbedTLS 4.x

**Status**: not implementable on vendored mbedTLS 3.6 LTS.

mbedTLS 3.6 has the `status_request` extension ID defined for parsing
but no client-side OCSP request builder, response parser, or
configuration hook.  Zero `mbedtls_ocsp_*` symbols exist anywhere in
the library.  The `ChangeLog` for the 3.6.x line contains zero OCSP
entries and the line is feature-frozen for LTS through 2027.

Implementing it ourselves on top of mbedTLS primitives is a ~1500–
2500 line effort (ASN.1 encoder/decoder for OCSP messages, signature
validation, responder fetch + caching, soft-vs-hard-fail policy)
with weak security ROI: OCSP soft-fail (the only realistic
deployment mode) is defeated by the same MITM that can block the
fetch, which is why modern browsers have moved to CRLite/CRLSets.

**Recommended mitigation today**: short-lived server certificates
(Let's Encrypt 90-day default; shorter for private PKI).

**Re-evaluate when**:
- mbedTLS 4.x ships a client-side OCSP API
- A concrete revocation-related incident forces the issue
- We have a use case where short certs aren't an option

### Session resumption

Not used today.  Each TLS connection runs a fresh handshake.

**Re-evaluate when**: `WebSocket` reconnect storms or
short-lived `HttpClient` bursts to the same host become hot.
Session tickets would amortise handshake cost across reconnects;
not worth the implementation cost otherwise.

### `mbedtls_ssl_conf_max_frag_len`

Mobile / high-latency tuning.  No relevant workload today —
we're targeting LAN / fibre-WAN / typical datacentre links
where the default 16 KiB record is the right size.

**Re-evaluate when**: a mobile / satellite / high-loss link
becomes a deployment target.

### HTTP/2 over a single connection

Would amortise TLS handshake cost across many requests to the
same host.  Real perf win for any workload with many small
requests (REST polling, API fan-out).  Out of scope for the TLS
audit — much bigger lift than mbedTLS-side work and lives in
`HttpClient` + a new HTTP/2 framing layer.

**Re-evaluate when**: we have a workload bottlenecked by repeated
handshake cost to the same host.

## mbedTLS upstream policy

### Current pin

Vendored at `thirdparty/mbedtls/`, pinned to the **3.6.x LTS** line.
Upstream LTS guarantee runs through **2027**.

### Why we're on 3.6 and not 4.x

mbedTLS 4.x relocates the legacy public headers
(`mbedtls/aes.h`, `mbedtls/ctr_drbg.h`, …) under `mbedtls/private/`
and consolidates the crypto layer behind PSA only.

- **SRT compatibility** — libsrt 1.5.x still calls the legacy
  pre-4.x crypto API directly.  We sidestep this today by linking
  SRT against its own isolated mbedTLS-3.6 bundle (symbol-localized
  via `objcopy`; see `cmake/promeki_srt_bundle.cmake`).
- **Library churn** — any future consumer that touches mbedTLS
  headers directly would need updates.

### When to upgrade to 4.x

Trigger conditions, in priority order:

1. **CVE in 3.6 that's fixed in 4.x but not back-portable.**
   Highest-priority forcing function.
2. **A specific 4.x-only feature we need.**  The compelling
   candidates today are:
   - **x86 SHA-NI hardware acceleration**
     (`MBEDTLS_SHA256_USE_X86_64_SHANI`).  Currently SHA-256 is
     software path ≈ 534 MB/s ≈ 85× our wire throughput, so this
     is *not* a current bottleneck.  Re-evaluate if we land bulk
     in-process signing, dense JWT verification, or similar.
   - **OCSP stapling client API** (see above).  Status unknown
     in current 4.x releases; investigate when triggered.
3. **3.6 LTS end-of-life (2027).**  Land the upgrade before
   2026 Q4 so we're not racing the deadline.

### Upgrade mechanics

1. Bump `thirdparty/mbedtls` submodule pointer to target 4.x tag.
2. Verify SRT still builds against its bundled mbedTLS-3.6
   (separate from main mbedTLS).
3. Audit `cmake/promeki_mbedtls_user_config.h` for any
   `#undef MBEDTLS_*` whose target was renamed in 4.x.  4.x also
   restructures `PSA_WANT_*` defines; check the upstream upgrade
   guide.
4. Rebuild main library, run `build check`.
5. Re-run throughput benchmarks (curl baseline + promeki
   end-to-end on a known HTTPS endpoint) and confirm no
   regression.
6. `nm` on `libtfpsacrypto.a` to confirm AES-NI / SHA-NI objects
   are linked in the new build.

## Where the audit findings live

- **Strict cipher / group / sig-alg lists** —
  `src/network/sslcontext.cpp` (top-of-file static arrays +
  `Impl::applySecurityProfile` comments).
- **Why each mbedTLS module is `#undef`'d** —
  `cmake/promeki_mbedtls_user_config.h` (per-module rationale).
- **Public security model + known gaps** —
  `include/promeki/sslcontext.h` (class-level doxygen).
- **TLS / HTTP observability + debug-module reference** —
  `docs/debugging.md` (`PROMEKI_DEBUG=…` workflow).
- **SRT mbedTLS isolation mechanism** —
  `cmake/promeki_srt_bundle.cmake` + comments in
  `CMakeLists.txt` around the SRT bundle build.
