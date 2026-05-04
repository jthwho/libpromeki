# AudioGen planar format support

**File:** `src/proav/audiogen.cpp:66`
**FIXME:** "Need to set to new plane for planar."

Currently increments `data++` per channel, which only works for
interleaved formats. Planar formats store each channel in a separate
memory plane.

- [ ] Detect planar vs interleaved from `AudioDesc`.
- [ ] For planar: advance to the next plane's base pointer per
  channel.
- [ ] For interleaved: keep current `data++` behaviour.
- [ ] Test with both planar and interleaved audio generation.
