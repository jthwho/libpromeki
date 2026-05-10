# NetworkInterface — Cross-Platform Build-Out

**Library:** `promeki` (feature flag `PROMEKI_ENABLE_NETWORK`)
**Status:** Stages 1, 2, 4, and 5 landed 2026-05-09. Stage 3 (Windows) pending.
**Today:** Stable impl handles, POSIX+Linux+BSD enumeration with kind/speed/duplex/carrier,
`NetworkRouting` (default gateway, source-address selection, DNS), `NetworkInterfaceMonitor`
with Linux netlink + Emscripten stub. TTL-cached enumeration. No Windows backend yet.

This plan turns `NetworkInterface` into a fully-featured cross-platform NIC
inventory with live state, change notifications, and route/DNS context. The
target is **production-grade**: thread-safe concurrent access, stable handles
across enumerations, no leaks under interface churn, push-based notifications
without polling, and a clean separation between discovery, identity, and
routing.

## Conventions

All work follows `CODING_STANDARDS.md` and the existing devplan conventions:

- Every public symbol gets Doxygen, including class-level `@par Thread Safety`
  per `CODING_STANDARDS.md` lines 841-848 (Thread-safe / Not thread-safe /
  Conditionally thread-safe / Thread-affine).
- Errors via `Error` returns, `Result<T>`, or `Error *err` out-params; never
  `bool *ok`.
- Logging via `promekiWarn` / `promekiInfo` / `promekiDebug` / `promekiError`.
  Every new `.cpp` that emits `promekiDebug()` registers a module name with
  `PROMEKI_DEBUG(ModuleName)` near the top (CODING_STANDARDS lines 932-945).
- Library containers (`List<T>`, `Map<K,V>`, `Set<T>`, `String`) in public
  APIs and in implementation; never raw STL where a wrapper exists.
- Library synchronization wrappers (`Mutex`, `ReadWriteLock`, `WaitCondition`,
  `Atomic<T>`) — never `std::mutex` / `std::shared_mutex` / etc. directly.
- Platform fences via `PROMEKI_PLATFORM_LINUX` / `PROMEKI_PLATFORM_BSD` /
  `PROMEKI_PLATFORM_POSIX` / `PROMEKI_PLATFORM_WINDOWS` /
  `PROMEKI_PLATFORM_EMSCRIPTEN`.
- Tests via doctest under `tests/unit/network/`, one TU per logical class.
- Free `TextStream &operator<<` overloads for new public data types live in
  the type's own header (CODING_STANDARDS lines 754-770), forward-declaring
  `TextStream` to avoid circular includes.
- ObjectBase-derived classes use `PROMEKI_OBJECT(ClassName, ParentClassName)`
  with both args (line 731), and `PROMEKI_SIGNAL` / `PROMEKI_SLOT` for
  signals/slots.
- Blocking calls accept `unsigned int timeoutMs = 0` (line 14); `0` means
  wait indefinitely. Non-blocking duration knobs (e.g. debounce window)
  also use `unsigned int` for consistency with `timeoutMs`.

### Object-category placement

| Type                         | Category                | Notes                                                                                   |
|------------------------------|-------------------------|-----------------------------------------------------------------------------------------|
| `NetworkInterface`           | Data object (value handle) | Cheap copy via `NetworkInterfaceImplPtr`; no internal sync (impl handles it).         |
| `NetworkInterfaceData`       | Data object (POD)       | Plain struct, copyable; **not** internally thread-safe — users get value snapshots.    |
| `NetworkInterfaceStats`      | Data object (POD)       | Same as above.                                                                          |
| `NetworkInterfaceImpl`       | **Utility/infrastructure** | Backend-owned, virtual-dispatching, reference-counted; thread-safe per standards line 57 (utility classes are often thread-safe and document it). Internal `ReadWriteLock` is appropriate here precisely because this is *not* a data object. |
| `NetworkInterfaceBackend`    | Utility/infrastructure  | Plug-in source; registry is internally synchronized.                                    |
| `NetworkInterfaceMonitor`    | Functional (ObjectBase) | Identity, signals, thread-affine.                                                       |
| `NetworkRouting`             | Utility (static-only)   | Constructor + destructor `= delete`'d; never instantiated.                              |
| `NetworkInterfaceKind`       | TypedEnum               | In `enums.h`, registered via `PROMEKI_REGISTER_ENUM_TYPE`.                              |

---

## Design Decisions

### Value handle stays a value handle

`NetworkInterface` is *not* promoted to `ObjectBase`. Promotion would break
the return-by-value semantics every caller (including `RtpMediaIO`'s
`firstNonLoopback().macAddress()` default at `rtpmediaio.cpp:1787`) depends
on, and would force a thread affinity onto a type that's currently a cheap
shared-pointer wrapper.

### One impl per interface, for the library's lifetime

Today every `NetworkInterfaceBackend::enumerate()` call mints fresh
`NetworkInterfaceImpl` instances — which is why `operator==` (`_d == o._d`)
returns false for two handles that point at the same physical NIC. We fix
this by stabilizing impls at the registry level:

- Backends stay simple — `enumerate()` keeps returning freshly-minted impls
  with current snapshot data. No per-backend caching code.
- `NetworkInterfaceBackend::enumerateAll()` owns a process-global
  `Map<ImplKey, NetworkInterfaceImplPtr>` where `ImplKey` is
  `(backendName, ifName, ifIndex)`. Index is part of the key so an iface
  that disappears and a same-name one with a different kernel index don't
  alias.

**The exact `enumerateAll()` algorithm**, all under one mutex:

1. Walk registered backends in priority order (lowest priority first).
2. Per-backend, walk produced impls in order.
3. For each impl, if `ifName` is already in this call's `seenNames` set
   (claimed by a higher-priority backend earlier this cycle), drop without
   touching the cache. *Cross-backend dedup happens before the cache write
   so lower-priority duplicates never accumulate.*
4. Otherwise insert into `seenNames`, look up `ImplKey` in the cache:
   - **Hit:** call `cached->replaceData(impl->data())`, push `cached` to
     the result list, drop the freshly-minted impl.
   - **Miss:** push the freshly-minted impl, store it in the cache.
   - Track this `ImplKey` as "refreshed this cycle".
5. After all backends have been walked, for every cache entry whose
   `ImplKey` was *not* refreshed this cycle: call `replaceData` with a
   "down snapshot" (clear `ipv4Subnets`, `ipv6Subnets`; set
   `isUp=false`, `isRunning=false`, `hasCarrier=false`; preserve `name`,
   `friendlyName`, `index`, `macAddresses`, `kind` for diagnostics), then
   drop the entry from the cache.
6. Held callers' handles outlive the cache eviction (the SharedPtr keeps
   the impl alive); they observe the down snapshot.

`unregisterBackend(name)` follows the same pattern: refresh each affected
impl to the down state, then drop the cache entries. Callers holding handles
through unregister see the iface go down rather than freeze on the last
snapshot.

**Consequences:**

- `operator==` keeps `_d == o._d` and is now correct by construction.
- Stage 4 monitor signals carry handles whose pointer identity matches
  earlier-cached handles — subscribers' slots compare with `==` directly.
- An iface that goes away leaves callers' handles valid but reporting
  `isUp() == false`, `ipv4Subnets().isEmpty() == true`, etc.
- Cache size is bounded by currently-up interfaces; no slow growth on
  repeated USB-Ethernet plug/unplug cycles.

### Snapshot reads are race-free with `replaceData`

The impl-stabilization model means the registry mutates an impl's snapshot
(`replaceData`) while other threads read via accessors. To make this
race-free without a lock-on-every-field-read pattern:

- `NetworkInterfaceImpl::data()` returns `NetworkInterfaceData` *by value*.
  Implementation: `ReadWriteLock` (the library wrapper around
  `std::shared_mutex`, `include/promeki/readwritelock.h`) around inline
  `_data`; `data()` takes a `ReadWriteLock::ReadLocker` + struct copy;
  `replaceData()` takes a `ReadWriteLock::WriteLocker` + assigns. Use the
  library type — never reach for `std::shared_mutex` directly per project
  conventions ("Prefer library's own classes over std").
- `NetworkInterfaceData` is cheap to copy: most fields are POD; the heap
  members are CoW (`String`) or short vectors.
- All `NetworkInterface` accessors that previously returned by reference
  now return by value. See Stage 1 for the full signature list.
- New `NetworkInterfaceData NetworkInterface::data() const` for callers that
  read multiple fields — one lock + copy instead of N. Production pattern:
  ```cpp
  auto snap = iface.data();
  log("iface %s mac=%s addrs=%zu",
      snap.name.cstr(),
      snap.macAddresses[0].toString().cstr(),
      snap.ipv4Subnets.size());
  ```
- `NetworkInterfaceImpl`'s `protected NetworkInterfaceData &mutableData()`
  goes away — backends construct via the existing constructor and never
  mutate after handing the impl to the registry.

### Live state and signals live in `NetworkInterfaceMonitor`

A new `NetworkInterfaceMonitor : ObjectBase` owns the OS notification source
(Linux `AF_NETLINK`, BSD/macOS `PF_ROUTE`, Windows `NotifyIpInterfaceChange` +
`NotifyUnicastIpAddressChange`) and emits signals.

The monitor *does not* maintain its own enumerated cache. Stage 1's impl
cache and Stage 5's enumeration TTL handle that. The monitor receives OS
events, debounces them, invalidates the enumeration TTL, runs
`enumerateAll()` (which refreshes all impls in place via `replaceData`),
diffs against previous-cycle data, and emits per-iface signals.

**The diff cannot be computed from impl pointers alone** — those impls get
mutated by `enumerateAll()`, so "before" and "after" both read the same
memory. The monitor caches data *values* in
`Map<NetworkInterfaceImpl *, PreviousEntry> _previousData` keyed by raw
impl pointer, where:

```cpp
struct PreviousEntry {
        NetworkInterfaceImplPtr impl;  // keeps refcount across cycles
        NetworkInterfaceData    data;  // last-cycle snapshot
};
```

Raw pointer as the map key is cheap and avoids any dependency on
`SharedPtr<T>::operator<` (which the library may or may not provide for
this specialization). The `PreviousEntry` holds a `NetworkInterfaceImplPtr`
so the impl stays alive across diff cycles even if the registry drops the
cache entry mid-cycle (e.g. via `unregisterBackend`). Per cycle:

1. `current = NetworkInterfaceBackend::enumerateAll()` (mutates impls in place).
2. For each impl in `current`:
   - Not in `_previousData` → emit `interfaceAdded`.
   - In `_previousData`, `isRunning` flipped → emit `linkUp` / `linkDown`.
   - Address set added/removed → emit `addressAdded` / `addressRemoved` per
     address (compute set difference between old and new
     `ipv4Subnets`/`ipv6Subnets`).
3. For each impl in `_previousData` not in `current` → emit
   `interfaceRemoved` (the impl is now in the down state per the registry's
   disappearance handling).
4. Rebuild `_previousData` from `current`'s `data()` snapshots.
5. Emit `interfacesChanged()` once.

Signals (all carry `NetworkInterface` by value):

- `PROMEKI_SIGNAL(interfaceAdded, NetworkInterface)`
- `PROMEKI_SIGNAL(interfaceRemoved, NetworkInterface)`
- `PROMEKI_SIGNAL(linkUp, NetworkInterface)`
- `PROMEKI_SIGNAL(linkDown, NetworkInterface)`
- `PROMEKI_SIGNAL(addressAdded, NetworkInterface, Ipv4Address)` and IPv6 overload
- `PROMEKI_SIGNAL(addressRemoved, NetworkInterface, Ipv4Address)` and IPv6 overload
- `PROMEKI_SIGNAL(interfacesChanged)` — coalesced "go re-read" hook

The monitor is opt-in. Programs that don't construct one keep today's
zero-thread, zero-netlink-fd profile. Coalescing window defaults to 50 ms.

### Snapshot fields grow, but stay POD

`NetworkInterfaceData` adds new fields (kind, friendlyName, linkSpeedMbps,
fullDuplex, hasCarrier). Backends fill what they can; consumers read what
they need. No virtual dispatch added for any of the new fields. Stats
counters keep their virtual `stats()` accessor — they're computed on-demand
from native sources, not from `_data`.

---

## Stage 1 — Stable impls, snapshot model, POSIX gap-fills, display polish

The architectural foundation. Three intertwined threads of work touching the
same files. No new threads, no notifications.

**Files (modify):**
- [x] `include/promeki/networkinterfaceimpl.h` — extend `NetworkInterfaceData`,
      add public `replaceData(...)`, drop `mutableData()`, switch to
      thread-safe by-value `data()` (see "Snapshot reads are race-free").
- [x] `include/promeki/networkinterface.h` — accessors for new fields,
      new `data()` accessor, `toString()`, `operator<<`. Update accessor
      signatures to return by value.
- [x] `include/promeki/enums.h` — add `NetworkInterfaceKind` `TypedEnum`
      (Unknown / Ethernet / Wifi / Loopback / Tunnel / Bridge / Vlan /
      Virtual / Cellular / PointToPoint).
- [x] `src/network/networkinterface.cpp` — new accessors, `toString()`;
      update existing accessors to return by value.
- [x] `src/network/networkinterfacebackend.cpp` — impl-cache rewrite of
      `enumerateAll()` per the algorithm above; `unregisterBackend` cleanup.
- [x] `src/network/posixnetworkinterfacebackend.cpp` — populate new fields
      (kind, link speed/duplex, BSD/macOS MTU).
- [x] `tests/unit/network/networkinterface.cpp` — coverage for new fields,
      stable-impl tests, concurrency test, update tests that bound accessor
      results to `const auto &`.
- [x] `CMakeLists.txt` — no new TUs in this stage; existing
      `posixnetworkinterfacebackend.cpp`, `networkinterface.cpp`,
      `networkinterfacebackend.cpp` already listed.

**`NetworkInterfaceData` additions:**
- [x] `String   friendlyName` — equals `name` on POSIX; Windows fills
      from `IP_ADAPTER_ADDRESSES.FriendlyName`.
- [x] `NetworkInterfaceKind kind = NetworkInterfaceKind::Unknown`
- [x] `uint64_t linkSpeedMbps = 0` — 0 if unknown.
- [x] `bool     fullDuplex = false`
- [x] `bool     hasCarrier = false` — same value as `isRunning` semantically;
      kept as a clearer-named alias for new code.

**Impl-stabilization (the architecture move):**
- [x] Public `void NetworkInterfaceImpl::replaceData(NetworkInterfaceData)`
      — copies under `ReadWriteLock::WriteLocker`.
- [x] Drop `protected NetworkInterfaceData &mutableData()`.
- [x] `data()` becomes by-value under `ReadWriteLock::ReadLocker`.
- [x] Update `NetworkInterfaceImpl`'s class-level Doxygen `@par Thread Safety`
      block to "All public methods are safe to call concurrently from
      multiple threads. `data()` and `replaceData()` are race-free via an
      internal `ReadWriteLock`." (Replaces the existing "concurrent reads on
      a single instance are safe" wording, which becomes incorrect.)
- [x] Define `ImplKey` in `networkinterfacebackend.cpp` (TU-private):
      ```cpp
      struct ImplKey {
              String   backendName;
              String   ifName;
              uint32_t ifIndex = 0;
              bool operator<(const ImplKey &o) const;
              bool operator==(const ImplKey &o) const;
      };
      ```
- [x] `using ImplCache = Map<ImplKey, NetworkInterfaceImplPtr>;` TU-private
      alias; static `ImplCache _implCache`, protected by `Registry::mutex`.
- [x] Helper `static NetworkInterfaceData makeDownSnapshot(const NetworkInterfaceData &)`
      producing the cleared-addresses / no-carrier snapshot.
- [x] `enumerateAll()` rewrite per the Design Decisions algorithm:
      cross-backend dedup before cache-write (using `Set<String> seenNames`);
      per-cycle `Set<ImplKey> refreshed` tracking; down-snapshot + cache
      eviction for non-refreshed entries.
- [x] `unregisterBackend(name)` extended: for each cache entry tagged with
      `name`, refresh to down snapshot, then drop.

**POSIX backend work:**
- [x] BSD/macOS MTU via `if_data` on `AF_LINK` ifaddrs entries (currently
      Linux-only via `SIOCGIFMTU`). Wire into the existing `extractMac`
      pass; the second-pass `SIOCGIFMTU` block becomes Linux-only.
- [x] Linux link speed/duplex via `/sys/class/net/<name>/speed` and
      `/duplex` (string read; speed `-1` leaves 0). Skip ETHTOOL ioctl —
      it needs `CAP_NET_ADMIN` for some modes.
- [x] Linux interface kind:
  - `/sys/class/net/<name>/wireless/` exists → `Wifi`
  - `/sys/class/net/<name>/bridge/` → `Bridge`
  - `/sys/class/net/<name>/bonding/` → `Virtual`
  - `IFF_LOOPBACK` → `Loopback`
  - `/sys/class/net/<name>/type` `1` (ARPHRD_ETHER) and not above → `Ethernet`
  - `/sys/class/net/<name>/type` `768`/`769` (TUNNEL/TUNNEL6) → `Tunnel`
  - `/sys/class/net/<name>/device/driver` `tun` → `Tunnel`
  - VLAN: `/proc/net/vlan/<name>` exists or `/sys/class/net/<name>/upper_*`
    → `Vlan`
  - Default → `Unknown`
- [x] BSD/macOS interface kind via `if_data.ifi_type` (IFT_ETHER, IFT_LOOP,
      IFT_TUNNEL, IFT_BRIDGE) and `SIOCGIFMEDIA` for WiFi detection.

**`NetworkInterface` API changes — full signature list:**

| Accessor                        | Old return                          | New return                  |
|---------------------------------|-------------------------------------|-----------------------------|
| `name()`                        | `const String &`                    | `String`                    |
| `allMacAddresses()`             | `const MacAddress::List &`          | `MacAddress::List`          |
| `ipv4Subnets()`                 | `const Ipv4Subnet::List &`          | `Ipv4Subnet::List`          |
| `ipv6Subnets()`                 | `const Ipv6Subnet::List &`          | `Ipv6Subnet::List`          |

**Additions:**
- [x] `String friendlyName() const`
- [x] `NetworkInterfaceKind kind() const`
- [x] `uint64_t linkSpeedMbps() const`
- [x] `bool fullDuplex() const`
- [x] `bool hasCarrier() const` — alias for `isRunning()`, kept for clarity
- [x] `NetworkInterfaceData data() const` — single-snapshot batch read
- [x] `String toString() const` — single-line "name (friendly) [kind] up/down
      MAC=... addrs=..." per libpromeki convention.
- [x] `TextStream &operator<<(TextStream &, const NetworkInterface &)` —
      free overload in `networkinterface.h` per CODING_STANDARDS lines
      754-770; forward-declare `TextStream`.
- [x] `PROMEKI_FORMAT_VIA_TOSTRING(promeki::NetworkInterface)`

**Stream operators for the public structs (in their own headers):**
- [x] `TextStream &operator<<(TextStream &, const NetworkInterfaceData &)` in
      `networkinterfaceimpl.h` — multi-line dump of every field; useful for
      `promekiDebug()` from caller code reading `iface.data()`.
- [x] `TextStream &operator<<(TextStream &, const NetworkInterfaceStats &)` in
      `networkinterfaceimpl.h` — single-line "rx=B/P/E/D tx=B/P/E/D
      [valid|invalid]"; useful for telemetry logging.
- [x] `PROMEKI_FORMAT_VIA_TOSTRING` is not applicable to plain structs;
      callers stream them through `TextStream` directly when they want
      formatted output.

**Tests:**
- [x] Loopback's `kind()` is `NetworkInterfaceKind::Loopback`.
- [x] First non-loopback iface (when present) reports `Ethernet` or `Wifi`.
- [x] `toString()` is non-empty and contains the iface name.
- [x] `friendlyName()` equals `name()` on POSIX.
- [x] `data()` content matches per-field accessors.
- [x] Stable impls: two `enumerate()` calls produce element-wise `==`-equal
      lists. Update existing
      `tests/unit/network/networkinterface.cpp:64-69` workaround.
- [x] Disappearance: register `FakeBackend` returning `fake0`, enumerate,
      hold a handle. Re-register a `FakeBackend` whose output omits `fake0`.
      Held handle reports `isUp() == false`, `ipv4Subnets().isEmpty()`.
      `findByName("fake0")` returns invalid (cache evicted).
- [x] Unregister cleanup: register `FakeBackend`, enumerate, hold a handle,
      `unregisterBackend("fake")`. Held handle reports `isUp() == false`.
- [x] Concurrency: spawn thread A calling `iface.data()` in a tight loop,
      thread B calling `impl->replaceData(snap)` alternating between two
      snapshots, run for 100 ms; assert every snapshot read by A is
      internally consistent with one of the two written snapshots (no torn
      reads).
- [x] Cross-backend dedup: register two `FakeBackend` instances at
      priorities 5 and 50, both returning `fake0`. `enumerate()` returns
      one entry; cache contains exactly one entry tagged with the
      higher-priority backend.

---

## Stage 2 — Stats, NetworkRouting, DNS

POSIX/Linux first; Windows lights up these accessors in Stage 3 by reusing
the same fields and a Windows route-table impl.

Routing/source-address/gateway/DNS lookups go on a new `NetworkRouting`
class rather than as static methods on `NetworkInterface`. Routing is
expected to grow (policy routing, per-iface route lists, default-route
metric preferences, on-link/off-link helpers, route-add/remove someday).
Pinning all that to `NetworkInterface`'s static surface bloats a class
whose job is "describe one NIC". `NetworkRouting` owns that surface
independently and can expand without bothering `NetworkInterface`.

`NetworkRouting` is a **utility class** (per CODING_STANDARDS line 51-69):
no instance state, all methods static, constructor + destructor `= delete`'d
so callers can't accidentally instantiate it. Pattern:

```cpp
class NetworkRouting {
        public:
                NetworkRouting()  = delete;
                ~NetworkRouting() = delete;
                // all methods static
                static NetworkAddress defaultGatewayIpv4();
                // ...
};
```

**Files (modify):**
- [x] `include/promeki/networkinterfaceimpl.h` — `NetworkInterfaceStats`
      already declared (`networkinterfaceimpl.h:51-61`); no changes.
- [x] `src/network/posixnetworkinterfacebackend.cpp` — implement
      `PosixInterfaceImpl::stats()`.
- [x] `tests/unit/network/networkinterface.cpp` — stats sanity tests.
- [x] `CMakeLists.txt` — add new TUs (see below).

**Files (new):**
- [x] `include/promeki/networkrouting.h` — public API.
- [x] `src/network/networkrouting.cpp` — common code; platform impls
      delegate via internal hooks.
- [x] `src/network/posixnetworkrouting.cpp` — Linux/BSD route-table impl,
      guarded by `PROMEKI_PLATFORM_POSIX`.
- [x] `tests/unit/network/networkrouting.cpp` — gateway lookup,
      source-address rules, DNS server enumeration.

**`PosixInterfaceImpl::stats()`:** read
`/sys/class/net/<name>/statistics/{rx,tx}_{bytes,packets,errors,dropped}`
once per call; return `valid=true` on success, `valid=false` if the
directory doesn't exist (e.g. some tun device kernels). No caching at the
impl level — callers polling stats hot are expected to rate-limit
themselves. **Note for delta consumers:** counters can reset to zero on
iface bounce, so consumers computing rates must handle a backwards step
(treat negative delta as "session reset, drop this sample").

**`NetworkRouting` API (all-static, value-result):**
- [x] `static NetworkAddress defaultGatewayIpv4()` — first default route
      (lowest metric); `NetworkAddress()` if none.
- [x] `static NetworkAddress defaultGatewayIpv6()` — same for IPv6.
- [x] `static NetworkInterface defaultRouteInterface(int family = AF_INET)`
      — iface that owns the default gateway.
- [x] `static Ipv4Address sourceAddressFor(const Ipv4Address &dest)` — pick
      via `NetworkInterface::findRoutesTo(dest)`, return the first address
      whose subnet contains `dest`, falling back to the default-route
      iface's primary address; multicast destinations default to the
      default-route iface's primary address.
- [x] `static Ipv6Address sourceAddressFor(const Ipv6Address &dest)` — same;
      link-local destinations require explicit iface, so return null
      (a future `sourceAddressFor(dest, iface)` overload covers the
      explicit case).
- [x] `static List<NetworkAddress> dnsServers()` — global resolvers in
      `/etc/resolv.conf` order; Stage 3 Windows impl pulls from
      `GetAdaptersAddresses().FirstDnsServerAddress`.
- [x] **Future hooks (API space reserved, not implemented this stage):**
      `routesFor(dest)` returning every matching route record (metric +
      gateway + iface), `addRoute()` / `removeRoute()` admin operations.

**POSIX route-table read:** Linux: parse `/proc/net/route` for IPv4 and
`/proc/net/ipv6_route` for IPv6 — enough for default-gateway and metric
ordering without netlink (netlink stays in Stage 4 for notifications).
BSD/macOS: `sysctl({CTL_NET, PF_ROUTE, 0, AF_INET, NET_RT_DUMP, 0})` walk.

Per-interface DNS (Linux `systemd-resolved` D-Bus, etc.) is deferred — see
Out of Scope.

**Tests:**
- [x] `NetworkRouting::defaultGatewayIpv4()` is either null or owned by an
      up iface (CI containers may legitimately have no default route).
- [x] `NetworkRouting::sourceAddressFor(127.0.0.1)` returns 127.0.0.1.
- [x] `NetworkInterface::stats()` on the loopback returns `valid=true`
      with monotonically non-decreasing counters across two reads
      (modulo the iface-bounce caveat above; loopback doesn't bounce in
      a test).
- [x] `NetworkRouting::dnsServers()` is either empty or every entry is
      `isResolved()`.

---

## Stage 3 — Windows backend

Pure-additive: registers a `WindowsNetworkInterfaceBackend` through the
existing `NetworkInterfaceBackend` plug-in registry on
`PROMEKI_PLATFORM_WINDOWS`. Reuses every `NetworkInterfaceData` field added
in Stages 1–2.

**Files (new):**
- [ ] `src/network/windowsnetworkinterfacebackend.cpp` — guarded by
      `PROMEKI_PLATFORM_WINDOWS`. Contains both the backend class and its
      `WindowsInterfaceImpl : NetworkInterfaceImpl` subclass overriding
      `stats()` to read `GetIfEntry2`.
- [ ] `src/network/windowsnetworkrouting.cpp` — Windows impl of the
      Stage 2 `NetworkRouting` helpers via `GetIpForwardTable2` and
      `GetAdaptersAddresses` for DNS.

**Files (modify):**
- [ ] `CMakeLists.txt` — add the two TUs above when
      `PROMEKI_PLATFORM_WINDOWS`; link `iphlpapi.lib`.
- [ ] `tests/unit/network/networkinterface.cpp` — add Windows-only test
      cases; existing loopback-name test gated since Windows loopback name
      is a GUID with friendlyName "Loopback Pseudo-Interface 1".

**Implementation:** single `GetAdaptersAddresses(AF_UNSPEC,
GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_INCLUDE_GATEWAYS, …)` returns IPs,
prefix lengths, MAC, MTU, friendly name, IfType, TransmitLinkSpeed, gateway
list. No per-iface ioctls.

- [ ] Backend `name() = "windows"`, `priority() = 100`.
- [ ] IfType mapping: `IF_TYPE_ETHERNET_CSMACD` → `Ethernet`,
      `IF_TYPE_IEEE80211` → `Wifi`, `IF_TYPE_SOFTWARE_LOOPBACK` →
      `Loopback`, `IF_TYPE_TUNNEL` → `Tunnel`, `IF_TYPE_PPP` →
      `PointToPoint`, `IF_TYPE_WWANPP*` → `Cellular`, else `Unknown`.
- [ ] `WindowsInterfaceImpl::stats()` reads `GetIfEntry2(IfIndex)`, fills
      `InOctets/InUcastPkts/InErrors/InDiscards` and `Out*` counterparts.
- [ ] `NetworkRouting::dnsServers()` walks
      `IP_ADAPTER_ADDRESSES.FirstDnsServerAddress` linked list across
      adapters, dedups, returns globally.
- [ ] `NetworkRouting::defaultGatewayIpv4()` /
      `defaultGatewayIpv6()`: `GetIpForwardTable2(family)` → entry with
      `DestinationPrefix.PrefixLength == 0`, lowest metric.
- [ ] Auto-register via the `Registrar` static-init pattern the POSIX
      backend uses.
- [ ] `linkSpeedMbps` from `TransmitLinkSpeed` (bits/s, divide by 1e6);
      `fullDuplex` from interface media type / `IfType` (Wifi is half-duplex
      conceptually, Ethernet full).

**Tests (Windows-only, gated by `PROMEKI_PLATFORM_WINDOWS`):**
- [ ] At least one adapter is enumerated.
- [ ] `friendlyName()` is non-empty for every adapter.
- [ ] `linkSpeedMbps()` is non-zero for at least one up Ethernet/Wifi
      adapter (skip with `MESSAGE` if no up non-loopback adapter — common
      in headless CI runners).
- [ ] `NetworkRouting::dnsServers()` is non-empty when at least one adapter
      has DNS configured.

---

## Stage 4 — `NetworkInterfaceMonitor` (ObjectBase + signals)

All earlier stages are polled snapshots; this stage adds push-based
notifications.

**Files (new):**
- [x] `include/promeki/networkinterfacemonitor.h`
- [x] `src/network/networkinterfacemonitor.cpp` — common code: signals,
      `_previousData` diff, debounce timer, registry of started monitors.
- [x] `src/network/linuxnetworkinterfacemonitor.cpp` — netlink impl,
      guarded by `PROMEKI_PLATFORM_LINUX`.
- [ ] `src/network/bsdnetworkinterfacemonitor.cpp` — `PF_ROUTE` impl,
      guarded by `PROMEKI_PLATFORM_BSD`. (pending)
- [ ] `src/network/windowsnetworkinterfacemonitor.cpp` —
      `NotifyIpInterfaceChange` + `NotifyUnicastIpAddressChange`, guarded
      by `PROMEKI_PLATFORM_WINDOWS`. (pending — see Stage 3)
- [x] `tests/unit/network/networkinterfacemonitor.cpp`

**Files (modify):**
- [x] `CMakeLists.txt` — add the four new TUs above under their platform
      guards; the common TU is unconditional.

**`NetworkInterfaceMonitor` API:**
- [x] Derive from `ObjectBase`; `PROMEKI_OBJECT(NetworkInterfaceMonitor, ObjectBase)`
      (two args per CODING_STANDARDS line 731).
- [x] Class-level `@par Thread Safety`: "**Thread-affine**: this class must
      only be used from the thread that created it (or the thread it was
      moved to via `moveToThread()`). Signals may be connected from any
      thread." (Standards line 856-859.)
- [x] `NetworkInterfaceMonitor()` — constructs idle.
- [x] `Error start()` — opens platform notification source, primes
      `_previousData` from a first `enumerateAll()` (no signals fired on
      priming), registers fd with `eventLoop()->addIoSource()` (Linux/BSD)
      or platform notify handle (Windows).
- [x] `void stop()` — `removeIoSource` / `CancelMibChangeNotify2`, close
      handle. Does not clear `_previousData`; restart resumes diffing
      cleanly.
- [x] `bool isRunning() const`.
- [x] `void setDebounceMs(unsigned int)` / `unsigned int debounceMs() const`
      — coalescing window (default 50 ms). `unsigned int` matches the
      `timeoutMs` convention (CODING_STANDARDS line 14).
- [x] `static NetworkInterfaceMonitor *anyRunning()` — returns the first
      started monitor in the global registry, or null. Discovery
      convenience for subscribers; explicit dependency injection
      preferred for new code.
- [x] Signals: full list in Design Decisions.

The monitor does **not** expose `interfaces()` — callers use the existing
`NetworkInterface::enumerate()`, which transparently benefits from Stage 1
impl-stabilization and Stage 5 TTL caching.

**Linux netlink wiring:**
- [x] `socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE)`, bind with
      `RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR | RTMGRP_IPV4_ROUTE | RTMGRP_IPV6_ROUTE`.
- [x] On `RTM_NEWLINK` / `RTM_DELLINK` / `RTM_NEWADDR` / `RTM_DELADDR` /
      `RTM_NEWROUTE` / `RTM_DELROUTE`: kick the debounce timer.
- [x] Debounce: one-shot 50 ms `EventLoop::startTimer`; events restart it;
      on fire run the diff cycle.

**BSD/macOS route-socket wiring:**
- [ ] `socket(PF_ROUTE, SOCK_RAW, AF_UNSPEC)` receives `RTM_IFINFO`,
      `RTM_NEWADDR`, `RTM_DELADDR`, `RTM_ADD`, `RTM_DELETE`. Same debounce
      and diff-cycle logic. (pending — BSD backend file not yet added)

**Windows notification wiring:**
- [ ] `NotifyIpInterfaceChange(AF_UNSPEC, callback, this, FALSE, &handle)`
      and `NotifyUnicastIpAddressChange(AF_UNSPEC, callback, this, FALSE,
      &handle)`. (pending — see Stage 3)
- [ ] Callbacks fire on a Windows-managed thread; marshal the kick to the
      monitor's affinity thread via the standard ObjectBase cross-thread
      dispatch so the debounce + diff + signal-emit happens on a single
      EventLoop. (Specific API: `EventLoop::postCallable` if available, or
      synthesize via a `DeferredCall` event posted to the monitor's
      `threadEventLoop()`.) (pending)
- [ ] `CancelMibChangeNotify2(handle)` on `stop()`. (pending)

**Diff cycle (platform-independent, lives in
`networkinterfacemonitor.cpp`):**
- [x] Step 1: call `NetworkInterfaceBackend::invalidateEnumerationCache()`
      (Stage 5), then `current = NetworkInterfaceBackend::enumerateAll()`.
- [x] Step 2: walk `current`. For each impl at raw pointer `p =
      iface.impl().ptr()`: lookup `_previousData[p]`. Absent → emit
      `interfaceAdded`. Present and `isRunning` flipped → emit
      `linkUp` / `linkDown`. Address-set diff → emit `addressAdded` /
      `addressRemoved` per address (set difference of old vs new
      `ipv4Subnets`/`ipv6Subnets`).
- [x] Step 3: walk `_previousData`. For each entry whose pointer is not
      in `current`: emit `interfaceRemoved` carrying
      `NetworkInterface(entry.impl)` (the SharedPtr in `PreviousEntry`
      keeps it alive even if the registry already evicted it).
- [x] Step 4: rebuild `_previousData` from `current` — for each iface in
      `current`, set `_previousData[iface.impl().ptr()] = { iface.impl(),
      iface.data() }`.
- [x] Step 5: emit `interfacesChanged()` once.

**Tests:**
- [x] Construct + start + stop without crashing on a host with at least
      one iface (any Linux runner).
- [x] After `start()`, no signals fire (priming only).
- [x] Synthetic test: register a `FakeBackend`, start monitor. Force a
      rescan via a `static testForceRescan()` hook. Verify `interfaceAdded`
      fires for the fake iface and `interfacesChanged` fires once.
- [x] Debounce: two rapid `testForceRescan()` calls produce one
      `interfacesChanged()` emission.
- [x] Diff fidelity: change a `FakeBackend`'s output between rescans
      (drop one address, add one address); verify `addressAdded` and
      `addressRemoved` fire once each with the correct addresses.
- [x] Signal handle identity: subscriber stashes a handle from
      `interfaceAdded`, later compares with `==` against
      `findByName(handle.name())` — confirms stable-impl identity holds
      across signal hops.

---

## Stage 5 — Re-enumeration TTL + Emscripten stub

Stage 1 already stabilizes handles. Stage 5 adds a TTL on running each
backend's `enumerate()` so a tight loop calling `findRoutesTo` doesn't walk
`getifaddrs` thousands of times per second. Stage 4's monitor invalidates
this TTL on every diff cycle, so subscribers see fresh data within the
debounce window.

**Files (modify):**
- [x] `src/network/networkinterface.cpp` — `enumerate()` and `findBy*` shape
      unchanged; inherits faster lookups via the TTL.
- [x] `src/network/networkinterfacebackend.cpp` — TTL on the backend
      enumeration path; `invalidateEnumerationCache()` API.
- [x] `src/network/networkinterfacemonitor.cpp` — call
      `NetworkInterfaceBackend::invalidateEnumerationCache()` immediately
      before each diff cycle's `enumerateAll()`.
- [x] `tests/unit/network/networkinterface.cpp` — TTL behavior tests.
- [x] `CMakeLists.txt` — add `emscriptennetworkinterfacebackend.cpp` under
      `PROMEKI_PLATFORM_EMSCRIPTEN` guard.

**Files (new):**
- [x] `src/network/emscriptennetworkinterfacebackend.cpp` — registers a
      `wasm` backend that enumerates an empty list so `registeredBackends()`
      isn't empty in WASM builds.

**TTL details:**
- [x] Static `steady_clock::time_point _lastEnumerate` shared across all
      backends (one timestamp keeps the code simpler than per-backend).
- [x] `enumerateAll()` returns the cached list (snapshot of the impl-cache
      contents from the last refresh) when `now - _lastEnumerate < ttl()`,
      skipping every backend's `enumerate()`.
- [x] When the TTL expires, run every backend's `enumerate()` per the
      Stage 1 algorithm.
- [x] `static void invalidateEnumerationCache()` — public, drops the
      timestamp so the next `enumerateAll()` rescans.
- [x] Default TTL: 250 ms (long enough to amortize a "look up by name then
      look up addresses" pair, short enough that polling clients without a
      monitor see changes within a quarter second).
- [x] `static void setEnumerationTtlMs(unsigned int)` /
      `static unsigned int enumerationTtlMs()` for tests (note: actual API
      uses `unsigned int` + `Ms` suffix per project conventions, not
      `std::chrono::milliseconds` as originally planned).

**Tests:**
- [x] With a `FakeBackend` whose output changes between calls, two
      `enumerate()` calls within the TTL window return the same list;
      after `invalidateEnumerationCache()` the next call sees the change.
- [x] `setEnumerationTtlMs(0)` disables the cache; every call re-enumerates.

---

## Production-Grade Use Cases (sanity check)

This plan is sized to support, without further follow-on work:

- **RTP TX picks an outbound iface from an SDP `c=IN IP4 ...` line.**
  `findByIpv4Address()` → `mtu()`, `linkSpeedMbps()` for pacing math,
  `macAddress()` for `ts-refclk:localmac`.
- **RTP RX joins a multicast group on the right NIC** —
  `findRoutesTo(mcast)` returns every up + multicast-capable iface;
  caller filters by `kind() == Ethernet` to skip docker/veth/wlan.
- **Application reacts to cable unplug/replug.**
  `NetworkInterfaceMonitor::linkDown` → tear down sessions;
  `linkUp` → re-attempt joins.
- **Application picks a source address for an outbound flow.**
  `NetworkRouting::sourceAddressFor(dest)` returns the right address;
  `NetworkRouting::defaultGatewayIpv4()` exposes the gateway for
  diagnostics.
- **Telemetry server polls per-NIC byte counters at 1 Hz.**
  `iface.stats()` returns valid counters; consumer handles the bounce-
  reset case per Stage 2 note.
- **Long-running daemon survives USB-Ethernet plug/unplug churn for days
  without leaks.** Disappearance handling drops cache entries; TTL caps
  enumeration cost; monitor signals stay accurate.
- **GUI app shows a network-config picker.** `enumerate()` →
  `friendlyName()` for display, `kind()` for icon, `linkSpeedMbps()` for
  status line.

If any of these can't be served by the API as described, that's a plan bug
— file an update before implementing.

---

## Out of Scope (this plan)

- **Wireless detail** (SSID, signal strength, BSSID). Belongs in a future
  `WifiInterface` accessor — not every consumer of `NetworkInterface`
  should pull `nl80211` / Wireless Extensions / Wlan API.
- **Multicast group membership.** Per-socket, not per-interface; see
  `UdpSocket` / `MulticastManager` / `MulticastReceiver`.
- **Per-interface DNS on Linux.** Requires `systemd-resolved` D-Bus or
  `resolvectl` parsing. Add when a use case appears.
- **Address configuration (admin operations).** Setting addresses, bringing
  links up/down, MTU changes — separate object (`NetworkInterfaceAdmin` or
  similar) needing root privileges. Not part of "fully-featured
  *enumeration*".
- **Cross-process change-event fan-out.** The monitor only fires on the
  local process's view; no IPC.
- **Per-interface signal subscription.** Filter on `NetworkInterface`
  value in your slot — `if (iface.name() != "eth0") return;` is trivial
  and avoids per-NIC ObjectBase weight.
- **ETHTOOL ioctl / `CAP_NET_ADMIN`-gated reads.** sysfs is enough for
  speed / duplex / kind on Linux; ETHTOOL adds a privilege requirement
  many deployments won't have.

---

## Stage Ordering Rationale

Stages 1–2 widen the data model and add the impl-stabilization architecture
without touching threads or notifications — but with **full thread safety**
for concurrent `data()` / `replaceData` access, since Stage 4 will exercise
that. Stage 3 brings Windows in by populating the same widened model — done
before Stage 4 so Windows doesn't lag the notification machinery. Stage 4
layers signals on top without changing the value-handle API. Stage 5
connects the monitor's invalidation hook into the read path so callers get
push-driven freshness for free. Each stage is independently shippable.
