# JPEG XS: RFC 9134 slice-mode packetization

**Files:** `src/network/rtppayload.cpp` (`RtpPayloadJpegXs`),
`src/proav/mediaiotask_rtp.cpp`,
`include/promeki/mediaiotask_rtp.h`

**FIXME:** `RtpPayloadJpegXs` implements codestream mode (K=0):
each `pack()` call fragments one complete codestream across
MTU-sized packets with the 4-byte RFC 9134 header. Slice mode (K=1)
— where the encoder emits one slice per packet and each slice fits
in a single RTP packet — is not yet implemented. Slice mode is
what ST 2110-22 mandates for constant-bitrate contribution links.

Slice mode requires:

- **Encoder integration**: SVT-JPEG-XS
  `slice_packetization_mode = 1` makes each
  `svt_jpeg_xs_encoder_get_packet()` return one slice-sized
  bitstream buffer. Needs a new
  `MediaConfig::JpegXsSlicePacketization` key plumbed into the
  encoder API.
- **Pack path**: instead of fragmenting a complete codestream, each
  encoder output buffer maps 1:1 to one RTP packet with `K=1` in
  the header. The `SEP` counter increments per slice; the `P` (last
  packet in frame) flag comes from `last_packet_in_frame` on the
  encoder output.
- **Unpack path**: reassembly by RTP timestamp is the same, but
  verification should check the per-frame packet counter for gaps /
  reordering.
- **SDP**: the `a=fmtp` line must carry `packetization-mode=1` so
  receivers know to expect slice boundaries at packet boundaries.

## Tasks

- [ ] Add `MediaConfig::JpegXsSlicePacketization` key; plumb into
  `JpegXsVideoEncoder` encoder init.
- [ ] Extend `RtpPayloadJpegXs::pack()` with a K=1 branch that maps
  encoder slices 1:1 to RTP packets.
- [ ] Update `RtpPayloadJpegXs::unpack()` to verify per-frame packet
  counter for gap detection.
- [ ] Update `buildJpegXsFmtp()` to emit `packetization-mode=1`
  when slice mode is active.
- [ ] Loopback test mirroring the existing codestream-mode coverage.
