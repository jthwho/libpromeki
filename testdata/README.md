# Test Data

Binary fixtures used by the libpromeki unit tests. Every binary file placed
here is tracked via git LFS — see `.gitattributes` at the project root for
the current list of tracked extensions.

## Rules

1. **Keep it small.** Aim for the minimum file that still exercises the code
   path you're testing. A 16x16 one-frame clip is enough to validate a
   container parser; a 64-sample PCM buffer is enough to validate an audio
   reader. Do not commit multi-megabyte files without a good reason.

2. **Anything over a few MB needs a second opinion.** If you genuinely need
   a large fixture (real-world footage, long audio stream, high resolution
   source), raise it before committing — we may want to fetch it on demand
   from an external location rather than carrying it in the repo.

3. **Document what each fixture is for.** Add a one-line comment in the
   test file that references the fixture, noting the source, resolution,
   codec, and any quirks that make it interesting for the test.

4. **Do not regenerate fixtures in place.** If a fixture needs to change
   (new codec option, different resolution), add a new file with a different
   name rather than overwriting — other tests may rely on the existing
   bytes.

5. **LFS pointers commit cleanly.** If you add a new fixture extension,
   update `.gitattributes` in the same commit and run
   `git lfs track "*.ext"` if the pattern is new.

## Layout

- `quicktime/` — `.mov` and `.mp4` fixtures for the QuickTime reader/writer
  tests.
