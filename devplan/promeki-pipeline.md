# ProMeKi Pipeline Demo

**Phase:** Demo (after 4A `MediaPipeline`, 3B HTTP/WebSocket, 3C Logger listener)
**Dependencies:** `MediaPipeline`, `MediaPipelinePlanner`, `MediaPipelineStats`, `HttpServer`, `WebSocket`, `HttpFileHandler`, `Resource` (cirf), `JpegVideoCodec`, `Logger`
**Library impact:** new `PipelineEvent` envelope on `MediaPipeline`, new stats tick, two new `MediaIO` sinks (`MediaIOTask_NullPacing`, `MediaIOTask_MjpegStream`)
**Demo executable:** `promeki-pipeline`

**Standards:** All code must follow `CODING_STANDARDS.md`. Every new class requires complete doctest unit tests. See `devplan/README.md` for full requirements.

---

## Overview

A Node-RED-style web application for designing and running `MediaPipeline` graphs visually. The demo serves a Vue 3 + Vue Flow frontend out of the libpromeki resource filesystem (cirf). The backend exposes the `MediaIO` type registry, per-type config schema (`VariantSpec`), and live pipeline lifecycle over a small REST + WebSocket API. Multiple pipelines run concurrently; each has its own tab in the UI. The planner's auto-inserted bridge stages render in the graph so the user can see what `build(autoplan=true)` actually produced.

Two new library-side `MediaIO` sinks are added so the demo has something interesting to play with: a `NullPacing` sink that pretends to be a real playback device (eats frames at a configured rate, throws them away after consuming them) and an `MjpegStream` sink that publishes a frame-rate-limited motion-JPEG feed for the frontend to render inline.

---

## Design Goals

- **Library-first.** Anything generally useful (event envelope, stats tick, MJPEG sink, null-pacing sink) lives in `promeki`, not in the demo. The demo is wiring + UI.
- **Frontend build is out-of-band.** `frontend/` has its own `package.json` and `vite` config. `npm run build` writes into `frontend-dist/`, which is committed and packaged via cirf. The C++ build never touches npm.
- **Schema-driven UI.** The frontend renders the per-stage config editor by walking the `Config::SpecMap` returned from the backend. Adding a new `MediaIO` backend with new config keys requires zero frontend code.
- **Bridge transparency.** When `build(autoplan=true)` inserts CSC / FrameSync / VideoEncoder + VideoDecoder stages, the resolved config (with deterministic bridge names `<from>__bridge<N>__<to>`) is what comes back to the UI, which renders bridge stages as small muted chips so the user sees the planner's decisions.
- **Multi-pipeline.** The backend keeps a map of pipelines by id; the UI presents them as tabs across the top with a `+` to create a new one and a gear icon for per-pipeline settings (stats interval, autoplan policy, name).

---

## Phase A — Library: event envelope, stats tick, Logger tap

Most of the plumbing already exists: `MediaPipeline` emits `stageOpened` / `stageStarted` / `stageStopped` / `stageClosed` / `pipelineError` / `closed`, and `MediaPipeline::stats()` returns a populated `MediaPipelineStats`. This phase is a thin layer that unifies them into one subscribable stream.

**Files:**
- [x] `include/promeki/pipelineevent.h`
- [x] `src/proav/pipelineevent.cpp`
- [x] `tests/pipelineevent.cpp`
- [x] Modifications to `include/promeki/mediapipeline.h` and `src/proav/mediapipeline.cpp`
- [x] Test additions in `tests/mediapipeline.cpp`

### `PipelineEvent`

A Shareable value-typed envelope that the demo (or any subscriber) can serialize and ship over the wire.  `Variant` cannot directly hold `MediaPipelineConfig` / `MediaPipelineStats`, so the envelope carries **two** payload slots: a `Variant` `payload` for primitive payloads (state name, log message) and a `JsonObject` `jsonPayload` for the complex object payloads.

```cpp
class PipelineEvent {
        PROMEKI_SHARED_FINAL(PipelineEvent)
        public:
                enum class Kind {
                        StateChanged,    // payload = String (MediaPipeline::State name, e.g. "Running")
                        StageState,      // payload = String transition: "Opened"|"Started"|"Stopped"|"Closed"
                        StageError,      // payload = String message; metadata "code" carries Error name
                        StatsUpdated,    // jsonPayload = MediaPipelineStats::toJson()
                        PlanResolved,    // jsonPayload = MediaPipelineConfig::toJson() (resolved)
                        Log              // payload = String message; metadata: level, source, line, threadName
                };

                Kind kind() const;
                const String &stageName() const;
                const Variant &payload() const;
                const JsonObject &jsonPayload() const;
                const Metadata &metadata() const;
                const TimeStamp &timestamp() const;

                JsonObject toJson() const;
                static PipelineEvent fromJson(const JsonObject &obj, Error *err = nullptr);
                static String kindToString(Kind k);
                static Kind kindFromString(const String &s, bool *ok = nullptr);
};
```

Doctest coverage: round-trip through JSON for each kind, with each payload shape the demo will see.

**Note on `StageState`:** `MediaIO` does not expose a public state enum (just `Mode` + `isOpen()`), and the existing pipeline-level signals already collapse to four canonical transitions, so the payload is a transition string (`"Opened"`, `"Started"`, `"Stopped"`, `"Closed"`) rather than a state name.

### MediaPipeline additions

- [x] `using EventCallback = std::function<void(const PipelineEvent &)>;`
- [x] `int subscribe(EventCallback cb);` — returns subscription id; callback runs on subscriber's `EventLoop` (cross-thread dispatch through existing connect machinery).
- [x] `void unsubscribe(int id);`
- [x] `void setStatsInterval(Duration interval);` — `Duration::zero` = disabled (default). When set and pipeline is `Running`, internal timer calls `stats()` and emits `statsUpdated(MediaPipelineStats)` signal + dispatches a `StatsUpdated` `PipelineEvent`. (Plan originally said `MediaDuration` — that's the frame-range type, not a wall-clock interval.)
- [x] `PROMEKI_SIGNAL(statsUpdated, MediaPipelineStats);`
- [x] Internal: `Logger::addListener` registration when first subscriber connects, deregister when last leaves; forwarded as `Log` events.
- [x] Internal: `PlanResolved` dispatched at the tail of `build(...)` whenever autoplan ran (carry the resolved `MediaPipelineConfig`).

Existing signals (`stageOpened` / `stageStarted` / `stageStopped` / `stageClosed`) collapse into `StageState` events; `pipelineError` becomes a pipeline-level `StageError` (empty `stageName`); `closed` becomes a `StateChanged` event with `State::Closed`.

**Tests:**
- [x] Subscribing receives every published kind once.
- [x] Cross-thread subscribers get callbacks on the right `EventLoop`.
- [x] `setStatsInterval` ticks emit `StatsUpdated` while running, stop emitting on `stop()`.
- [x] Subscription survives multiple build / start / stop cycles.

---

## Phase B1 — Library: `MediaIOTask_NullPacing` (Sink)

A frame-eating sink that pretends to be a real playback device for demo purposes. It accepts whatever the upstream produces, throws each frame away after holding it for the configured wall-clock interval, and reports normal stats so the UI shows real numbers.

**Files:**
- [x] `include/promeki/mediaiotask_nullpacing.h`
- [x] `src/proav/mediaiotask_nullpacing.cpp`
- [x] `tests/mediaiotask_nullpacing.cpp`

**Config keys** (added to `MediaConfig`):
- [x] `NullPacingMode` — enum `Wallclock` (default) | `Free`. Free drains as fast as upstream feeds.
- [x] `NullPacingTargetFps` — `Rational`, default `0/1` meaning "follow source descriptor".
- [x] `NullPacingBurnTimings` — `bool`, default `false`. When true, logs per-frame jitter / period at debug level (handy for demos).

**Behavior:**
- Source-pull style. Sink emits `frameWanted` only at `1 / TargetFps` intervals. Frames arriving between ticks are accepted, logged in the drop counter, and discarded. The held `frameWanted` naturally back-pressures the upstream so source rates settle to the configured pacing.
- Reports `MediaIOStats` for `FramesPerSecond`, `FramesDropped`, `AverageLatencyUs` (time from arrival to discard), `PeakLatencyUs`.

**Tests:**
- [x] Wallclock mode: TPG @ 60 fps + NullPacing @ 24 fps → exactly 24 fps observed at sink, 36/s dropped at source, no errors.
- [x] Free mode: drains at upstream rate.
- [x] `TargetFps = 0/1` falls back to source `MediaDesc` rate.

---

## Phase B2 — Library: `MediaIOTask_MjpegStream` (Sink)

JPEG-encoded preview stream. Transport-agnostic: the sink doesn't own a server, it owns a tiny ring of latest-encoded frames and notifies attached subscribers. A one-liner helper wires up an `HttpServer` route that serves `multipart/x-mixed-replace; boundary=...`.

**Files:**
- [x] `include/promeki/mediaiotask_mjpegstream.h`
- [x] `src/proav/mediaiotask_mjpegstream.cpp`
- [x] `tests/mediaiotask_mjpegstream.cpp`

**Config keys** (added to `MediaConfig`):
- [x] `MjpegMaxFps` — `Rational`, default `15/1`. Frames arriving inside `1 / MaxFps` of the last encoded frame are dropped *before* JPEG encoding.
- [x] `MjpegQuality` — `S32`, range 1–100, default 80.
- [x] `MjpegMaxQueueFrames` — `S32`, default 1. Ring depth; subscribers always receive the newest, never get queued up by a slow client.

Pixel-format handling: the sink accepts only 8-bit interleaved 4:2:0 / 4:2:2 / 4:4:4 (i.e. what the JPEG encoder takes natively). For anything else, the planner's CSC bridge inserts the conversion — the sink declares its accepted descriptor via `MediaIO::proposeInput` and lets autoplan handle the rest. No internal CSC.

**Subscriber API:**
```cpp
class MjpegStreamSubscriber {
        public:
                virtual ~MjpegStreamSubscriber() = default;
                virtual void onFrame(const Buffer &jpeg, const TimeStamp &ts) = 0;
                virtual void onClosed() = 0;
};

int  attachSubscriber(MjpegStreamSubscriber *s);     // returns id; s lifetime managed by caller
void detachSubscriber(int id);
```

**HTTP helper** (in the sink, not in `HttpServer` core):
- [x] `void registerHttpRoute(HttpServer &server, const String &path);`
  - **Shipped shape (Phase B2 streaming follow-up):** continuous `multipart/x-mixed-replace; boundary=promeki-mjpeg` stream. Each new encoded JPEG is emitted as a multipart part with its own `Content-Type: image/jpeg` and `Content-Length` headers. Browsers consume this natively via `<img src>` — no polling needed.
  - **Implementation:** the route allocates a per-connection `AsyncBufferQueue` (a sequential `IODevice` with `enqueue`/`closeWriting` producer-side API) plus a per-connection `MjpegStreamSubscriber` adapter parented to the queue. The encoder pushes the same `Buffer::Ptr` to every subscriber (encode-once, share-by-pointer). On client disconnect the connection drops the queue's last shared ref, the queue's destructor cascades into the adapter's destructor (queue is its `ObjectBase` parent), and `detachSubscriber` runs from there. On sink close, every subscriber receives `onClosed`, which in the adapter calls `queueRef->closeWriting()` so the connection drains the chunked terminator and finishes cleanly.
  - **The async-read primitive (delivered with this phase):** `HttpConnection::pumpWrite` now distinguishes `read()==0 && atEnd()==false` (park: unsubscribe `IoWrite`, hook the device's `readyRead`, resume on the next ready signal) from `read()==0 && atEnd()==true` (existing EOF behaviour). File-backed bodies (`atEnd()==true` at EOF) are unaffected.

**Tests:**
- [x] Source pumps 60 fps, sink configured at 15 fps → ~15 frames/sec arrive at subscriber, no encoder backlog, dropped count tracks 75% of input.
- [x] Multiple subscribers see the same frames (latest-N broadcast).
- [x] Subscriber detach during stream is clean (no leaks, no callbacks after detach).
- [x] HTTP streaming smoke test: register on `127.0.0.1:0`, connect a hand-rolled HTTP/1.1 client, parse the chunked `multipart/x-mixed-replace` body, assert ≥3 distinct JPEG parts arrive within ~3 s, each carrying valid SOI/EOI markers; verify client disconnect tears down the subscriber cleanly.
- [x] HTTP refusal test: route returns `503` when the sink is not currently open.

---

## Phase C — Demo scaffold

**Files:**
- [x] `demos/promeki-pipeline/CMakeLists.txt`
- [x] `demos/promeki-pipeline/main.cpp`
- [x] `demos/promeki-pipeline/pipelinemanager.{h,cpp}`
- [x] `demos/promeki-pipeline/apiroutes.{h,cpp}` (stub)
- [x] `demos/promeki-pipeline/eventbroadcaster.{h,cpp}` (stub)
- [x] `demos/promeki-pipeline/pipelinesettings.{h,cpp}` (per-pipeline settings, JSON round-trip)
- [x] `demos/promeki-pipeline/resources.json` (cirf manifest, prefix `"promeki-pipeline"`)
- [x] `demos/promeki-pipeline/frontend/` (vite project: `package.json`, `vite.config.ts`, `index.html`, `src/main.ts`, `tsconfig.json`, `.gitignore`)
- [x] `demos/promeki-pipeline/frontend-dist/` (initially: minimal placeholder `index.html` so Phase C can run without Phase E)
- [x] `demos/promeki-pipeline/.gitattributes` (`frontend-dist/** linguist-generated=true -diff`)

**CMake:**
```cmake
if(TARGET promeki::promeki)
    cirf_add_resources(promeki_pipeline_resources
        CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/resources.json
        OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/promeki_pipeline_resources)

    add_executable(promeki-pipeline
        main.cpp
        pipelinemanager.cpp
        apiroutes.cpp
        eventbroadcaster.cpp)
    target_link_libraries(promeki-pipeline PRIVATE
        promeki::promeki
        promeki_pipeline_resources)
    target_include_directories(promeki-pipeline PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/promeki_pipeline_resources)

    install(TARGETS promeki-pipeline RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
```

**`main.cpp` shape:**
- Construct `Application`, parse `--port` (default 8080) and `--bind` flags.
- Construct `HttpServer`, `PipelineManager`, `EventBroadcaster`.
- Call `register_promeki_pipeline_resources("promeki-pipeline")`.
- Mount static files: `server.route("/{path:*}", HttpMethod::Get, HttpFileHandler::Ptr::takeOwnership(new HttpFileHandler(":/promeki-pipeline")))`.
- Wire API routes (Phase D).
- `server.listen(port)`, then `app.exec()`.

**`PipelineManager`:**
- Owns `Map<String, Entry>` where `Entry { String name; PipelineSettings settings; MediaPipeline::Ptr pipeline; MediaPipelineConfig userConfig; MediaPipelineConfig resolvedConfig; }`.
- `String create(const String &name)` → returns generated id (UUID short form).
- `Error replaceConfig(id, const MediaPipelineConfig &userConfig)` — only when state ∈ {Empty, Built, Closed}.
- `Error build(id, bool autoplan)` — if autoplan, runs `MediaPipelinePlanner::plan` and stores the resolved config; otherwise resolved == user.
- `Error open/start/stop/close(id)`.
- Snapshots: `JsonObject describe(id) const` returns `{ id, name, settings, userConfig, resolvedConfig, state }`.

**`PipelineSettings`** (per-pipeline, distinct from graph):
- [x] `String name`
- [x] `Duration statsInterval` (default 1 s, 0 = off) — wall-clock interval, not the frame-range `MediaDuration`
- [x] `MediaPipelinePlanner::Quality quality` (default Highest)
- [x] `int maxBridgeDepth` (default 4)
- [x] `StringList excludedBridges`
- [x] `bool autoplan` (default true)

JSON round-trip: `toJson()` / `fromJson()`.

**`EventBroadcaster`:**
- Subscribes to each pipeline's `PipelineEvent` stream.
- Maintains `Map<int, WebSocket *>` of connected WS clients with optional `pipelineId` filter.
- For each event, serializes envelope and sends as one WS text frame to matching subscribers.

After Phase C the binary builds, listens on `:8080`, serves a placeholder `index.html`, but exposes no API yet.

---

## Phase D — REST + WebSocket API

All routes return JSON. Errors use `HttpResponse::badRequest(...)` / `notFound(...)` with `{ "error": "..." }` body.

**Type registry / schema:**
- [x] `GET /api/types` → `[{ "name": "TPG", "displayName": "Test Pattern Generator", "description": "...", "modes": ["Source"], "extensions": [...], "schemes": [...] }]`.  `displayName` is the human-readable label sourced from `MediaIO::FormatDesc::displayName` (added 2026-04-25); when a backend leaves the field empty the API echoes the canonical `name` so the UI never has to special-case empty.  Library-side change: every registered backend now sets a sensible `displayName` (e.g. `"MJPEG Preview Stream"`, `"Color Space Converter"`, `"Burn-in (Timecode / Text)"`).
- [x] `GET /api/types/{name}/schema` → `{ "<configKey>": { "types": ["S32"], "default": ..., "min": ..., "max": ..., "enum": [...], "description": "..." } }` from `MediaIO::configSpecs(name)`.
- [x] `GET /api/types/{name}/defaults` → `Config` from `MediaIO::defaultConfig(name)`.
- [x] `GET /api/types/{name}/metadata` → default `Metadata` from `MediaIO::defaultMetadata(name)`.

**Pipelines:**
- [x] `GET    /api/pipelines` → list summaries.
- [x] `POST   /api/pipelines` → body `{ name, settings?, userConfig? }` → `{ id }`.
- [x] `GET    /api/pipelines/{id}` → full describe (user + resolved config + state).
- [x] `PUT    /api/pipelines/{id}` → replace `userConfig`.
- [x] `DELETE /api/pipelines/{id}` → close + remove.
- [x] `GET    /api/pipelines/{id}/settings` → `PipelineSettings`.
- [x] `PUT    /api/pipelines/{id}/settings` → replace settings (applies stats interval immediately).
- [x] `POST   /api/pipelines/{id}/build`  → optional `?autoplan=0`.
- [x] `POST   /api/pipelines/{id}/open`
- [x] `POST   /api/pipelines/{id}/start`
- [x] `POST   /api/pipelines/{id}/stop`
- [x] `POST   /api/pipelines/{id}/close`

**Preview streams** (one dynamic route, dispatches by id + stage at request time):
- [x] `GET /api/pipelines/{id}/preview/{stage}` → continuous `multipart/x-mixed-replace; boundary=promeki-mjpeg` MJPEG stream (`503` when the sink is not currently open, `404` when the pipeline / stage is unknown, `400` when the stage is not an `MjpegStream` sink).  Implemented as a single dynamic route registered at startup by `ApiRoutes`; the handler resolves `id` → `MediaPipeline`, `stage` → `MediaIO`, downcasts to the registered `MjpegStream` task and delegates to the new `MediaIOTask_MjpegStream::buildMultipartHandler` helper.  Browsers consume this natively via `<img src>` — no polling needed.

**Live events:**
- [x] `WS /api/events` (optional `?pipeline={id}` to filter). Each `PipelineEvent` is sent as a single WebSocket text frame containing the JSON envelope (with the originating pipeline id stamped in).

**Static files:**
- [x] `GET /{path:*}` → `HttpFileHandler` rooted at `:/promeki-pipeline/`.  Specificity-based routing in `HttpRouter` keeps the catch-all from swallowing `/api/*` even though it's registered last.

---

## Phase E — Frontend (Vue 3 + Vue Flow)

`demos/promeki-pipeline/frontend/` is a standalone vite project. `npm run build` writes into `../frontend-dist/`. cirf packages `frontend-dist/` as `:/promeki-pipeline/`.

**Stack:**
- Vue 3, TypeScript, Vite
- `@vue-flow/core` for graph canvas (pan/zoom/connect/select)
- Pinia for state (pipelines map, selected node id, ws connection)
- No CSS framework — small custom styles. Optional: `pinia-plugin-persistedstate` for last-open tabs.

**Layout:**
```
┌──────────────────────────────────────────────────────────────┐
│ [tab1*] [tab2] [tab3] [+]                              [⚙]   │
├──────────┬────────────────────────────────────────┬──────────┤
│ Palette  │                                        │  Editor  │
│ ─Source  │                                        │          │
│  TPG     │           Vue Flow canvas              │ <Spec    │
│  V4L2    │           (selected pipeline)          │  Field/> │
│  ...     │                                        │  ...     │
│ ─Sink    │                                        │          │
│  ...     │                                        │ [Apply]  │
│ ─Xform   │                                        │          │
│  ...     │                                        │          │
├──────────┴────────────────────────────────────────┴──────────┤
│ Log / Stats panel (collapsible)                               │
└──────────────────────────────────────────────────────────────┘
```

**Components:**
- [x] `App.vue` — top-level layout, tab strip, gear icon → `<PipelineSettingsModal/>`.
- [x] `PaletteView.vue` — fetches `/api/types`, groups by Source / Sink / Transform, drag start sets a payload picked up by Vue Flow drop.  Each palette item renders the backend's `displayName` (sourced from `FormatDesc::displayName` via `/api/types`) as the only visible label; the canonical `name` stays in the drag payload and the item tooltip but is no longer printed in the UI.
- [x] `GraphCanvas.vue` — Vue Flow wrapper. Renders user nodes and bridge nodes (different style); routes user edits back to the Pinia store; submits to backend on commit.  When a palette item is dropped the new stage's name is auto-generated as `<mode>N` where mode is the lowercased drag-source mode (`source`, `sink`, `transform`) and N is the smallest positive integer not already taken — so dropping a TPG produces `source1`, then a CSC produces `transform1`, then a NullPacing produces `sink1`, then another TPG produces `source2`.  The user can rename via NodeChip's inline rename UI afterwards.
- [x] `NodeChip.vue` — generic node, shows the backend's `displayName` on the header, the editable stage name, and a state badge.  The canonical type id is held in `data.type` (used by previews + tooltips) but is not rendered in the UI.
- [x] `BridgeChip.vue` — small muted chip for stages whose name matches `^br\d+_.+_.+$`; non-deletable; tooltip shows "Auto-inserted by planner".
- [x] `MjpegPreview.vue` — `<img>` for any selected `MjpegStream` sink whose `src` points at the multipart streaming endpoint. Browsers update the visible frame natively as each multipart part arrives; no polling, no cache-busting needed. On 503 (sink not yet streaming) we re-mount the `<img>` every 500 ms.
- [x] `ConfigEditor.vue` — given selected stage, fetches `/api/types/{name}/schema`, renders `<SpecField>` per key.
- [x] `SpecField.vue` — switches on `Variant::Type`: integer types (`S8`/`U8`/`S16`/`U16`/`S32`/`U32`/`S64`/`U64` / `int`) → number input with `min`/`max` if present; floats (`F32`/`F64`) → free-step number input; `Bool` → toggle; `String` → text; `Enum` (any spec carrying an `enum: {type, values}` block) → dropdown; `EnumList` → vertical stack of checkboxes whose checked items round-trip to the comma-separated wire form; `Rational<int>` → two-int `n/d` composite; `FrameRate` → preset dropdown sourced from the spec's `presets` block (synthesised in `apiroutes.cpp` from `FrameRate::wellKnownRates()`) plus a `Custom...` option that reveals the underlying two-int `n/d` editor (and bare FrameRate specs without presets still fall back to the rational editor); `Size2D` → two-int `WxH` composite; `Color` → HTML5 color picker + canonical `Model(r,g,b,a)` text input + 0..1 alpha spinner; `SocketAddress` → `host:port` text input; `ContentLightLevel` → `MaxCLL` / `MaxFALL` composite; `MasteringDisplay` → 10-field composite covering R/G/B/WP chromaticities and min/max luminance; `PixelFormat` / `PixelMemLayout` / `VideoCodec` → dropdown driven by an `enum` block synthesised in `apiroutes.cpp` from the type's `registeredIDs()`.  A raw-JSON textarea remains as a defensive fallback but emits `console.warn` if it ever fires on a real backend schema.
- [x] `LogStatsPanel.vue` — bottom panel, two tabs: log lines (filtered), stats table (per-stage from `StatsUpdated`).
- [x] `PipelineSettingsModal.vue` — gear-icon modal: name, stats interval, autoplan toggle + quality + maxBridgeDepth + excluded bridges (multi-select from `/api/types`).
- [x] `TabStrip.vue` — tab strip + `Build`/`Open`/`Start`/`Stop`/`Close`/⚙ toolbar (split out from `App.vue` for testability).

**Stores (Pinia):**
- [x] `usePipelinesStore` — map of `{id, name, settings, userConfig, resolvedConfig, state, latestStats, log}`; actions for create / build / start / stop / close / replaceConfig / replaceSettings, plus debounced `scheduleConfigPush` for canvas edits.
- [x] `useTypesStore` — `/api/types` cache + per-type schema / defaults / metadata cache (lazy).
- [x] `useEventsStore` — WS connection, dispatches incoming events into the pipelines store; reconnects with exponential backoff.

**Build wiring:**
- [x] `frontend/package.json` scripts: `dev`, `build` (writes to `../frontend-dist/`), `preview`.
- [x] `frontend/vite.config.ts` `build.outDir` set to `../frontend-dist/`, dev proxy `/api → http://localhost:8080`, `/api/events` ws proxy.
- [x] `frontend-dist/` is committed; `.gitattributes` flags it `linguist-generated`.
- [x] `resources.json` updated to include `frontend-dist/index.html` plus `assets/*` glob (vite emits hashed bundle names; the glob keeps the manifest stable across rebuilds).

**Bridge node UX:**
- Nodes whose name matches `^br\d+_.+_.+$` get the `BridgeChip` component, smaller font, muted color, no resize handles, no delete affordance. Edges into / out of bridge nodes are non-interactive (the bridge goes away when the user removes one of the surrounding stages).
- The graph the user *edits* is the `userConfig`. The graph the user *sees* is the `resolvedConfig` (which contains both user stages and bridges) when non-empty, otherwise the `userConfig`. The frontend never PUTs bridges back — `setUserConfig` only ever mutates user stages.

---

## Acceptance criteria

Phase A — `subscribe()` returns each kind for a TPG → NullPacing pipeline; `setStatsInterval(1s)` produces ticking `StatsUpdated` events; `Logger::info("test")` appears as a `Log` event for every active subscriber.

Phase B1 — A pipeline of TPG (60 fps) → NullPacing (24 fps Wallclock) reports exactly 24 fps at the sink for 10 s with no errors and dropped count = source rate − sink rate.

Phase B2 — `curl http://localhost:8080/api/pipelines/<id>/preview/<stage>` from a running TPG → MjpegStream pipeline streams a `multipart/x-mixed-replace` body containing successive JPEG parts (each verified to carry SOI/EOI). A browser pointing `<img src>` at the same URL updates the visible frame at the sink's configured cadence without polling.

Phase C — `promeki-pipeline --port 8080` runs, serves a placeholder page at `http://localhost:8080/`.

Phase D — Building a TPG → NullPacing pipeline end-to-end via curl works (POST config, POST build, POST open, POST start; observe `WS /api/events` carrying `StateChanged`, `StageState`, `StatsUpdated`).

Phase E — User can drag TPG → MjpegStream onto canvas (drops land anywhere within the canvas pane, including the corners, and at the cursor exactly regardless of pan/zoom — `GraphCanvas` uses `useVueFlow().screenToFlowCoordinate` to convert the drop event into flow-space), click each node (the selected chip lights up with a 2px accent-blue border + soft glow via the `selected` prop forwarded by Vue Flow), edit its config, hit Build (autoplan), see CSC bridge inserted as a small chip between them at the midpoint of its from/to neighbors, hit Run, see live frames in the inline preview while the selected sink is `Running`, and observe stats ticking in the bottom panel.  Node positions persist across page reloads (stored on each `Stage::metadata` via the `Frontend.X` / `Frontend.Y` IDs declared on `Metadata`) and are stable across re-renders (per-pipeline runtime cache in `GraphCanvas` is the single source of truth — deleting one node leaves every other node exactly where it was; bridges derive their position once and stay there).  Selecting a node + pressing Delete or Backspace, or clicking the per-node × badge, removes the stage and any routes touching it.  After Stop, clicking [Run] drives the pipeline back to Running through the close → build → open → start cascade (`PipelineManager::run`, exposed at `POST /api/pipelines/{id}/run`).  Settings, multi-pipeline tabs, and per-pipeline filtered logs are all wired up; the served bundle is the vite production build packaged through cirf.

---

## Out of scope (future work)

- Saving / loading pipeline configs to disk through the UI (the JSON round-trip is already there; a "save preset" endpoint is a small follow-up).
- Authentication / multi-user — demo binds to localhost.
- Hot-edit during run (currently must stop / rebuild for any change).
- Nested pipelines / sub-graphs.
- Custom node icons per backend (would need an `icon` field on `MediaIO::FormatDesc`).
