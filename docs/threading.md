# Threading and Concurrency {#threading}

How to safely use promeki classes across threads.

This page describes the threading model, thread safety guarantees, and
recommended patterns for concurrent programming with promeki.

## Threading Model {#thread_model}

promeki uses a thread-affine model for functional objects (`ObjectBase`)
and a value-handoff model for data objects. The key concepts:

- **Data objects** are plain values. They are not internally
  synchronized. Share them across threads via `Ptr` (`SharedPtr`)
  with ownership handoff, or by copying simple types.
- **Functional objects** (`ObjectBase`) are bound to a specific thread's
  `EventLoop`. Call their methods only from that thread. Use
  signals/slots for cross-thread communication — the signal system
  automatically marshals calls to the receiver's EventLoop.
- **Thread-safe classes** like `Queue`, `Mutex`, and
  `Atomic` are explicitly documented as such and can be used from
  any thread.

## Thread Safety Categories {#thread_categories}

Every class documents its thread safety guarantee using one of these
categories:

| Category | Meaning | Examples |
|----------|---------|----------|
| **Thread-safe** | All public methods may be called concurrently from any thread. Internally synchronized. | `Queue`, `Mutex`, `ReadWriteLock`, `WaitCondition`, `Atomic`, `ThreadPool`, `Future` |
| **Not thread-safe** | External synchronization required for concurrent access to the same instance. | Most data objects when accessed directly |
| **Conditionally thread-safe** | Distinct instances may be used concurrently. Concurrent access to a single instance requires external synchronization. | `List`, `Map`, `Set`, `String` |
| **Thread-affine** | Must be used from the thread that created it (or the thread it was moved to via `moveToThread()`). | `ObjectBase` and all subclasses |

## Sharing Data Objects {#thread_data}

See the [Data Object Categories](@ref dataobjects) page for detailed
patterns and code examples. Summary:

- **Shareable types**: wrap in `Ptr` and hand off. Do not mutate
  from the original thread after handoff.
- **Simple types**: copy by value — they are small and cheap.
- **Composite data**: build a shareable struct, share via `Ptr`.
- **Do not** use `Mutex` to protect data objects. Restructure to use
  copy or `Ptr` handoff instead.

## ObjectBase and Thread Affinity {#thread_objectbase}

Every `ObjectBase` instance is associated with the `EventLoop` of
the thread that created it. This is its thread affinity.

- Call methods on an `ObjectBase` only from its affiliated thread.
- Use `moveToThread()` to change affinity. The object must not be
  in use on the old thread when you move it.
- Timers started with `startTimer()` fire on the object's EventLoop.

### Cross-Thread Signals {#thread_signals}

When a signal is connected to a slot on a different thread's
EventLoop, the signal system automatically queues the call and
delivers it on the receiver's thread. This is the primary mechanism
for safe cross-thread communication between functional objects.

```cpp
// producer runs on thread A
// consumer runs on thread B
ObjectBase::connect(&producer.dataReadySignal, &consumer.onDataReadySlot);

// When producer emits dataReady, the slot is invoked on thread B's
// EventLoop -- not on thread A.  No manual synchronization needed.
```

Signal arguments are marshaled via `VariantList` for cross-thread
dispatch, so all argument types must be representable as `Variant`.

## EventLoop {#thread_eventloop}

Each thread can have at most one `EventLoop`. The EventLoop
processes events, timers, deferred calls, and cross-thread signal
deliveries.

- `exec()` runs the loop, blocking until `quit()` is called.
- `processEvents()` runs one iteration without blocking — suitable
  for integration with external loops or WASM environments.
- `Thread` provides a built-in EventLoop. The main thread's
  EventLoop is set up by `Application`.

## Concurrency Primitives {#thread_primitives}

promeki provides wrapper classes for standard C++ synchronization
primitives. These wrappers offer Qt-inspired naming and integrate
with each other (e.g., `WaitCondition` works with `Mutex`).
Always use the library wrappers instead of raw `std::` types.

### Mutex {#thread_mutex}

`Mutex` wraps `std::mutex` with `lock()`, `unlock()`, and
`tryLock()`. Use the nested `Mutex::Locker` for RAII scoped
locking:

```cpp
Mutex mutex;

// RAII locking -- preferred
{
        Mutex::Locker locker(mutex);
        // critical section
}

// Manual locking -- when RAII scope doesn't fit
mutex.lock();
// critical section
mutex.unlock();
```

### ReadWriteLock {#thread_rwlock}

`ReadWriteLock` wraps `std::shared_mutex` for reader-writer
patterns where multiple concurrent readers are safe but writers
need exclusive access.

```cpp
ReadWriteLock rwl;

// Multiple readers can hold this simultaneously
{
        ReadWriteLock::ReadLocker locker(rwl);
        // read-only access
}

// Writer gets exclusive access
{
        ReadWriteLock::WriteLocker locker(rwl);
        // read-write access
}
```

### WaitCondition {#thread_waitcondition}

`WaitCondition` wraps `std::condition_variable` for thread
signaling. It works with `Mutex` and supports both predicate
and non-predicate waits with optional timeouts.

```cpp
Mutex mutex;
WaitCondition cv;
bool ready = false;

// Waiting thread
mutex.lock();
cv.wait(mutex, [&] { return ready; });
// ready is now true
mutex.unlock();

// Signaling thread
{
        Mutex::Locker locker(mutex);
        ready = true;
}
cv.wakeOne();  // or cv.wakeAll()
```

### Atomic {#thread_atomic}

`Atomic` wraps `std::atomic` with acquire/release semantics.
Use `value()` to load and `setValue()` to store. Read-modify-write
operations (`fetchAndAdd()`, `compareAndSwap()`, `exchange()`) use
`memory_order_acq_rel`.

```cpp
Atomic<int> counter(0);
counter.setValue(42);
int old = counter.fetchAndAdd(1);  // old == 42, counter == 43
```

### Future and Promise {#thread_future}

`Future` and `Promise` provide asynchronous result passing.
A `Promise` produces a value (or error) that a `Future`
consumes. Both are move-only.

```cpp
Promise<int> p;
Future<int> f = p.future();

// Producer (e.g., on another thread)
p.setValue(42);

// Consumer
auto [value, err] = f.result();  // blocks until ready
```

`Future<void>` is specialized: `result()` returns an `Error`
instead of a `Result` pair. Both `Future<T>::result()` and
`Future<T>::waitForFinished()` accept an optional `timeoutMs`
parameter.

## ThreadPool {#thread_pool}

`ThreadPool` provides a pool of worker threads for running tasks
concurrently. Submit work with `submit()`, which returns a
`Future` for the result.

```cpp
ThreadPool pool;
Future<int> result = pool.submit([]() {
        return expensiveComputation();
});

// Later...
auto [value, err] = result.result();
```

Key features:

- Default thread count is `std::thread::hardware_concurrency()`.
- `setThreadCount(0)` switches to inline mode where tasks run on
  the calling thread — useful for WASM or single-threaded contexts.
- `waitForDone()` blocks until all submitted tasks complete.
- `clear()` discards pending (not running) tasks.

Tasks submitted to the pool should be self-contained. Pass data in
by copy (for simple types) or by `Ptr` (for shareable types). Do
not capture raw pointers to `ObjectBase` instances — use signals
instead to communicate results back.

## MediaIO Threading {#thread_pipeline}

`MediaIO` is abstract; concrete backends inherit from one of
three strategy classes that each pick a thread for command
execution: `InlineMediaIO` (calling thread), `SharedThreadMediaIO`
(per-instance `Strand` on a shared `ThreadPool`), or
`DedicatedThreadMediaIO` (an owned worker thread).
`SharedThreadMediaIO` is the default for compute backends — its
strand serializes commands per-instance while the pool keeps the
process-wide thread count bounded. I/O backends that block on
syscalls inherit from `DedicatedThreadMediaIO` so a slow backend
cannot starve the shared pool.

Frames move between `MediaIO` ports via `MediaIOPortConnection`,
which subscribes to the source's `frameReady` signal and pushes
each ready result into every connected sink. Sink writes apply
an always-on capacity gate that returns `Error::TryAgain` when
the sink is full; consumers wait on `frameWanted` before retrying.
