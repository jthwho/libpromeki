# Color Space Conversion (CSC) Framework {#csc}

High-performance, configurable pixel format and color space
conversion with Highway SIMD acceleration.

## Overview {#csc_overview}

The CSC framework converts images between any pair of pixel
descriptions (`PixelFormat`), handling differences in memory layout,
component ordering, bit depth, chroma subsampling, color model, and
transfer function. It is the implementation behind `Image::convert()`.

The framework has three execution tiers:

1. **Fast paths** — Hand-tuned integer-domain kernels for common
   broadcast conversion pairs (e.g. RGBA8 ↔ YUV8 4:2:2 Rec.709).
   These bypass the generic pipeline entirely and deliver the highest
   throughput (3,000–10,000+ Mpix/s at 1080p).

2. **SIMD generic pipeline** — A multi-stage float-domain pipeline
   with Highway SIMD acceleration. Used for conversions without a
   registered fast path (200–1,300 Mpix/s).

3. **Scalar generic pipeline** — The same multi-stage pipeline with
   SIMD disabled. Selected via the `"Path"` configuration key.
   Useful for debugging and as a reference implementation (100–300
   Mpix/s).

## Pipeline Architecture {#csc_pipeline}

The generic pipeline processes one scanline at a time through an
ordered chain of stages. Stages that are not needed for a given
conversion are eliminated at compile time.

```
Source pixels
  → Unpack (memory layout → float SoA buffers)
  → Alpha fill (if target has alpha, source does not)
  → Range In (limited-range integers → 0.0–1.0)
  → YCbCr→RGB (if source is YCbCr: matrix on encoded values)
  → EOTF (remove source transfer function → linear)
  → Gamut matrix (source primaries → target primaries, linear domain)
  → OETF (apply target transfer function → encoded)
  → RGB→YCbCr (if target is YCbCr: matrix on encoded values)
  → Range Out (0.0–1.0 → target integer range)
  → Pack (float SoA buffers → target memory layout)
Target pixels
```

The YCbCr matrix operates on gamma-encoded RGB values, not linear.
This matches the ITU-R specifications (BT.709, BT.601, BT.2020)
where the luma/chroma separation is defined on the encoded signal.

## Fast-Path Kernels {#csc_fastpaths}

Fast paths are registered at static initialization via
`CSCRegistry::registerFastPath()` and discovered automatically by
`CSCPipeline` during compilation. Each fast path is a single function
that converts one scanline from source to target format, bypassing
all pipeline stages.

Registered fast paths (54 total):

| Standard | Bit Depth | Formats |
|----------|-----------|---------|
| BT.709   | 8-bit     | YUYV, UYVY, NV12, NV21, NV16, Planar 422/420 ↔ RGBA8 |
| BT.709   | 10-bit LE | UYVY, Planar 422/420, NV12, v210 ↔ RGBA10 LE |
| BT.709   | 8-bit     | v210 ↔ RGBA8 (inline 8↔10-bit step around v210 math) |
| BT.709   | 12-bit LE | UYVY, Planar 422/420, NV12 ↔ RGBA12 LE |
| BT.601   | 8-bit     | YUYV, UYVY, NV12, Planar 420 ↔ RGBA8 |
| BT.2020  | 10-bit LE | UYVY, Planar 420 ↔ RGBA10 LE |
| BT.2020  | 12-bit LE | UYVY, Planar 420 ↔ RGBA12 LE |
| Format   | 8-bit     | BGRA↔RGBA, RGBA↔RGB |

Fast paths use fixed-point integer arithmetic with standard
broadcast coefficients (e.g. BT.709: Y = 66R + 129G + 25B). They
do not apply separate sRGB and Rec.709 transfer functions — the
input sRGB signal is treated as Rec.709 directly. This is standard
broadcast practice and produces results within ±1 LSB of the ITU
integer reference for 8-bit.

## Accuracy Characteristics {#csc_accuracy}

**Scalar pipeline vs Color::convert()** — The scalar generic
pipeline uses the same float-domain EOTF, 3×3 matrix, and OETF
chain as `Color::convert()`, with transfer functions evaluated via
pre-computed LUTs. Maximum deviation: **±2 LSB** for 8-bit
conversions.

**Fast path vs scalar pipeline** — Fast paths use integer
BT.709/601/2020 arithmetic which intentionally approximates the
sRGB↔Rec.709 transfer function difference as negligible. Maximum
deviation from the scalar pipeline: **≤35 LSB** near black where
the sRGB and Rec.709 linear segments diverge. For mid-range and
highlight values, the deviation is typically ≤2 LSB.

**Fast path round-trip** — RGB → YCbCr → RGB via fast paths incurs
≤3 LSB error for achromatic and saturated colors (integer
quantization). Chroma subsampling (4:2:2, 4:2:0) introduces
additional spatial error at sharp color transitions.

**10-bit accuracy** — 10-bit fast paths use the same fixed-point
structure scaled to the 10-bit range (Y: 64–940, Cb/Cr: 64–960).
When the source is upconverted from 8-bit, the combined tolerance
is **±4 LSB**.

**12-bit accuracy** — 12-bit fast paths use fixed-point arithmetic
scaled to the 12-bit limited range (Y: 256–3760, Cb/Cr: 256–3840,
out of 0–4095). Round-trip (RGBA12 → YCbCr12 → RGBA12) error is
≤3 LSB for achromatic and saturated colors. Chroma subsampling
(4:2:2, 4:2:0) introduces additional spatial error at sharp color
transitions.

## Configuration {#csc_config}

`CSCPipeline` accepts a `MediaConfig` for runtime hints:

| Key                     | Type   | Default       | Description |
|-------------------------|--------|---------------|-------------|
| `MediaConfig::CscPath`  | String | `"optimized"` | `"optimized"` enables SIMD and fast paths. `"scalar"` forces the generic float pipeline with no SIMD. |

```cpp
// Force scalar path for debugging / reference comparison
MediaConfig cfg;
cfg.set(MediaConfig::CscPath, String("scalar"));
Image dst = src.convert(PixelFormat::YUV8_422_Rec709, metadata, cfg);
```

## Thread Safety {#csc_threading}

`CSCPipeline` is immutable after construction and safe to execute
concurrently from multiple threads. Each thread must provide its
own `CSCContext` for scratch storage. `Image::convert()` creates a
temporary context internally, so no manual context management is
needed for the high-level API.

**Pipeline caching** — Callers that repeatedly convert between the
same format pairs should use `CSCPipeline::cached()` instead of
constructing a new `CSCPipeline` each call. The cache is a
process-wide mutex-protected map keyed on (srcDesc, dstDesc, path).
`compile()` runs exactly once per unique triple; subsequent calls
return the same shared pipeline. The `Image::convert()` high-level
API uses the cache automatically.

## Adding Custom Fast Paths {#csc_extending}

Register a fast path at static initialization:

```cpp
static void myKernel(const void *const *srcPlanes,
                     const size_t *srcStrides,
                     void *const *dstPlanes,
                     const size_t *dstStrides,
                     size_t width, CSCContext &ctx) {
    // Convert one scanline...
}

static struct MyRegistrar {
    MyRegistrar() {
        CSCRegistry::registerFastPath(
            PixelFormat::MySourceFormat,
            PixelFormat::MyTargetFormat,
            myKernel);
    }
} __myRegistrar;
```

The pipeline will automatically use the registered kernel when
constructing a `CSCPipeline` for that source/target pair, unless
the `"Path"` config is set to `"scalar"`.

## Test Strategy {#csc_testing}

The CSC test suite validates conversions at multiple levels:

- **Level 1**: `Color::convert()` single-pixel validation (ground truth)
- **Level 2**: Scalar pipeline consistency with `Color::convert()` (±2 LSB)
- **Level 3**: `VideoTestPattern` through scalar pipeline vs Color reference
- **Level 4**: `VideoTestPattern` through fast path vs BT.709 integer reference (±1 LSB)
- **Level 5**: Round-trip accuracy (RGB → YCbCr → RGB)
- **Level 6**: Cross-format consistency (same source → multiple YCbCr layouts)
- **Level 7**: 10-bit consistency (8-bit upconversion → 10-bit YCbCr)
- **Level 8**: Range boundaries and edge cases
- **Level 9**: Rec.601 / Rec.2020 coefficient validation

**See also:** `CSCPipeline`, `CSCContext`, `CSCRegistry`,
`Image::convert()`; [Color Science](@ref color_science) for the underlying color
model architecture.
