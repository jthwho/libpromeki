# DateTime number-word parsing

**File:** `src/core/datetime.cpp:112`
**FIXME:** "Need to use the String::parseNumberWords()"

The `std::istringstream` was replaced with `strtoll` as part of the
stream migration, but the FIXME still stands: the code should use
`String::parseNumberWords()` for natural-language number parsing
(e.g. "three days ago") instead of bare `strtoll`.

- [ ] Implement or verify `String::parseNumberWords()` exists.
- [ ] Replace `strtoll` token parsing with `String::parseNumberWords()`.
- [ ] Update tests.
