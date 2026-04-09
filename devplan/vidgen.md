# vidgen — DEPRECATED

**Status:** The `vidgen` utility and all its `MediaNode`-based pipeline nodes are **deprecated**. They remain in the tree only until the new `MediaPipeline` (see `proav_pipeline.md`) and the new MediaIO backends (see `proav_nodes.md`) can express the same pipeline, at which point `utils/vidgen/` and every `*Node` class it depends on will be deleted.

`vidgen` was originally built as the first real user of the old `MediaNode` pipeline framework. It works, but every capability it provides will be replaced by a config-driven `mediaplay` invocation running over the new MediaIO-based pipeline:

| Old vidgen feature | New equivalent |
|---|---|
| `TestPatternNode` | `MediaIOTask_TPG` (complete) |
| `JpegEncoderNode` | `MediaIOTask_Converter` with JPEG codec (planned) |
| `FrameDemuxNode` | Native fan-out in `MediaPipeline` |
| `TimecodeOverlayNode` | Built into `MediaIOTask_TPG` text-burn / future text-overlay converter |
| `RtpVideoSinkNode` | `MediaIOTask_RtpVideo` (planned, see `proav_nodes.md`) |
| `RtpAudioSinkNode` | `MediaIOTask_RtpAudio` (planned, see `proav_nodes.md`) |

Nothing new should be built on top of vidgen. Deferred items and FIXMEs from the vidgen era are either already handled (text burn-in on TPG, packet pacing on RtpSession, LTC encode via AudioTestPattern) or will land on the new MediaIO backends directly.

## Removal checklist

- [ ] `MediaIOTask_RtpVideo` / `MediaIOTask_RtpAudio` shipped
- [ ] `MediaIOTask_Converter` shipped with JPEG encode path
- [ ] `mediaplay` migrated to build its pipeline via `MediaPipeline::build()` (see `proav_nodes.md`)
- [ ] Confirm `mediaplay --input TPG --input-config=... --output RtpVideo --output-config=...` produces bit-identical RTP output to the current `vidgen`
- [ ] Delete `utils/vidgen/` and every `*Node` / `*node.h` under `include/promeki/` and `src/proav/`
- [ ] Delete the matching tests

This is the last outstanding job before the deprecated `MediaNode`/`MediaPipeline` files can be removed.
