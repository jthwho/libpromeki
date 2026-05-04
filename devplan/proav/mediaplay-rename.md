# Rename `mediaplay` to `mediaio`

The utility is no longer specific to playback — it builds and runs
arbitrary `MediaPipeline` graphs (sources, transforms, sinks of any
shape). The name `mediaio` reflects current scope better.

## Tasks

- [ ] Rename `utils/mediaplay/` → `utils/mediaio/`; update CMake.
- [ ] Update install target, packaging, and any shell completions.
- [ ] Cross-references in `docs/`, `devplan/`, and `README.md`.
- [ ] Add `docs/mediaio.dox` (replacing the still-pending
  `docs/mediaplay.dox`) — full grammar reference with worked
  examples.
- [ ] Add a man page (and link from `docs/utils.dox`).
