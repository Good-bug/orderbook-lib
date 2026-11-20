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

The standalone build uses the repository's vcpkg manifest. Install CMake 3.25+,
Ninja (optional), a C++23 compiler (GCC 14+ recommended), and vcpkg, then set
`VCPKG_ROOT` to the vcpkg installation directory.

From this directory:

```bash
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

All unit-test source files are compiled into the `orderbook_tests` executable.
To build or run only that target:

```bash
cmake --build build --target orderbook_tests
./build/orderbook_tests
```

The tests cover Binance DTO conversion, `NaiveBook` level updates and views,
and `BookSynchronizer` buffering, snapshot replay, and sequence-gap handling.
Catch2 must be available through vcpkg for the test target to be generated.
