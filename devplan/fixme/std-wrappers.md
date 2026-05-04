# Replace direct std-library usage with library wrappers

Library classes should use the project's container/type wrappers
(`List`, `Map`, `Array`, `String`) instead of raw `std::` types.

## std::vector → `List<T>`

- `src/core/bufferediodevice.cpp:149,167,211,227,240` — multiple
  `std::vector<uint8_t>` used as temporary read/collect buffers.

## std::map → `Map<K,V>`

- `src/core/string.cpp:283` — `static const std::map<std::string,
  int64_t> numberWords` lookup table.
- `src/core/datetime.cpp:78` — `static const std::map<std::string,
  system_clock::duration> units` lookup table.

## std::array → `Array<T,N>`

- `include/promeki/macaddress.h:109` — `std::array<uint8_t, 6>` in
  constructor initializer.
- `include/promeki/musicalscale.h:45` — `using MembershipMask =
  std::array<int, 12>` public typedef.
- `include/promeki/util.h:116,127,141-144,154` — `std::array<T, 4>`
  in public template function signatures (`promekiCatmullRom`,
  `promekiBezier`, `promekiBicubic`, `promekiCubic`).
- `src/core/system.cpp:27` — `std::array<char, HOST_NAME_MAX>` local
  variable.

## Tasks

- [ ] Replace `std::vector` with `List<T>` in
  `src/core/bufferediodevice.cpp`.
- [ ] Replace `std::map` with `Map<K,V>` in `src/core/string.cpp` and
  `src/core/datetime.cpp`.
- [ ] Replace `std::array` with `Array<T,N>` in `macaddress.h` and
  `musicalscale.h`.
- [ ] Replace `std::array` with `Array<T,N>` in `util.h` template
  functions.
- [ ] Replace `std::array` with `Array<T,N>` in `src/core/system.cpp`.
- [ ] Verify all replacements compile and pass tests.
