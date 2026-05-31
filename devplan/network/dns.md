# DNS stub resolver — COMPLETE (v1)

**Library:** `promeki` (feature flag `PROMEKI_ENABLE_NETWORK`)
**Landed:** 2026-05-31

A library-native DNS stub resolver that forms RFC 1035 packets
directly and speaks to the host's configured upstream resolvers, so
the library no longer calls `getaddrinfo` / `gethostbyname`.

---

## Shipped

### Low-level wire layer

- `enums_dns.h` — `TypedEnum<Self>` wrappers for `DnsRecordType`,
  `DnsRecordClass`, `DnsRecordSection`, `DnsOpcode`, `DnsRcode`.
  Numeric values match the IANA on-wire encodings.
- `dnsname.h/cpp` — RFC 1035 §4.1.4 compression-aware name codec.
  `DnsName::encode` / `decode`; pointer hops capped at
  `MaxNamePointerHops` (16); forward / self pointers rejected;
  lifted and generalised from the old `mdnsname.cpp`.
- `dnsrecord.h/cpp` — `DnsRecord` and `DnsQuestion` value types with
  named static factories (`makeA`, `makeAaaa`, `makeSrv`, `makePtr`,
  `makeTxt`, …).  Same field names as the old `MdnsParsedRecord`
  so mDNS callers compile unchanged through
  `using MdnsParsedRecord = DnsRecord;` in `mdnspacket.h`.
- `dnspacket.h/cpp` — `DnsPacket` parser (`parse` for unicast /
  `parseMdns` for multicast) + `DnsPacket::Builder` (questions,
  answers, authority, additional, EDNS0 OPT).  `MdnsPacket` is now
  `using MdnsPacket = DnsPacket;`.

### Cache

- `dnscache.h/cpp` — `DnsCache`: TTL-aware per-name, per-type LRU.
  Hard cap (`MaxEntries`, default 1024).  `insert` / `lookup` /
  `expire` (call-site driven).  Thread-safe under its own mutex.

### Config

- `dnsconfig.h/cpp` — `DnsConfig`: parses `/etc/resolv.conf`
  (`nameserver`, `search`, `options ndots:`).  Falls back to
  `8.8.8.8:53` when no system config is found.  Refreshed lazily
  on first use; forced re-read via `refresh()`.

### Resolver

- `dnsresolver.h/cpp` — `DnsResolver` (`ObjectBase` subclass):
  - `lookup(name, type)` — async; returns a `DnsLookup` that emits
    `answeredSignal(DnsLookup*)` / `failedSignal(DnsLookup*, Error)`.
  - `resolveSync(hostname)` — blocking convenience wrapper that
    covers the old `getaddrinfo` hot path.
  - UDP-first with TCP fallback on TC-bit (RFC 1035 §4.2.1).
  - Iterates through `DnsConfig`'s server list on SERVFAIL /
    timeout (configurable via `setRetryCount` / `setTimeoutMs`).
  - Two-server failover tested end-to-end.
  - RFC 5452 §9 question-section verification: inbound responses
    are checked against (name, type, class) of the in-flight
    question; mismatched responses are silently dropped.
  - `DnsResolver::setMdnsRoutingEnabled(bool)` — opt out of
    `.local.` → mDNS routing per-resolver-instance; useful for
    tests that need predictable NXDOMAIN behaviour.
  - mDNS routing: `.local.` names are dispatched to the packet-
    observer API on `MdnsManager` rather than to a unicast server.
    When `PROMEKI_ENABLE_MDNS` is off, `.local.` falls through to
    unicast (typically NXDOMAIN).

### mDNS integration

- `MdnsManager` gained `addPacketObserver` / `removePacketObserver`
  / `unregisterPacketObserver` for the resolver's mDNS routing path;
  observers run on the manager's worker thread and must marshal
  back to their own `EventLoop` before touching resolver state.
- All existing network code that called `getaddrinfo` directly
  (HttpClient, RTMP, SRT, WebSocket, etc.) now routes through
  `DnsResolver::resolveSync`.

### Tests

- `tests/unit/network/dnsname.cpp` — label escape / compression
  round-trips.
- `tests/unit/network/dnspacket.cpp` — per-RR-type encode + parse
  round-trips; EDNS0 OPT; malformed-input rejection.
- `tests/unit/network/dnscache.cpp` — TTL expiry, capacity eviction,
  multi-type per-name isolation.
- `tests/unit/network/dnsconfig.cpp` — resolv.conf parsing; fallback
  behaviour.
- `tests/unit/network/dnsresolver.cpp` — end-to-end via a loopback
  mock DNS server (A / NXDOMAIN / CNAME chase / TCP fallback /
  two-server failover / cache-hit / question-section verification).

---

## Deferred Items

- [ ] **Async `resolve` on `NetworkAddress`** — `NetworkAddress::resolve()` is still synchronous (`resolveSync` under the hood).  An async variant returning `Result<NetworkAddress>` via signal would unblock callers on the EventLoop thread.
- [ ] **SRV / TXT / MX lookups in public API** — `DnsLookup` supports arbitrary `DnsRecordType` internally; a convenience wrapper (e.g. `DnsResolver::lookupSrv(service, proto, domain)`) is not yet exposed.
- [ ] **DNSSEC validation** — DO bit passes through; RRSIG / DNSKEY records come back as opaque `rawRdata`.  No chain validation.
- [ ] **DoT / DoH** — plain UDP + TCP only.  DNS-over-TLS would reuse the existing `SslSocket`.
- [ ] **Recursive resolution** — this is a stub resolver; full root-hint recursion is out of scope.
- [ ] **IDN** — callers pass punycode; no IDNA encoding/decoding.
- [ ] **Windows / musl resolv.conf paths** — `DnsConfig` reads `/etc/resolv.conf`; Windows fallback is hard-coded `8.8.8.8`.
