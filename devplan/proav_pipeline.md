# ProAV Pipeline Framework Core

**Phase:** 4A
**Dependencies:** Phase 1 (Mutex, WaitCondition, Future, PriorityQueue, ThreadPool), Phase 2 (IODevice)
**Library:** `promeki-proav` (extends existing library)

**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

Generalizes the existing source/sink pattern in AudioBlock. Default threading model: thread pool. Nodes can override via `setThreadingPolicy()`.

---

## MediaPort

Data object describing a node's input or output connection point.

**Files:**
- [ ] `include/promeki/proav/mediaport.h`
- [ ] `src/mediaport.cpp`
- [ ] `tests/mediaport.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] `enum Direction { Input, Output }`
- [ ] `enum MediaType { Audio, Video, Data }`
- [ ] `String name() const`, `setName(const String &)` — human-readable port name
- [ ] `Direction direction() const`
- [ ] `MediaType mediaType() const`
- [ ] `AudioDesc audioDesc() const` — valid when mediaType == Audio
- [ ] `VideoDesc videoDesc() const` — valid when mediaType == Video
- [ ] `ImageDesc imageDesc() const` — valid when mediaType == Video (image format details)
- [ ] `void setAudioDesc(const AudioDesc &)`
- [ ] `void setVideoDesc(const VideoDesc &)`
- [ ] `bool isCompatible(const MediaPort &other) const` — checks type and format compatibility
- [ ] `bool isConnected() const`
- [ ] PROMEKI_SHARED_FINAL, `::Ptr`, `::List`, `::PtrList`
- [ ] Doctest: construction, compatibility checks, audio/video desc assignment

---

## MediaNode

ObjectBase-derived base class for all pipeline processing nodes.

**Files:**
- [ ] `include/promeki/proav/medianode.h`
- [ ] `src/medianode.cpp`
- [ ] `tests/medianode.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] Derive from `ObjectBase`, use `PROMEKI_OBJECT`
- [ ] `enum State { Idle, Configured, Running, Error }`
- [ ] `enum ThreadingPolicy { UseGraphPool, DedicatedThread, CustomPool }`
- [ ] `State state() const`
- [ ] `String name() const`, `setName(const String &)`
- [ ] Input/output ports:
  - [ ] `List<MediaPort::Ptr> inputPorts() const`
  - [ ] `List<MediaPort::Ptr> outputPorts() const`
  - [ ] `MediaPort::Ptr inputPort(int index) const`
  - [ ] `MediaPort::Ptr outputPort(int index) const`
  - [ ] `MediaPort::Ptr inputPort(const String &name) const`
  - [ ] `MediaPort::Ptr outputPort(const String &name) const`
  - [ ] `int inputPortCount() const`, `int outputPortCount() const`
- [ ] Protected: `addInputPort(MediaPort::Ptr)`, `addOutputPort(MediaPort::Ptr)`
- [ ] Threading:
  - [ ] `void setThreadingPolicy(ThreadingPolicy policy)`
  - [ ] `void setThreadingPolicy(ThreadPool *pool)` — CustomPool variant
  - [ ] `ThreadingPolicy threadingPolicy() const`
- [ ] Virtual lifecycle:
  - [ ] `virtual Error configure()` — validate ports, allocate resources. Transition Idle -> Configured.
  - [ ] `virtual Error start()` — begin processing. Transition Configured -> Running.
  - [ ] `virtual void stop()` — stop processing. Transition Running -> Idle.
  - [ ] `virtual void process() = 0` — process one cycle of data
- [ ] `PROMEKI_SIGNAL(stateChanged, State)`
- [ ] `PROMEKI_SIGNAL(errorOccurred, Error)`
- [ ] Doctest: state transitions, port management, via concrete test node subclass

---

## MediaLink

Connects output port to input port. Manages buffered data flow.

**Files:**
- [ ] `include/promeki/proav/medialink.h`
- [ ] `src/medialink.cpp`
- [ ] `tests/medialink.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] `MediaPort::Ptr source() const` — output port
- [ ] `MediaPort::Ptr sink() const` — input port
- [ ] `MediaNode *sourceNode() const`
- [ ] `MediaNode *sinkNode() const`
- [ ] `Queue<Frame::Ptr>` internal buffer between ports
- [ ] `void setQueueDepth(int depth)` — configurable backpressure. Default: 2.
- [ ] `int queueDepth() const`
- [ ] `int queuedFrameCount() const`
- [ ] `bool isFull() const`
- [ ] `bool isEmpty() const`
- [ ] `Error pushFrame(Frame::Ptr frame, unsigned int timeoutMs = 0)` — blocks if full. Returns `Error::Ok` or `Error::Timeout`.
- [ ] `Result<Frame::Ptr> pullFrame(unsigned int timeoutMs = 0)` — blocks if empty. Returns frame + `Error::Ok`, or null + `Error::Timeout`.
- [ ] `Error tryPushFrame(Frame::Ptr frame)` — non-blocking. Returns `Error::Ok` or error if full.
- [ ] `Result<Frame::Ptr> tryPullFrame()` — non-blocking. Returns frame + `Error::Ok`, or null + error if empty.
- [ ] Format negotiation: validate source/sink compatibility on connection
- [ ] PROMEKI_SHARED_FINAL, `::Ptr`, `::List`, `::PtrList`
- [ ] Doctest: push/pull frames, queue depth backpressure, format compatibility check

---

## MediaGraph

DAG of MediaNodes connected by MediaLinks. Validates topology.

**Files:**
- [ ] `include/promeki/proav/mediagraph.h`
- [ ] `src/mediagraph.cpp`
- [ ] `tests/mediagraph.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] Derive from `ObjectBase`, use `PROMEKI_OBJECT`
- [ ] `Error addNode(MediaNode *node)` — takes ownership
- [ ] `Error removeNode(MediaNode *node)`
- [ ] `List<MediaNode *> nodes() const`
- [ ] `MediaNode *node(const String &name) const` — find by name
- [ ] `MediaLink::Ptr connect(MediaPort::Ptr output, MediaPort::Ptr input)` — create link
- [ ] `MediaLink::Ptr connect(MediaNode *source, int outputIndex, MediaNode *sink, int inputIndex)` — convenience
- [ ] `MediaLink::Ptr connect(MediaNode *source, const String &outputName, MediaNode *sink, const String &inputName)`
- [ ] `Error disconnect(MediaLink::Ptr link)`
- [ ] `Error disconnect(MediaPort::Ptr output, MediaPort::Ptr input)`
- [ ] `List<MediaLink::Ptr> links() const`
- [ ] `Error validate()` — check topology:
  - [ ] No cycles (DAG requirement)
  - [ ] All required ports connected
  - [ ] Format compatibility on all links
  - [ ] Returns descriptive error on failure
- [ ] `List<MediaNode *> topologicalSort()` — processing order
- [ ] `List<MediaNode *> sourceNodes()` — nodes with no inputs
- [ ] `List<MediaNode *> sinkNodes()` — nodes with no outputs
- [ ] `void clear()` — remove all nodes and links
- [ ] Doctest: build graph, validate, topological sort, cycle detection, format mismatch detection

---

## MediaPipeline

Owns MediaGraph + default thread pool. Orchestrates processing.

**Files:**
- [ ] `include/promeki/proav/mediapipeline.h`
- [ ] `src/mediapipeline.cpp`
- [ ] `tests/mediapipeline.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] Derive from `ObjectBase`, use `PROMEKI_OBJECT`
- [ ] `enum State { Stopped, Starting, Running, Paused, Stopping, Error }`
- [ ] `MediaGraph *graph()` — access the managed graph
- [ ] `ThreadPool *threadPool()` — the default pool
- [ ] `void setThreadPool(ThreadPool *pool)` — override default
- [ ] `Error start()` — validate graph, configure all nodes, start processing
- [ ] `Error stop()` — stop all nodes, wait for completion
- [ ] `Error pause()` — pause processing
- [ ] `Error resume()` — resume from pause
- [ ] `State state() const`
- [ ] Processing loop:
  - [ ] Respects topological order from MediaGraph
  - [ ] Schedules `process()` calls to ThreadPool
  - [ ] Nodes with DedicatedThread policy get their own thread
  - [ ] Nodes with CustomPool policy use their specified pool
  - [ ] Data flows through MediaLinks between process() calls
  - [ ] Backpressure: if a MediaLink is full, upstream node blocks or skips
- [ ] `PROMEKI_SIGNAL(stateChanged, State)`
- [ ] `PROMEKI_SIGNAL(errorOccurred, Error)`
- [ ] `PROMEKI_SIGNAL(started)`
- [ ] `PROMEKI_SIGNAL(stopped)`
- [ ] Error propagation: node errors propagate to pipeline level
- [ ] Doctest: simple source -> sink pipeline, start/stop, pause/resume
- [ ] Demo: audio passthrough demo (AudioSourceNode -> AudioSinkNode)
