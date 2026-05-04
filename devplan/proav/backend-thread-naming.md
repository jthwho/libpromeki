# MediaIO backends: name spawned threads from `name()`

Individual MediaIO backends that spawn their own threads outside the
shared `MediaIO::pool()` should name those threads using the stage's
`name()` so `top` / `htop` / `gdb` show something useful.

No backend currently does this; it's a small drive-by change per
backend whenever someone gets annoyed enough at unnamed worker
threads in a profiler.

(This is the leftover from the larger "MediaIO takes a `Name`
config option" work that otherwise landed via `MediaConfig::Name` +
`MediaConfig::Uuid` in the benchmark/identifier work.)

## Tasks

- [ ] Audit which backends spawn private threads (`RtpMediaIO`
  send/receive workers, `V4L2MediaIO`, others as discovered).
- [ ] Each spawn site sets the new thread's name from
  `parent->name()` plus a role suffix (e.g. `rtp0/tx-video`,
  `v4l2/capture`).
- [ ] Naming uses `Thread::setName()` (or
  `pthread_setname_np` directly on POSIX where the thread is raw).
