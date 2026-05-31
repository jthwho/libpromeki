# mDNS / DNS-SD — IN PROGRESS (wire layer generalised → DNS; see `dns.md`)

**Library:** `promeki` (feature flag `PROMEKI_ENABLE_MDNS`, requires `_NETWORK`)
**Vendored dep:** `thirdparty/mdns` (mjansson/mdns — single-header packet parser)

A native mDNS / DNS-SD implementation built directly on `UdpSocket`
and `MulticastManager`, with a passive sniffer at one extreme and a
fully RFC-compliant publisher at the other.  Browsers, resolvers, and
type browsers share the same packet-fan-out infrastructure.

---

## Shipped

### Value types (all `PROMEKI_DATATYPE`)

- `MdnsServiceType` — `_app._proto[.domain]` triple, RFC 6763 §4 parsing, subtype browse FQDN helper.
- `MdnsTxtRecord` — RFC 6763 §6 key/value collection with `Presence` enum (KeyOnly / Empty / Present), case-insensitive keys, UTF-8-boundary-aware byte truncation at `MaxEntryBytes`.
- `MdnsServiceInstance` — instance carrier (type + instanceName + hostname + port + v4/v6 + txt + ifindex + lastSeen + ttl).  `operator==` is identity (type + instanceName, case-insensitive); `hasSameContent` and `hasSameSnapshot` cover the broader comparisons.
- `MdnsRecord` — publish-side RR with named factories (`ptr` / `srv` / `txt` / `a` / `aaaa`), cache-flush bit defaulting per type.

### Wire layer

- `MdnsPacket`, `MdnsParsedRecord`, `MdnsParsedQuestion` are now **using-aliases** of `DnsPacket`, `DnsRecord`, `DnsQuestion` (generalised 2026-05-31; see `dns.md`).  All mDNS callers remain source-compatible; no rename required.
- `mdnsBuildAnnounce` / `mdnsBuildGoodbye` / `mdnsBuildProbe` — outbound packet encoders using the new `DnsPacket::Builder`.  Uncompressed name encoding.  PTR cache-flush bit forcibly suppressed per RFC 6762 §10.2.
- `mdnsname.h` — `mdnsEscapeLabel` / `mdnsUnescapeLabel` / `mdnsSplitName` / `mdnsJoinName` helpers.  Numeric `\DDD` byte escapes accepted on input but not generated.

### Engine

- `MdnsManager` (`Thread` subclass) — owns:
  - Dual IPv4 + IPv6 sockets (configurable via `setIpFamily`), wildcard-bound to port 5353 (default).
  - Multicast group joins on `224.0.0.251` (v4) and `ff02::fb` (v6) per joined interface.
  - `IP_PKTINFO` / `IPV6_PKTINFO` for per-packet ingress interface attribution.
  - Receive thread driven by the worker `EventLoop`: each socket is an `addIoSource(fd, IoRead | IoError)` registration; a single `startTimer(_tickIntervalMs, repeating)` drives housekeeping; `IoError` emits `receiveErrorSignal`. `stop()` shuts down via `Thread::quit(0)` — no stop-flag polling. Outbound TTL=255 per RFC 6762 §11; per-interface egress steering via `IP_MULTICAST_IF` / `IPV6_MULTICAST_IF` fan-out.
  - `run()` calls `moveToThread(this)` first thing so every signal connected with the manager as the owner — including the `NetworkInterfaceMonitor` slots wired up in `attachInterfaceMonitor` — dispatches via the worker loop. The monitor itself still lives on the constructing thread; only the slot routing changes.
  - Browser / resolver / publisher / type-browser registration lists; fan-out under mutex with the "unregister blocks during in-flight" race contract.
  - Housekeeping ticks driving every registered consumer's `onManagerTick`.
- `Application::mdnsManager()` — lazy-create global with best-effort auto-start.

### Consumer classes

- `MdnsBrowser` — discovers instances of one service type.  PTR-driven cache, signals `serviceFound` / `serviceUpdated` / `serviceLost`, continuous-query backoff (RFC 6762 §5.2 with 1 s → 1 hour cap), directed follow-up SRV/TXT/A/AAAA queries on partial info, RFC 6762 §10.2 cache-flush semantics with 1 s grace window, and RFC 6762 §7.1 Known-Answer Suppression in continuous queries.
- `MdnsTypeBrowser` — discovers the set of service types currently advertised on the link via the RFC 6763 §9 meta-query `_services._dns-sd._udp.local.`.
- `MdnsResolver` — one-shot resolution of a known instance.  Wraps an internal `MdnsBrowser`, filters to the target instance name, fires `resolved` / `failed` with a configurable timeout.
- `MdnsPublisher` — full RFC 6762 publisher state machine: `Idle → Probing → Announcing → Published → Conflicted / Withdrawing`.  Three probes at 250 ms intervals, two announces at 1 s, periodic re-announce on smallest-TTL/2, Goodbye on `withdraw`.  Probes the unique-by-name records (SRV/TXT/A/AAAA) only — never the shared PTR owner.  Probe-window conflicts trigger RFC 6762 §9 auto-rename by default (`"Studio Camera" → "(2)" → "(3)" …`, capped at `MaxRenameAttempts`); manual-rename mode (`setAutoRename(false)`) routes the conflict to `conflictSignal` instead.  Inbound queries in `Published` state schedule a jittered response (RFC 6762 §6, 20–120 ms uniform) via the manager's worker `EventLoop` and apply RFC 6762 §7.1 known-answer suppression against the requester's Answer section — records whose requester-side TTL has aged below the half-life threshold pass through unsuppressed so the cache gets refreshed before it expires.

### Tests (150 doctest cases at last count)

Per-class unit tests covering value-type round-trips (Variant, DataStream), parser correctness, browser cache dynamics, publisher state-machine transitions including auto-rename / manual-rename / give-up paths and the responder-side KAS filter (fresh-TTL suppression, half-life refresh, content mismatch, wrong section), resolver completion criteria, label escaping, KAS / QU bit / dual-family socket behaviour.

---

## Deferred Items

- [ ] **NSEC negative-response parsing** — `MdnsParsedRecord::Type::Nsec` not modelled.  Forward-compat as `Unknown` for now; needed for a strict publisher response that includes negative-cache info.
- [ ] **Per-interface sockets** — current model is wildcard-bound sockets with `IP_PKTINFO` for ingress and per-write `IP_MULTICAST_IF` for egress.  Per-interface sockets would simplify the egress fan-out and let the OS kernel demux ingress directly.  Significant refactor; only useful at large multi-NIC scale.
- [ ] **Windows ingress attribution** — `setReceivePktInfo` returns `NotSupported` on Windows; the receive path falls back to the legacy serialised read with `NetworkInterface()` as the ingress.  Needs a Winsock `WSARecvMsg` port to match Linux.

---

## Architecture notes

- All discovery / publish classes use the same raw-pointer-with-mutex registration contract: `setManager(mgr)` adds the consumer to the manager's per-class list, `~Consumer` calls `setManager(nullptr)` which `unregisterX` blocks on any in-flight callback.  This is the same pattern Qt's `QObject` uses for parent/child ownership.
- Cross-thread signal dispatch goes through each subscriber's `ObjectBase::eventLoop()` per the project's standard `connect(Signal*, Function, ObjectBase*)` overload.  The manager's worker thread runs its `EventLoop` directly (via `Thread::run`'s default `exec()`), and `run()` calls `moveToThread(this)` first thing — so `NetworkInterfaceMonitor` signal slots wired with the manager as the owner dispatch to the worker loop, keeping the engine-mutation surface single-threaded.
- Name encoding is uniformly uncompressed.  RFC 1035 §4.1.4 lets the sender choose — skipping compression keeps the encoder small + deterministic + safe against the historic "pointer loop" parser bugs that plague compressed name decoders.  Receivers (mjansson) accept either form transparently.
- The cache-flush bit on `MdnsRecord` is honoured for SRV / TXT / A / AAAA and force-masked off for PTR per RFC 6762 §10.2 regardless of the user's setting.
- Label escaping follows RFC 1035 §5.1 master-file conventions.  `MdnsServiceInstance::fqdn()` escapes the instance label; the parser produces escape-aware text from the wire; the encoder splits on unescaped `.`.  Round-trip equality holds for instances with embedded `.` and `\` in their labels.
