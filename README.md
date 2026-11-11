# feedhandler_lib

Core C++23 library for Binance market data feed handling and order book maintenance.

## Components

- **`domain`** (`include/domain/`): Exchange-independent domain types (`Price`, `Qty`, `Level`, `DepthUpdate`, `DepthSnapshot`). Fixed-point 128-bit arithmetic (`kScaleFactor = 100,000,000`).
- **`protocol`** (`include/protocol/`): Binance JSON DTOs and strict converters to domain models.
- **`gateway`** (`include/gateway/`, `src/gateway/`): WebSocket listener service connecting to Binance market streams with observer pattern for subscribers.
- **`orderbook`** (`include/orderbook/`, `src/orderbook/`): Order book depth tracking and view generation.
- **`rest`** (`include/rest/`): REST client for fetching depth snapshots.
- **`writer`** (`include/writer/`): Asynchronous CSV writer for depth updates.

## Standalone Build

```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build
ctest --test-dir build
```
