#include <catch2/catch_test_macros.hpp>
#include <glaze/glaze.hpp>

#include "protocol/binance_convert.hpp"
#include "protocol/binance_dto.hpp"

TEST_CASE("fixed-point conversion preserves values larger than int64") {
    const auto parsed = ob::convert::parse_int128("12345678901234567890.12345678");

    REQUIRE(parsed.has_value());
    REQUIRE(*parsed > static_cast<ob::domain::FixedInt>(INT64_MAX));
    REQUIRE(ob::convert::format_fixed_int124(*parsed) ==
            "12345678901234567890.12345678");
}

TEST_CASE("fixed-point conversion rejects unsupported precision and overflow") {
    REQUIRE(ob::convert::parse_int128("0.000000001").error() ==
            ob::domain::ParseError::PrecisionLoss);
    REQUIRE(ob::convert::parse_int128("1701411834604692317316873037158.84105727")
                .has_value());
    REQUIRE(ob::convert::parse_int128("1701411834604692317316873037158.84105728")
                .error() == ob::domain::ParseError::Overflow);
    REQUIRE(ob::convert::parse_int128("1701411834604692317316873037159")
                .error() == ob::domain::ParseError::Overflow);
}

TEST_CASE("Binance websocket depth JSON converts levels to int128") {
    constexpr std::string_view json_view =
        R"({"e":"depthUpdate","E":1730000000000,"s":"BTCUSDT","U":10,"u":11,"b":[["12345678901234567890.12345678","0.00000001"]],"a":[["1.5","2"]]})";
    const std::string json{json_view};
    ob::dto::DepthUpdateDto wire_update;
    constexpr glz::opts opts{.error_on_unknown_keys = false};
    REQUIRE_FALSE(glz::read<opts>(wire_update, json));

    const auto update = ob::convert::to_domain(wire_update);
    REQUIRE(update.has_value());
    REQUIRE(update->bids.size() == 1);
    REQUIRE(update->bids[0].price.value ==
            *ob::convert::parse_int128("12345678901234567890.12345678"));
    REQUIRE(update->bids[0].qty.value == 1);
    REQUIRE(update->asks[0].price.value == 150'000'000);
    REQUIRE(update->asks[0].qty.value == 200'000'000);
}

// TEST_CASE("Binance REST snapshot JSON converts levels to int128") {
//     constexpr std::string_view json_view =
//         R"({"lastUpdateId":42,"bids":[["1000000000000.12345678","1"]],"asks":[["1000000000001","0.25"]]})";
//     const std::string json{json_view};
//     ob::dto::DepthSnapshotDto wire_snapshot;
//     constexpr glz::opts opts{.error_on_unknown_keys = false};
//     REQUIRE_FALSE(glz::read<opts>(wire_snapshot, json));

//     const auto snapshot = ob::convert::to_domain(wire_snapshot);
//     REQUIRE(snapshot.has_value());
//     REQUIRE(snapshot->last_update_id == 42);
//     REQUIRE(snapshot->bids[0].price.ticks ==
//             *ob::convert::parse_int128("1000000000000.12345678"));
//     REQUIRE(snapshot->asks[0].qty.ticks == 25'000'000);
// }
