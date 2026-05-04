# NDI setup {#ndi}

How to install the NDI SDK so libpromeki's NDI MediaIO backend
builds and runs on your machine.

NDI (Network Device Interface) is a low-latency IP video / audio
protocol developed by NewTek (now Vizrt). libpromeki wraps it
behind the generic MediaIO interface, so application code never
calls NDI directly — it just opens an NDI source / sink by URL
and the backend is selected at runtime when it is enabled at
build time.

The NDI SDK is distributed under a separate license from
libpromeki and cannot be vendored, so it must be installed
separately and pointed at via `PROMEKI_NDI_SDK_DIR`.

## 1. Download the SDK {#ndi_sdk}

The SDK is a free download from Vizrt but requires accepting the
NDI SDK license:

- <https://ndi.video/for-developers/ndi-sdk/>

There are two flavours, both of which libpromeki accepts:

- **NDI SDK** — the standard SDK; runtime library is
  `libndi.so`.
- **NDI Advanced SDK** — adds the advanced senders / receivers
  and additional codecs; runtime library is
  `libndi_advanced.so`.

Either ships the same headers under `include/`. The dynamic-load
shim (`Processing.NDI.DynamicLoad.h`) finds whichever runtime is
present, so the choice is purely a runtime / licensing decision —
the build sees one consistent header set either way.

Download the Linux installer (Windows / macOS installers work on
their respective platforms, same header layout), and extract /
install it anywhere you like. The result looks like:

```
ndisdk/
  include/
    Processing.NDI.Lib.h          ← main NDI API header
    Processing.NDI.DynamicLoad.h  ← runtime library shim
    Processing.NDI.Advanced.h     ← Advanced-SDK-only extensions
    ...
  lib/
    x86_64-linux-gnu/
      libndi_advanced.so          ← runtime library (Advanced SDK)
      libndi_advanced.so.6
      libndi_advanced.so.6.2.1
    aarch64-linux-gnu/
    ...
  bin/
  documentation/
```

Headers live flat under `include/` (no nested `Interface/`
directory like the NVIDIA Video Codec SDK). Per-architecture
runtimes live under `lib/<arch-triple>/`.

## 2. Point libpromeki at the SDK {#ndi_configure}

Pass the SDK directory at configure time:

```sh
cmake -B build -S . -DPROMEKI_NDI_SDK_DIR=/home/you/src/ndisdk
```

Or export it as an environment variable once:

```sh
export PROMEKI_NDI_SDK_DIR=/home/you/src/ndisdk
cmake -B build -S .
```

A `-DPROMEKI_NDI_SDK_DIR=...` on the command line always wins
over the environment variable. The legacy `NDI_SDK_DIR`
environment variable is also consulted as a find hint for
compatibility with existing setups.

The CMake probe looks for `include/Processing.NDI.Lib.h` below
that directory. If it finds the header, `PROMEKI_ENABLE_NDI`
defaults to `ON`; otherwise it defaults to `OFF` and the backend
is not built.

## 3. Runtime library {#ndi_runtime}

libpromeki uses the NDI dynamic-load shim
(`Processing.NDI.DynamicLoad.h`), so there is nothing to link
against at build time. At process startup the shim probes for
`libndi.so` (or `libndi_advanced.so`) on the loader path and
populates a function pointer table. If the library is not found,
the NDI backend reports an error at MediaIO open time but the
rest of libpromeki keeps working.

For development, the simplest setup is to add the SDK's `lib/`
directory to the loader path:

```sh
export LD_LIBRARY_PATH=/home/you/src/ndisdk/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH
```

For deployment, install the runtime system-wide (the SDK
installer can do this) or copy the `.so` into a directory listed
in `/etc/ld.so.conf.d/` and run `sudo ldconfig`.

Verify by running:

```sh
ldconfig -p | grep -E 'libndi(_advanced)?\.so'
```

## Verification {#ndi_verify}

A clean configure with everything set up correctly prints lines
like these during `cmake`:

```
-- Found NDI SDK: /home/you/src/ndisdk/include
--   NDI Advanced SDK headers detected
```

After building, check the compiled-in features via the build-info
string emitted by any libpromeki executable at startup:

```
Features: NETWORK PROAV ... NDI
```

## URL form {#ndi_urls}

The NDI MediaIO backend accepts two URL shapes for both source
and sink mode:

- `ndi://<host>/<name>` — `<name>` on `<host>`. For sink mode,
  `<host>` must be the local machine (NDI senders advertise from
  the local box); a non-local host is rejected at open with
  `Error::InvalidArgument`. For source mode, `<host>` filters the
  discovery match to that machine's canonical name.
- `ndi:///<name>` — `<name>` on this machine. Equivalent to
  `ndi://<this-host>/<name>`; the local hostname is filled in
  automatically.

The same URL is openable for either direction. Direction is
selected by the caller:

- `MediaIO::createForFileRead("ndi:///MyCamera")` opens a
  receiver on `<this-host> (MyCamera)`.
- `MediaIO::createForFileWrite("ndi:///MyOutput")` opens a
  sender named `MyOutput`.
- `MediaIO::createFromUrl("ndi:///MyCamera")` defaults to a
  receiver; pass `?OpenMode=Write` to open as a sender.

`NdiFactory::enumerate()` returns currently-advertised sources
in `ndi://<host>/<name>` form, so each enumerated URL round-trips
through `createForFileRead` to the same source.

## Troubleshooting {#ndi_troubleshoot}

### CMake cannot find the SDK {#ndi_trouble_sdk}

If `cmake` prints

```
-- NDI SDK not found; set PROMEKI_NDI_SDK_DIR
```

pass the SDK directory via `-DPROMEKI_NDI_SDK_DIR=...`. The
directory must contain `include/Processing.NDI.Lib.h`; the SDK
installer extracts to that layout automatically.

### libndi not found at runtime {#ndi_trouble_runtime}

The build succeeds but opening an NDI MediaIO returns an error
about the runtime library. Check:

- Is `libndi.so` (or `libndi_advanced.so`, for the Advanced SDK)
  on the loader path?
  (`ldconfig -p | grep -E 'libndi(_advanced)?\.so'`)
- If you only have the SDK extracted (no system install), is
  `LD_LIBRARY_PATH` pointing at the SDK's
  `lib/<arch-triple>/` directory?
- Is the architecture right? The SDK ships per-arch
  subdirectories under `lib/`; pick the one that matches your
  build target.
