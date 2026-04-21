# NVIDIA NVENC setup {#nvenc}

How to install CUDA, the NVIDIA Video Codec SDK, and the
NVENC runtime so libpromeki's `NvencVideoEncoder` builds and
runs on your machine.

NVENC is a hardware video encoder built into every NVIDIA GPU from
Kepler onward. libpromeki wraps it behind the generic `VideoEncoder`
interface, so application code never calls NVENC directly — it just
asks for an encoder named `"h264"` or `"hevc"` and receives an
`NvencVideoEncoder` when the backend is enabled at build time.
Getting to that point needs three distinct pieces of NVIDIA software:

1. The **NVIDIA display driver** (provides `libnvidia-encode.so`
   at runtime — this is the actual user-mode NVENC library).
2. The **CUDA toolkit** (provides CUDA runtime + driver API
   headers that libpromeki's `cuda` and `NvencVideoEncoder`
   build against).
3. The **NVIDIA Video Codec SDK** (provides `nvEncodeAPI.h` —
   the NVENC API header, which is distributed separately from
   CUDA and the driver).

The sections below walk through each piece on a Debian / Ubuntu
system. The same packages exist under slightly different names on
Fedora / Arch / openSUSE; the structure of the install is the
same.

## 1. Driver and NVENC runtime {#nvenc_driver}

If you already run an NVIDIA GPU with the proprietary driver, the
driver itself is installed. The NVENC runtime is a separate
package that must match the driver version — install it with:

```sh
# Replace 580 with your driver's major version.  Check with:
#   nvidia-smi | head -5
sudo apt install libnvidia-encode-580
```

This drops `libnvidia-encode.so.1` into
`/usr/lib/x86_64-linux-gnu/`. libpromeki loads it at runtime via
`dlopen`; there is nothing to link against at build time.

Verify by running:

```sh
ldconfig -p | grep libnvidia-encode
```

If nothing is printed, either the package is not installed or its
path is missing from `ld.so.conf.d/`. Run `sudo ldconfig` to
refresh the cache.

## 2. CUDA toolkit {#nvenc_cuda}

libpromeki needs the CUDA runtime (`libcudart`, `cuda_runtime.h`)
and the CUDA driver API header (`cuda.h`). On Ubuntu:

```sh
sudo apt install nvidia-cuda-toolkit nvidia-cuda-dev
```

This installs `nvcc` at `/usr/bin/nvcc` and the CUDA headers at
`/usr/include/cuda_runtime.h` / `/usr/include/cuda.h`.

libpromeki's CMake configuration calls `find_package(CUDAToolkit)`
to locate these files automatically. When the call succeeds the
build defaults `PROMEKI_ENABLE_CUDA` to `ON`; otherwise it
defaults to `OFF`. Explicit
`-DPROMEKI_ENABLE_CUDA=ON|OFF` overrides the probe.

If `find_package` fails even though the toolkit is installed, pass
`CUDAToolkit_ROOT` to CMake:

```sh
cmake -B build -S . -DCUDAToolkit_ROOT=/usr/local/cuda
```

## 3. NVIDIA Video Codec SDK {#nvenc_sdk}

The SDK is a free download from NVIDIA but requires a developer
account:

- <https://developer.nvidia.com/nvidia-video-codec-sdk/download>

Download the Linux zip (Windows zip works on Windows, same
contents), and extract it anywhere you like. The result looks
like:

```
Video_Codec_SDK_13.0.37/
  Interface/
    nvEncodeAPI.h     ← the NVENC API header libpromeki needs
    nvcuvid.h         ← (NVDEC, not used yet)
    cuviddec.h
  Lib/linux/stubs/x86_64/
    libnvidia-encode.so ← link stub (we do not use this; runtime
                          library is loaded via dlopen)
  Samples/            ← reference applications, useful for
                        cross-checking behaviour
  Read_Me.pdf
```

Point libpromeki at the SDK at configure time:

```sh
cmake -B build -S . \
    -DPROMEKI_NVENC_SDK_DIR=/home/you/src/Video_Codec_SDK_13.0.37
```

Or export it as an environment variable once:

```sh
export PROMEKI_NVENC_SDK_DIR=/home/you/src/Video_Codec_SDK_13.0.37
cmake -B build -S .
```

A `-DPROMEKI_NVENC_SDK_DIR=...` on the command line always wins
over the environment variable. The legacy `NVENC_SDK_DIR`
environment variable is also consulted as a find hint for
compatibility with existing setups.

The CMake probe looks for `Interface/nvEncodeAPI.h` below that
directory. If it finds the header,
`PROMEKI_ENABLE_NVENC` defaults to `ON`; otherwise it defaults
to `OFF` and the backend is not built.

Supported SDK versions: 12.0 and newer. Older SDKs use a different
preset / tuning-info model that libpromeki does not map to.

## Verification {#nvenc_verify}

A clean configure with everything set up correctly prints lines
like these during `cmake`:

```
-- Found CUDAToolkit: /usr/include (found version "12.4.131")
-- Found NVENC SDK: /home/you/src/Video_Codec_SDK_13.0.37/Interface
```

After building, check the compiled-in features via the build-info
string emitted by any libpromeki executable at startup:

```
Features: NETWORK PROAV ... CUDA NVENC
```

Finally, run the NVENC unit tests:

```sh
./build/bin/unittest-promeki -tc='Nvenc*'
```

These tests auto-skip when no GPU is present, but on a machine
with a working driver + SDK + runtime they encode a handful of
synthetic frames through both H.264 and HEVC and verify the
resulting bitstream.

## Troubleshooting {#nvenc_troubleshoot}

### libnvidia-encode not found at runtime {#nvenc_trouble_runtime}

The build succeeds but `NvencVideoEncoder::createEncoder` returns
an encoder whose first `submitFrame` call reports
`Error::LibraryFailure`. Check:

- Is `libnvidia-encode-NNN` installed? (`apt list --installed |
  grep nvidia-encode`)
- Does its major version match the driver? (`nvidia-smi` reports
  the driver version; the package suffix must match.)
- Is `libnvidia-encode.so.1` on the loader path?
  (`ldconfig -p | grep libnvidia-encode`)

### CMake cannot find the SDK {#nvenc_trouble_sdk}

If `cmake` prints

```
-- NVENC SDK not found; set PROMEKI_NVENC_SDK_DIR
```

pass the SDK directory via `-DPROMEKI_NVENC_SDK_DIR=...` The
directory must contain `Interface/nvEncodeAPI.h`; the SDK zip
expands to that layout automatically.

### Driver / toolkit version skew {#nvenc_trouble_driver_mismatch}

NVENC is forward-compatible: an SDK built against API version N
runs on drivers supporting N or newer. If you installed the
toolkit from apt but the Video Codec SDK is a newer major release
than your driver supports, NVENC initialisation will fail with
`NV_ENC_ERR_INVALID_VERSION`. Either upgrade the driver or use
an older SDK release whose minimum driver version your driver
satisfies (see the SDK's `Read_Me.pdf` for the compatibility
matrix).
