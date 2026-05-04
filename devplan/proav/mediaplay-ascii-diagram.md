# mediaplay: ASCII pipeline diagram

It'd be useful for `mediaplay --plan` / `--describe` (and the
periodic `--stats` header) to render the pipeline as a box-and-arrow
ASCII diagram, e.g.:

```
Source[TPG] → Converter[JPEG] → Sink[QuickTime]
```

with each stage's active config overrides listed underneath. Useful
for `--help` style introspection and for debugging at a glance when
running a long capture.

## Tasks

- [ ] Pick a renderer style (single-line arrow vs multi-line box).
  Multi-line probably reads better when fan-out is involved.
- [ ] Walk the resolved `MediaPipelineConfig` (post-autoplan) to
  build the diagram so bridge stages appear inline.
- [ ] Render under `mediaplay --plan` and as a header for
  `--stats` output.
