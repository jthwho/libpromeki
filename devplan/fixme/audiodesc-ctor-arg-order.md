# AudioDesc(rate, channels) constructor is easy to misuse

**File:** `include/promeki/audiodesc.h:98`

**FIXME:** The convenience constructor
`AudioDesc(float sampleRate, unsigned int channels)` has a
parameter order that is easy to swap accidentally — a caller
writing `AudioDesc(2, 48000.0f)` (channels first, mistakenly)
compiles cleanly via the implicit `int → float` and
`float → unsigned int` conversions and produces an `AudioDesc`
with 48000 channels at 2 Hz.

## Tasks

- [ ] Replace direct construction with a named factory such as
  `AudioDesc::nativeFloat(sampleRate, channels)` so the call site
  reads unambiguously.
- [ ] Or introduce a builder (e.g.
  `AudioDesc::Builder().sampleRate(...).channels(...).build()`)
  to make argument intent explicit.
- [ ] Once a safer factory exists, mark the
  `AudioDesc(float, unsigned int)` ctor `explicit` (or remove it)
  and migrate call sites.
- [ ] Add a static_assert / unit test guarding the conversion
  trap (e.g. ensure swapped numeric literals fail to compile or
  produce an invalid descriptor).
