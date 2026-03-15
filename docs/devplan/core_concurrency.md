# Core Concurrency Primitives

**Phase:** 1B
**Dependencies:** None
**Pattern reference:** `include/promeki/core/queue.h` (existing mutex/CV/timeout conventions)

**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

All classes are utility classes — not data objects. No PROMEKI_SHARED_FINAL.

---

## Mutex

Wraps `std::mutex` with a nested RAII `Locker` type.

**Files:**
- [ ] `include/promeki/core/mutex.h`
- [ ] `tests/mutex.cpp`

**Implementation checklist:**
- [ ] Header guard, includes (`<mutex>`), namespace
- [ ] Wrap `std::mutex` as private member
- [ ] `lock()`
- [ ] `unlock()`
- [ ] `tryLock()` — returns bool
- [ ] Nested `Locker` class (RAII):
  - [ ] Constructor takes `Mutex &`, calls `lock()`
  - [ ] Destructor calls `unlock()`
  - [ ] Non-copyable, non-movable
- [ ] Non-copyable, non-movable
- [ ] Doctest: lock/unlock, Locker RAII scope, tryLock behavior

---

## ReadWriteLock

Wraps `std::shared_mutex` for reader-writer locking.

**Files:**
- [ ] `include/promeki/core/readwritelock.h`
- [ ] `tests/readwritelock.cpp`

**Implementation checklist:**
- [ ] Header guard, includes (`<shared_mutex>`), namespace
- [ ] Wrap `std::shared_mutex` as private member
- [ ] `lockForRead()` — shared lock
- [ ] `lockForWrite()` — exclusive lock
- [ ] `unlock()`
- [ ] `tryLockForRead()` — returns bool
- [ ] `tryLockForWrite()` — returns bool
- [ ] Nested `ReadLocker` class (RAII):
  - [ ] Constructor takes `ReadWriteLock &`, calls `lockForRead()`
  - [ ] Destructor calls `unlock()`
- [ ] Nested `WriteLocker` class (RAII):
  - [ ] Constructor takes `ReadWriteLock &`, calls `lockForWrite()`
  - [ ] Destructor calls `unlock()`
- [ ] Non-copyable, non-movable
- [ ] Doctest: concurrent reads allowed, write excludes readers, RAII lockers

---

## WaitCondition

Wraps `std::condition_variable`.

**Files:**
- [ ] `include/promeki/core/waitcondition.h`
- [ ] `tests/waitcondition.cpp`

**Implementation checklist:**
- [ ] Header guard, includes (`<condition_variable>`), namespace
- [ ] Wrap `std::condition_variable` as private member
- [ ] `Error wait(Mutex &mutex, unsigned int timeoutMs = 0)` — 0 means wait indefinitely. Returns `Error::Ok` if woken, `Error::Timeout` if timed out.
- [ ] `wakeOne()` — notify one waiting thread
- [ ] `wakeAll()` — notify all waiting threads
- [ ] Non-copyable, non-movable
- [ ] Implementation note: must convert `Mutex` internal `std::mutex` to `std::unique_lock` for CV wait
- [ ] Doctest: basic wake/wait between threads, timeout behavior

---

## Atomic\<T\>

Wraps `std::atomic<T>`.

**Files:**
- [ ] `include/promeki/core/atomic.h`
- [ ] `tests/atomic.cpp`

**Implementation checklist:**
- [ ] Header guard, includes (`<atomic>`), namespace
- [ ] Template class wrapping `std::atomic<T>`
- [ ] Constructor from `T` value (default: default-constructed T)
- [ ] `value()` — load with `memory_order_acquire`
- [ ] `setValue(T)` — store with `memory_order_release`
- [ ] `fetchAndAdd(T)` — returns old value (for integral types)
- [ ] `fetchAndSub(T)` — returns old value (for integral types)
- [ ] `compareAndSwap(T &expected, T desired)` — returns bool (wraps `compare_exchange_strong`)
- [ ] `exchange(T desired)` — returns old value
- [ ] Non-copyable, non-movable
- [ ] Doctest: basic load/store, fetchAndAdd, compareAndSwap

---

## Migrate Queue to Use New Primitives

The existing `Queue<T>` (`include/promeki/core/queue.h`) uses raw `std::mutex` and `std::condition_variable` internally. Once `Mutex` and `WaitCondition` are implemented, migrate Queue to use them — same principle as the std:: stream migration.

**Files:**
- [ ] Modify `include/promeki/core/queue.h`

**Implementation checklist:**
- [ ] Replace `std::mutex _mutex` with `Mutex _mutex`
- [ ] Replace `std::condition_variable _cv` with `WaitCondition _cv`
- [ ] Replace `std::unique_lock<std::mutex>` (Locker typedef) with `Mutex::Locker`
- [ ] Adapt `pop()`, `peek()`, `waitForEmpty()` timeout logic to use `WaitCondition::wait(Mutex &, timeoutMs)`
- [ ] Remove `#include <mutex>` and `#include <condition_variable>` if no longer needed
- [ ] All existing Queue tests must still pass — no behavioral change

---

## Promise\<T\>

Wraps `std::promise<T>`. Used with `Future<T>`.

**Files:**
- [ ] `include/promeki/core/promise.h` (may be combined with `future.h`)

**Implementation checklist:**
- [ ] Header guard, includes (`<future>`), namespace
- [ ] Wrap `std::promise<T>` as private member
- [ ] `setValue(T value)` — sets the result
- [ ] `setError(Error error)` — sets an error (stores via exception_ptr or custom mechanism)
- [ ] `future()` — returns `Future<T>`
- [ ] Move-only (non-copyable, movable)
- [ ] Handle void specialization: `Promise<void>` with `setValue()` (no arg)

---

## Future\<T\>

Wraps `std::future<T>`. Returned by `Promise<T>::future()` and `ThreadPool::submit()`.

**Files:**
- [ ] `include/promeki/core/future.h`
- [ ] `tests/future.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] Wrap `std::future<T>` as private member
- [ ] `isReady()` — non-blocking check (polls with `wait_for(0)`)
- [ ] `result(unsigned int timeoutMs = 0)` — returns `std::pair<T, Error>`. 0 means wait indefinitely.
- [ ] `waitForFinished()` — blocks until ready
- [ ] `Error waitForFinished(unsigned int timeoutMs)` — blocks with timeout. Returns `Error::Ok` or `Error::Timeout`.
- [ ] Move-only (non-copyable, movable)
- [ ] Handle void specialization: `Future<void>` where `result()` returns `Error`
- [ ] Doctest: basic set/get, timeout behavior, error propagation, ready check

---

## ThreadPool

General-purpose thread pool. Lives in core library, used by MediaPipeline and available for general use.

**Files:**
- [ ] `include/promeki/core/threadpool.h`
- [ ] `src/threadpool.cpp`
- [ ] `tests/threadpool.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] Private members: `List<Thread *>` or `std::vector<std::thread>`, task queue with `Mutex`/`WaitCondition`
- [ ] Constructor with optional thread count (default: `std::thread::hardware_concurrency()`)
- [ ] `submit(Callable)` — returns `Future<T>` (deduced from callable return type)
- [ ] `setThreadCount(int count)` — resize pool. Count 0 runs tasks inline on caller (WASM graceful degradation)
- [ ] `threadCount()` — current thread count
- [ ] `activeThreadCount()` — threads currently executing tasks
- [ ] `waitForDone()` — blocks until all submitted tasks complete
- [ ] `Error waitForDone(unsigned int timeoutMs)` — blocks with timeout. Returns `Error::Ok` or `Error::Timeout`.
- [ ] `clear()` — remove pending (not running) tasks from queue
- [ ] Destructor: signals shutdown, joins all threads
- [ ] Non-copyable, non-movable
- [ ] Thread-safe: all public methods safe to call from any thread
- [ ] Doctest: submit tasks, verify results via Future, thread count, waitForDone, inline mode (count 0)
