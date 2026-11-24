# orderbook_lib

Core C++23 library for Binance market-data handling and order-book
maintenance. The library target is `orderbook::lib`.

## Components

- **`domain`** (`include/domain/`): Exchange-independent types such as
  `Price`, `Qty`, `Level`, `DepthUpdate`, and `DepthSnapshot`. Prices and
  quantities use signed 128-bit fixed-point values with a scale factor of
  `100,000,000`.
- **`protocol`** (`include/protocol/`): Binance JSON DTOs and converters that
  validate wire data before producing domain types.
- **`gateway`** (`include/gateway/`, `src/gateway/`): Binance WebSocket
  listener and observer notifications.
- **`rest`** (`include/rest/`): REST client for fetching depth snapshots.
- **`orderbook`** (`include/orderbook/`, `src/orderbook/`): `NaiveBook`
  maintains sorted bid/ask levels and provides depth-limited views.
  `BookSynchronizer` buffers stream updates while waiting for a snapshot,
  discards updates covered by the snapshot, replays contiguous updates, and
  requests a new snapshot when it detects a sequence gap.
  `OrderBook` connects synchronization to the listener and publishes views
  for consumers.
- **`writer`** (`include/writer/`): Asynchronous CSV writer for depth updates.

## Build and test

The standalone build uses the vcpkg manifest and CMake presets in this
directory. Install CMake 3.25+, Ninja, GCC 14, and vcpkg, then set `VCPKG_ROOT`
to the vcpkg installation directory.

From this directory, configure, build, and run tests with the Debug preset:

```bash
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug
```

All unit-test source files are compiled into the `orderbook_tests` executable.
To build or run only that target:

```bash
cmake --build --preset debug --target orderbook_tests --parallel
./build/debug/orderbook_tests
```

Catch2 test tags can be used to run a subset of the suite, for example:

```bash
./build/debug/orderbook_tests "[book_synchronizer]"
```

The `asan` preset enables AddressSanitizer and UndefinedBehaviorSanitizer, and
`release` configures an optimized build. Configure, build, and test either
preset by substituting its name for `debug`:

```bash
cmake --preset asan
cmake --build --preset asan --parallel
ctest --preset asan
```

The tests cover Binance DTO conversion, `NaiveBook` level updates and views,
and `BookSynchronizer` buffering, snapshot replay, and sequence-gap handling.
The `orderbook_tests` target is generated when Catch2 is available through
vcpkg.
