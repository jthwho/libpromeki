# V4L2 Mem2Mem Codec Backend

**Phase:** 4B (ProAV / MediaIO backends)
**Library:** `promeki`
**Standards:** All work follows `CODING_STANDARDS.md`. Every class
requires complete doctest coverage. See `devplan/README.md` for the
full requirements.

Shipped (2026-06-01): `V4l2M2mCodec` engine, `V4l2VideoEncoder`,
`V4l2VideoDecoder`, `V4l2RawFormat`, `V4l2CodecParams`, caption-SEI
bitstream surgery, DMABUF zero-copy primitives in the engine. Targets
the Xilinx VCU (`allegro`/`al5d`), Raspberry Pi (`bcm2835-codec`), and
validated against the kernel `vicodec` test driver.

## What shipped

- `V4l2M2mCodec` — shared engine supporting single-planar
  (`V4L2_CAP_VIDEO_M2M`) and multiplanar (`V4L2_CAP_VIDEO_M2M_MPLANE`),
  auto-probed at `open`. Owns device fd, MMAP buffer pools, format
  negotiation, control programming (`setControl` / `setControlCompound`),
  and QBUF/DQBUF bookkeeping. DMABUF import/export primitives:
  `queueOutputDmabuf`, `exportBuffer`.
- `V4l2VideoEncoder` — VideoEncoder backend registered as `"V4L2"` for
  H264/HEVC, weight `BackendWeight::Vendored + 20`. Lazy open on first
  `submitFrame`. Inputs: NV12, NV16, P010 (via `V4l2RawFormat`). Wires
  profile/level, VUI colorimetry, HDR static metadata, caption SEI.
- `V4l2VideoDecoder` — VideoDecoder backend (`"V4L2"`, H264/HEVC). Staged
  bring-up: OUTPUT queue only until `V4L2_EVENT_SOURCE_CHANGE` then
  CAPTURE configured + streamed. NV12 (or driver-negotiated format) out
  via adaptive `V4l2RawFormat` lookup.
- `V4l2RawFormat` — table-driven `{V4L2 fourcc, PixelFormat, bytesPerSample,
  shift, chromaVDiv}`. Formats: NV12 (8b 4:2:0), NV16 (8b 4:2:2), P010
  (10b 4:2:0, MSB-aligned). Shared `v4l2PackSemiPlanar` /
  `v4l2UnpackSemiPlanar` helpers used by both backends.
- `V4l2CodecParams` — host-testable pure mappings: H.273 colorimetry to
  V4L2 fields, H.264/HEVC profile/level enum translations, mastering
  display and CLL info struct builders.
- `V4l2CaptionSei` — software SEI injection/extraction around the HW
  codec: `v4l2BuildSeiNal`, `v4l2InjectSeiNals`, `v4l2ExtractSeiPayloads`.
  Handles H.264 (NAL 0x06) and HEVC (prefix-SEI 0x4E 0x01), emulation
  prevention, placement before the first VCL NAL. Caption encode/decode
  wired into the encoder/decoder via `VideoEncoderSei` + `AncTranslator`.
- Unit tests: `tests/unit/v4l2m2mcodec.cpp` — 22 device-free policy cases
  plus an optional vicodec integration harness (encoder + decoder round-
  trip, colorimetry, dma-buf zero-copy); 327 assertions on a host with
  vicodec loaded.

## Open work

### DMABUF zero-copy backend wiring

- [ ] **Encoder OUTPUT import via DMABUF.** The engine `queueOutputDmabuf`
  is proven live on vicodec. Activate it from `V4l2VideoEncoder::submitFrame`
  when the input `Buffer` is `MemSpace::Dmabuf` — skip the `packSemiPlanar`
  memcpy. Requires a single-fd dma-buf frame abstraction: current NV12
  payloads have two separate plane Buffers (Y + CbCr) whereas a V4L2
  single-fd import puts both planes in one dma-buf at a per-plane offset.
  Fix options: (a) require the caller to allocate via `DmaHeap` into a
  single contiguous `Buffer` with stride/offset metadata, (b) add a
  `BufferView`-based pack step using `copyTo`. Option (a) pairs cleanly
  with the CSC stage producing a dma-heap-backed NV12 frame.

- [ ] **Decoder CAPTURE export via VIDIOC_EXPBUF.** Export each CAPTURE
  vb2 buffer as a dma-buf once at pool setup (same pattern as
  `V4l2MediaIO`). Hand the decoded frame downstream as `MemSpace::Dmabuf`
  with a release callback re-queuing the vb2 buffer to the driver. This
  avoids copying decoded frames into host memory for a GPU- or
  display-bound downstream.

- [ ] **Release-gated requeue pool** (decoder side). A decoded dma-buf
  frame must not be re-queued until every downstream consumer has
  released it. The `BufferImpl::ReleaseCallback` + `V4l2RequeueGate`
  pattern from `V4l2MediaIO` applies directly. See
  [`project_dmabuf_fd_ownership`] for the fd-ownership contract.

### Xilinx VCU (allegro / al5d) bring-up

- [ ] First real-hardware encoder test on the VCU (MMAP path). The
  engine already handles multiplanar; confirm the allegro driver's
  quirks (supported profile/level set, min/max bitrate, header mode
  default) and add best-effort fallbacks.
- [ ] VCU-specific packed 10-bit formats **XV15** (`NV12_10LE32`) and
  **XV20** (`NV16_10LE32`): 10:10:10:2 packed in 32-bit words, not
  in mainline `videodev2.h`. These need a vendor-fourcc path in
  `V4l2RawFormat` and a pack/unpack implementation.
- [ ] Test HDR control effect end-to-end (VCU writes VUI/SEI;
  verified soft-round-trip only via vicodec which lacks HDR controls).

### Raspberry Pi (bcm2835-codec) bring-up

- [ ] Confirm multiplanar mode, NV12, H.264 encoder profile/level on Pi 4.
  The engine is tested against vicodec single-planar; the Pi is
  multiplanar so test the `_mplane` branch on real hardware.
- [ ] Pi 5 note: no H.264 hardware *encoder* (only stateless HEVC decode
  via rpivid). Stateless decode is a separate, larger Request-API effort
  and is deferred.

### Decoder: mid-stream dynamic resolution change

- [ ] Only the initial `V4L2_EVENT_SOURCE_CHANGE` (codec header parse)
  triggers `setupCapture`. A mid-stream resolution change (e.g. adaptive
  streaming) requires tearing down and re-negotiating the CAPTURE queue
  mid-decode. This is a known V4L2 stateful-decoder requirement; deferred
  until there's a concrete use case.

### HEVC tier

- [ ] No `MediaConfig` tier key yet; HEVC tier left at driver default.
  Add `VideoTier` MediaConfig key and map to
  `V4L2_CID_MPEG_VIDEO_HEVC_TIER` when a user tier preference exists.

## Testing reality

`vicodec` (FWHT) validates the V4L2 API / queue / flush state machine
but NOT H.264 bitstreams or dma-contiguous DMABUF paths — those need
real Pi4 / VCU hardware. To run the optional integration harness:

```sh
sudo modprobe vicodec
./build/bin/unittest-promeki -tc='*Vicodec*'
```

The host-safe policy cases run unconditionally as part of
`build check`.
