# Media I/O Subsystem {#mediaio}

The MediaIO framework is libpromeki's uniform interface for
reading and writing media — files, image sequences, capture
cards, codecs, and synthetic generators. The framework is
documented through two audience-targeted guides.

## Two guides {#mediaio_guides}

- **[MediaIO User Guide](mediaio_user_guide.md)** — for application
  authors. The always-async API, factory entry points,
  `MediaIORequest`, ports and port groups, port connections,
  cancellation, signals, and the catalog of available backends.
- **[MediaIO Backend Author Guide](mediaio_backend_guide.md)** —
  for backend authors. Strategy class selection, factory
  registration, the `executeCmd` contract, port construction
  during open, the cancellation contract, the live-capture
  pattern, and the test scaffolding.

## See also {#mediaio_see_also}

- **[Inspector — Frame validation and monitoring](inspector.md)** —
  the QA companion for the TPG synthetic source.
- **[MediaPipeline](mediapipeline.md)** — the pipeline composition
  layer that wraps MediaIO + port connections behind a single
  lifecycle.
- **[MediaPipelinePlanner](mediaplanner.md)** — automatic bridge
  insertion for format-mismatched stages.
- **[Threading](threading.md)** — process-wide concurrency model
  including the `ThreadPool`/`Strand` primitives the framework
  relies on.
