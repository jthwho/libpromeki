# Inspector — Frame validation and monitoring {#inspector}

QA-oriented user guide for `InspectorMediaIO` — what
each check does, how to consume the results, what the log
output means, and how to interpret the A/V sync values a
real production stream will produce.

The Inspector is the inverse of the `TpgMediaIO` Test
Pattern Generator: where the TPG *produces* synthetic frames
with embedded validation signals, the Inspector *consumes* frames
and runs a configurable set of checks on each one. It is a
MediaIO **sink** — frames go in, no frames come out — designed to
sit at the terminal end of a pipeline and tell you whether the
stream still looks the way it should.

## Quick start {#inspector_quickstart}

The fastest way to verify a TPG stream is to wire a default
inspector to it. Both sides default to "full checks", so this is
literally a two-factory-call setup with a frame loop:

```cpp
#include <promeki/mediaio.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediaioportconnection.h>

MediaIO *src  = MediaIO::create(MediaIOFactory::defaultConfig("TPG"));
MediaIO *sink = MediaIO::create(MediaIOFactory::defaultConfig("Inspector"));

(void)src->open().wait();
(void)sink->open().wait();

auto *conn = new MediaIOPortConnection(src->source(0), sink->sink(0));
conn->start();

// ... let the pipeline run; consume signals or sleep for a bit ...

(void)src->close().wait();
(void)sink->close().wait();
delete conn;
delete src;
delete sink;
```

That's enough to get every check running:

- The TPG's default audio mode is `AudioPattern::PcmMarker` on
  every channel, which stamps a Manchester-encoded 76-bit codeword
  carrying `[stream:8][channel:8][frame:48]` into each chunk via
  `AudioDataEncoder`.
- The TPG's image data encoder pass is on by default and stamps two
  64-bit payloads (frame ID + BCD timecode) into the top of every
  frame.  The frame ID uses the same `[stream:8][channel:8][frame:48]`
  layout as the audio marker (with `channel = 0`), so the inspector
  can correlate audio and video by the shared frame number.
- The Inspector's default `MediaConfig::InspectorTests` list
  carries every default-on test (`ImageData`, `AudioData`,
  `AvSync`, `Continuity`, `Timestamp`, `AudioSamples`), so the
  full suite runs out of the box.  `Ltc` and `CaptureStats` are
  off by default and have to be opted in.

The default periodic log writes a multi-line report once per second
of wall time, plus immediate warnings whenever a discontinuity is
detected. Sample output (1080p RGBA8 source, 30 fps, 48 kHz audio):

```
config: image data decode  = enabled
config: audio data decode  = enabled
config: A/V sync check     = enabled (auto-enables image data + audio data decode)
config: A/V sync jitter tolerance = 0 samples (any frame-to-frame change beyond this fires a discontinuity warning)
config: continuity check    = enabled (auto-enables image data decode)
config: image data band    = 16 scan lines per item, TPG-convention 2 items at top of frame
config: drop frames         = yes (sink behaviour)
config: periodic log every  = 1.00 seconds (wall time)
Frame 30: report after 30 frames (1.00 s wall) — 30 total since open
Frame 30: picture data band: decoded 30 / 30 frames (100.0%) — most recent: streamID 0x00000000, frameNo 29, TC 01:00:00:29
Frame 30: audio data: decoded 30 / 30 frames on every channel — most recent: streamID 0x00, frame 29
Frame 30: A/V Sync: audio and video locked (0 samples)
```

The continuity line is intentionally absent on a clean stream — the
inspector stays silent when there is nothing to flag. See
[Annotated log reference](#inspector_log_reference) for a full
annotation of every line.

## The inspector tests {#inspector_checks}

The inspector runs an independently-selectable set of tests per
frame. The selection is driven by a single config key,
`MediaConfig::InspectorTests` — an `EnumList` of
`InspectorTest` values. The default list carries every test
so the full suite runs out of the box; narrow the list to run a
subset. The inspector resolves test dependencies at open time, so
you never have to list the upstream decoders explicitly.

| Test                           | Enum value                       | Auto-enables                |
|--------------------------------|----------------------------------|-----------------------------|
| Decode picture data band       | `InspectorTest::ImageData`       | (no deps)                   |
| Decode audio data marker       | `InspectorTest::AudioData`       | (no deps)                   |
| Decode audio LTC               | `InspectorTest::Ltc`             | (no deps)                   |
| A/V Sync (sample offset)       | `InspectorTest::AvSync`          | `ImageData` + `AudioData`   |
| Continuity (TC / frame# / SID) | `InspectorTest::Continuity`      | `ImageData`                 |
| Timestamp delta + actual FPS   | `InspectorTest::Timestamp`       | (no deps)                   |
| Per-frame audio sample count   | `InspectorTest::AudioSamples`    | (no deps)                   |

Example — only run the timestamp and A/V sync checks:

```cpp
EnumList tests = EnumList::forType<InspectorTest>();
tests.append(InspectorTest::Timestamp);
tests.append(InspectorTest::AvSync);
cfg.set(MediaConfig::InspectorTests, tests);
// Or from the command line / string form:
//   InspectorTests=Timestamp,AvSync
```

### Picture data band decode {#inspector_check_picture}

Pulls the two 64-bit payloads written by `ImageDataEncoder` out
of the top of every frame:

- **Band 1** (lines `0..N-1`): the **frame ID** word, encoded as
  `(streamID << 32) | frameNumber`. StreamID is the upstream
  producer's identifier (set via `MediaConfig::StreamID` on the
  TPG); frameNumber is the producer's monotonic frame counter
  from when the source was opened.
- **Band 2** (lines `N..2N-1`): the **BCD timecode** word, packed
  in the `TimecodePackFormat::Vitc` format that
  `Timecode::toBcd64` produces.

`N` is set by `MediaConfig::InspectorImageDataRepeatLines` and
must match the producer's `MediaConfig::TpgDataEncoderRepeatLines`.
The default is 16 — wide enough that the inspector can use
multi-line averaging for SNR but cheap enough that the visible
data band is unobtrusive.

The decode is **all-or-nothing**: if either band fails to decode
(sync nibble corruption, CRC mismatch, missing image, decoder not
yet initialised, etc.) the inspector reports
`InspectorEvent::pictureDecoded` as `false` and leaves all
picture-side fields at their default values. This guarantees a
callback consumer can never confuse a stale or partially-decoded
reading with a real "frame 0, stream 0, TC 00:00:00:00" frame —
always check `pictureDecoded` before reading the other fields.

### Audio LTC decode {#inspector_check_ltc}

Pulls one channel of the frame's audio (selected by
`MediaConfig::InspectorLtcChannel`, default channel 0) and
feeds it through `LtcDecoder`. The decoder is format-agnostic:
any `AudioFormat` is accepted (PCMI_S8, PCMI_S16LE,
PCMI_Float32LE, ...). The most recently recovered timecode is
reported in `InspectorEvent::ltcTimecode` along with the
within-chunk sample offset of the LTC sync word in
`InspectorEvent::ltcSampleStart`.

The LTC decoder is stateful and may need a few frames to lock onto
a fresh stream — early frames will report
`InspectorEvent::ltcDecoded` as `false` until the decoder has
accumulated enough biphase-mark transitions.

### A/V Sync {#inspector_check_avsync}

The headline check. When both `ImageData` and `AudioData` are
running, the inspector computes the **instantaneous offset**
between the picture's video MediaTimeStamp and the audio chunk
that carries the matching frame number's codeword, in audio
samples. The value lives in
`InspectorEvent::avSyncOffsetSamples`.

**What this measures, exactly**

Both `ImageDataEncoder` and `AudioDataEncoder` stamp the same
`[stream:8][channel:8][frame:48]` codeword on every frame, so the
48-bit frame number uniquely identifies a frame across the two
streams. The inspector computes a raw phase from the codeword's
actual stream-sample position vs the rational-rate-predicted
ideal, then reports the deviation from a baseline phase it
latches on the first successful match:

```
raw_phase = streamSampleStart -
            FrameRate::cumulativeTicks(sampleRate, frameNumber)
baseline  = raw_phase at first match (latched once)
offset    = raw_phase - baseline             (in audio samples)
```

`streamSampleStart` is the codeword's leading-edge position in
the absolute audio-sample coordinate system, decoupled from how
the audio was chunked into per-frame payloads.
`cumulativeTicks(sampleRate, frame)` is the exact integer-truncated
audio sample position predicted by the upstream frame rate — for
NTSC 29.97 at 48 kHz this is the same number the TPG used to size
its audio chunks (1601/1602/1601/1602/1602...), so a clean
cadenced run has the codeword landing at exactly the predicted
sample and the raw phase reads 0.

The baseline absorbs any constant phase the producer happened to
start with — a mid-stream join (non-zero starting frame number),
an SRC's constant group delay, network-buffering offsets, or any
other one-time difference between the producer's audio cadence
and the rational ideal. None of those are real A/V sync errors,
so they shouldn't drive the reported value off 0; the baseline
makes the offset robust to them. Real changes in the codeword
position (codeword moved within a chunk, audio sample
dropped/inserted, audio path resampling drift) still show up
because they move the raw phase away from the latched baseline.

**Sign convention**

The inspector renders the offset in plain language so the sign
convention isn't something a QA reader has to remember:

```
Frame N: A/V Sync: Video leads audio by 2 samples, 0.0013 frames
Frame N: A/V Sync: Audio leads video by 5 samples, 0.0031 frames
Frame N: A/V Sync: audio and video locked (0 samples)
```

Internally, positive `avSyncOffsetSamples` means the audio
codeword landed at a later stream sample than predicted —
i.e. the audio is delayed relative to video, so **video leads
audio**. Negative = audio leads video.

**What "in sync" actually looks like**

In professional video the audio and video are locked to a common
reference, so a healthy stream's offset is **0** regardless of
frame rate — the cadence-aware formulation removes the
fractional-rate wobble that a simple wall-clock subtraction
would produce. Any frame-to-frame change is a real audio-side
fault: the codeword moved within its chunk, an audio sample was
dropped or inserted, or the audio sample-rate is off. The
inspector enforces that contract via the change-detection check
(see [Continuity tracking](#inspector_check_continuity) below)
with a configurable tolerance via
`MediaConfig::InspectorSyncOffsetToleranceSamples`. The default
tolerance is **0** — strict bit-exact lock — because the cadence
is already mathematically subtracted out. Pipelines with
known sub-sample jitter (e.g. an SRC re-clocking the audio) can
raise it.

### Continuity tracking {#inspector_check_continuity}

Compares this frame's picture-side metadata against the previous
frame's and emits an `InspectorDiscontinuity` for every property
that changed in an unexpected way. Tracked properties:

| Property              | Discontinuity kind                                     | What "unexpected" means                                                                       |
|-----------------------|--------------------------------------------------------|-----------------------------------------------------------------------------------------------|
| Frame number          | `InspectorDiscontinuity::FrameNumberJump`              | Did not advance by exactly 1                                                                  |
| Stream ID             | `InspectorDiscontinuity::StreamIdChange`               | Changed at all                                                                                |
| Picture timecode      | `InspectorDiscontinuity::PictureTcJump`                | Did not advance by exactly 1 (uses an inferred mode if known, otherwise the check skips)      |
| LTC timecode          | `InspectorDiscontinuity::LtcTcJump`                    | Did not advance by exactly 1                                                                  |
| Picture decoded       | `InspectorDiscontinuity::ImageDataDecodeFailure`       | Failed to decode after a previously successful frame                                          |
| LTC decoded           | `InspectorDiscontinuity::LtcDecodeFailure`             | Failed to decode after a previously successful frame                                          |
| A/V sync offset       | `InspectorDiscontinuity::SyncOffsetChange`             | Moved more than `MediaConfig::InspectorSyncOffsetToleranceSamples`                            |

Each discontinuity carries a pre-rendered description with the
**previous and current values inline** so a QA reader can see the
whole story in one log line:

```
Frame 4: discontinuity: Frame number jumped: was 3 (expected 4 next), got 5 (+1 frame relative to expected)
Frame 47: discontinuity: A/V sync offset moved: was +2 samples, now +1 samples (delta -1, tolerance 0) — audio and video are no longer locked
```

Discontinuities also accumulate into
`InspectorSnapshot::totalDiscontinuities` so a polled consumer
can see how many have occurred over the inspector's lifetime
without having to subscribe to the per-frame stream.

## Consuming the results {#inspector_results}

The inspector exposes its measurements in three complementary
ways. Pick whichever fits your consumer pattern:

### Per-frame callback {#inspector_callback}

Set via `InspectorMediaIO::setEventCallback` before the
inspector is opened. The callback receives a fully-populated
`InspectorEvent` once per frame and is invoked from the
MediaIO worker thread, so it must be thread-safe. This is the
lowest-latency path — useful for real-time UIs or telemetry
shipping.

Because `MediaIO::create` returns a `MediaIO*` and hides the
underlying task, callers that want to set a callback must
construct the task themselves and adopt it. See
[Construction patterns](#inspector_construct).

### Accumulator snapshot {#inspector_snapshot}

`InspectorMediaIO::snapshot` returns a thread-safe value
copy of the running totals plus the most recent
`InspectorEvent`. Useful for polled consumers — the test
suite, a status bar, anything that wants "how is the stream
doing right now" without subscribing to every frame.

```cpp
InspectorSnapshot snap = inspector->snapshot();
if(snap.totalDiscontinuities > 0) { failed_check_count++; }
if(snap.lastEvent.avSyncValid) { recordAvSync(snap.lastEvent.avSyncOffsetSamples); }
```

### Periodic log {#inspector_log}

The default delivery channel. At the cadence configured by
`MediaConfig::InspectorLogIntervalSec` (default 1.0 second of
wall time), the inspector emits a multi-line summary via
`promekiInfo` plus immediate warnings via `promekiWarn` for any
discontinuities detected. Set the interval to `0` to disable
the periodic summary entirely (immediate warnings still fire).

Every line in a single periodic report shares the same
`"Frame N:"` prefix where `N` is the inspector's monotonic
frame counter at report time, so a log reader (or `grep`) can
group the lines for a single report by string match.
Discontinuity warnings emitted between periodic reports use the
frame index of the frame on which the discontinuity was detected.

Lines for **disabled** checks are silently elided so the log
doesn't carry stale "n/a" placeholders. The continuity summary
line is only emitted when the running total is non-zero — a clean
stream produces a four-line periodic report (header + picture +
audio + A/V sync), and only grows the continuity summary line
when something needs attention.

## Construction patterns {#inspector_construct}

Two paths, depending on whether you need the per-frame callback.

### Standard factory (no callback) {#inspector_construct_factory}

```cpp
MediaIO *io = MediaIO::create(MediaIOFactory::defaultConfig("Inspector"));
(void)io->open().wait();
(void)io->sink(0)->writeFrame(frame).wait();
// ... query io->stats() or rely on the periodic log
(void)io->close().wait();
delete io;
```

The factory route is the right choice when you only care about
the periodic log and the `InspectorMediaIO::snapshot` accessor.
`MediaIO *` is an opaque handle from the factory's perspective —
there's no typed pointer to set callbacks on.

### Direct-construction path (per-frame callback) {#inspector_construct_direct}

```cpp
auto *insp = new InspectorMediaIO();
insp->setConfig(MediaIOFactory::defaultConfig("Inspector"));
insp->setEventCallback([](const InspectorEvent &e) {
    // Runs on the worker thread — be thread-safe.
    if (e.avSyncValid && std::abs(e.avSyncOffsetSamples) > 100) {
        std::printf("WARNING: large sync offset: %lld samples\n",
                    (long long)e.avSyncOffsetSamples);
    }
});
(void)insp->open().wait();

(void)insp->sink(0)->writeFrame(frame).wait();
// ... insp keeps publishing snapshot() / events as long as it's open.

(void)insp->close().wait();
delete insp;
```

The callback **must** be installed before `open()` — calling
`setEventCallback` on a running inspector is a data race. This
is why the factory path can't expose the callback: the factory
returns a `MediaIO *` (the user-surface type), which doesn't
expose `InspectorMediaIO`-specific accessors.

## What to look for in CI / QA {#inspector_what_to_look_for}

The inspector's design assumption is that **a clean run is silent
past the configuration block**. An automated CI job that wires up
a TPG → Inspector pair and pumps frames for some duration only has
to grep its log for a few patterns:

| Pattern                                         | Meaning                                                          |
|-------------------------------------------------|------------------------------------------------------------------|
| `discontinuity:`                                | Something was unexpected. Always a warning.                      |
| `NOT DECODED`                                   | The decoder lost lock mid-stream. Always a warning.              |
| `audio and video are no longer locked`          | The A/V sync offset moved. Specific `SyncOffsetChange` wording.  |
| `Frame number jumped`                           | The producer dropped or repeated a frame.                        |
| `Stream ID changed`                             | The producer was swapped mid-stream.                             |

Use the periodic report's "X / Y frames (Z%)" decode percentages
to drive a "did the inspector see anything at all?" sanity check —
a 0% decode rate over a long run usually means the upstream
producer isn't actually emitting the data band the inspector is
looking for, or the band geometry doesn't match.

The default sync-offset jitter tolerance is **0** — every
sample-of-change is reported. In production this is the right
default because in pro video audio and video are locked to the
same reference and any drift is real. Pipelines with bounded
jitter (e.g. resampler-induced phase wander) can raise the
tolerance via `MediaConfig::InspectorSyncOffsetToleranceSamples`.

## Annotated log reference {#inspector_log_reference}

Every line the inspector writes, in the order you'll see them.

### At open time — configuration block {#inspector_log_config}

```
config: image data decode  = enabled
config: audio data decode  = enabled
config: LTC decode         = disabled
config: A/V sync check     = enabled (auto-enables image data + audio data decode)
config: A/V sync jitter tolerance = 0 samples (any frame-to-frame change beyond this fires a discontinuity warning)
config: continuity check    = enabled (auto-enables image data decode)
config: image data band    = 16 scan lines per item, TPG-convention 2 items at top of frame
config: drop frames         = yes (sink behaviour)
config: periodic log every  = 1.00 seconds (wall time)
```

Captured at `open()` so a post-mortem reader can interpret any
later log lines in the right context. Lines for disabled checks
still appear (so a forensics reader can see what wasn't enabled).

### Periodic report (one block per interval) {#inspector_log_periodic}

```
Frame 30: report after 30 frames (1.00 s wall) — 30 total since open
Frame 30: picture data band: decoded 30 / 30 frames (100.0%) — most recent: streamID 0xc0ffeeaa, frameNo 29, TC 01:00:00:29
Frame 30: audio data: decoded 30 / 30 frames on every channel — most recent: streamID 0xaa, frame 29
Frame 30: A/V Sync: audio and video locked (0 samples)
```

- **Header line**: how many frames were processed since the last
  report, the wall-clock window, and the running total since the
  inspector opened.
- **picture data band**: cumulative decode rate (X of Y frames),
  the most recently recovered stream ID, frame number, and
  timecode. When the most recent frame failed to decode, the
  line becomes a warning instead of an info line.
- **audio data**: cumulative decode rate plus the most recent
  recovered stream ID and 48-bit frame number from any channel
  carrying a `PcmMarker` codeword.
- **A/V Sync**: the per-frame offset rendered in plain language
  plus both samples and fractional frames. Only emitted when the
  sync check is enabled and a marker-based measurement was
  possible on the most recent frame (i.e. a video frame whose
  frame number was matched by an audio codeword).

Lines that aren't relevant (decoders disabled, etc.) are elided.

### Discontinuity warnings (immediate) {#inspector_log_warnings}

```
Frame 4: discontinuity: Frame number jumped: was 3 (expected 4 next), got 5 (+1 frame relative to expected)
Frame 47: discontinuity: A/V sync offset moved: was +2 samples, now +1 samples (delta -1, tolerance 0) — audio and video are no longer locked
Frame 91: discontinuity: Stream ID changed: was 0x12345678, now 0xabcdef00
```

Fired the moment a discontinuity is detected, with the same
`"Frame N:"` prefix as the periodic report so a log reader can
tie them together. Always at warning level so log scrapers can
pull them out separately from the routine info-level traffic.

## Known limits {#inspector_known_limits}

- **Picture TC continuity needs a frame rate.** The picture data
  band's wire format only carries digits + the DF flag, so the
  recovered TC has no native `Timecode::Mode`. The inspector
  latches the first `Timecode::Mode` it sees from any source
  (currently the LTC decoder) and attaches it to picture TCs from
  then on. When LTC is disabled, the picture TC continuity check
  is silently skipped — the frame number and stream ID continuity
  checks still run because they're just integer comparisons.

- **The LTC decoder needs a few frames to lock.** Early frames
  in a fresh stream report `ltcDecoded` as `false` until the
  biphase mark state machine has accumulated enough transitions.
  The inspector reports the cumulative percentage in the
  periodic log so you can see the decoder catch up.

- **The image data decoder uses the slow CSC path.** The first
  implementation runs every band-decode through
  `Image::convert(RGBA8_sRGB)`, which works for every `PixelFormat`
  but isn't free. Hand-rolled fast paths per-format are a future
  enhancement; until then the inspector is fine for QA / monitoring
  use but not for fully-loaded production paths.

- **One global continuity history.** The inspector tracks a
  single previous-frame snapshot for continuity, not per-stream.
  A stream-ID change is treated as its own discontinuity kind
  rather than as "switch tracking history".

## See also {#inspector_see_also}

- `InspectorMediaIO` — the C++ API.
- `InspectorEvent` / `InspectorSnapshot` /
  `InspectorDiscontinuity` — the result types.
- `TpgMediaIO` — the producer side designed to pair
  with the inspector for end-to-end QA.
- [Image Data Encoder Wire Format](@ref imagedataencoder) — the wire
  format spec for the picture-side data band.
- `Timecode::toBcd64` / `Timecode::fromBcd64` — the
  timecode encoding the picture data band carries.
- `LtcDecoder` — the format-agnostic LTC decoder the
  inspector uses for the audio side.
