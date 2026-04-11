# Random Ideas That Need Exploration

1. ~~All MediaIO should take a "Name" config option.~~ *Mostly landed with
   [benchmarking.md](benchmarking.md) Part C — `MediaConfig::Name` (default `"media<localId>"`)
   and `MediaConfig::Uuid` (default fresh `UUID::generate()`) are shipped alongside a process-local
   atomic `localId` counter. Every `MediaIO` instance has a stable identity triple queryable via
   `localId()` / `name()` / `uuid()`. Still pending: the backend half of the idea — individual
   backends that spawn their own threads outside the shared `MediaIO::pool()` should name those
   threads from `name()`. No backend currently does this; it's a small drive-by change per
   backend when someone gets annoyed enough at `top` / `htop` showing unnamed worker threads.*

