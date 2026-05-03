# Media I/O Subsystem {#mediaio}

The MediaIO framework is libpromeki's uniform interface for
reading and writing media — files, image sequences, capture
cards, codecs, and synthetic generators. The framework is
documented through two audience-targeted guides.

## Two guides {#mediaio_guides}

- **@ref mediaio_user_guide "MediaIO User Guide"** — for application
  authors. The always-async API, factory entry points,
  `MediaIORequest`, ports and port groups, port connections,
  cancellation, signals, and the catalog of available backends.
- **@ref mediaio_backend_guide "MediaIO Backend Author Guide"** —
  for backend authors. Strategy class selection, factory
  registration, the `executeCmd` contract, port construction
  during open, the cancellation contract, the live-capture
  pattern, and the test scaffolding.

## See also {#mediaio_see_also}

- **@ref inspector "Inspector — Frame validation and monitoring"** —
  the QA companion for the TPG synthetic source.
- **@ref mediapipeline "MediaPipeline"** — the pipeline composition
  layer that wraps MediaIO + port connections behind a single
  lifecycle.
- **@ref mediaplanner "MediaPipelinePlanner"** — automatic bridge
  insertion for format-mismatched stages.
- **@ref threading "Threading"** — process-wide concurrency model
  including the `ThreadPool`/`Strand` primitives the framework
  relies on.
