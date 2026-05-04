# Windows File implementation

**File:** `src/core/file.cpp:40`
**FIXME:** "The windows code here needs love."

The Windows `#ifdef` branch is a stub — `isOpen()` returns false, and
the rest of the Windows-specific File methods are likely incomplete or
missing.

- [ ] Implement Windows File backend using `CreateFile` /
  `ReadFile` / `WriteFile` HANDLE API.
- [ ] Test on Windows (or at minimum ensure it compiles with correct
  stubs).
- [ ] Natural time to fix: Phase 2 File → IODevice refactor.
