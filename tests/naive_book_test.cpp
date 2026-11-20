#include "orderbook/books/naive_book.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "domain/types.hpp"

namespace {

using ob::domain::DepthUpdate;
using ob::domain::Level;
using ob::domain::Price;
using ob::domain::Qty;
using ob::domain::SeqId;
using ob::orderbook::NaiveBook;

struct TestCase {
    std::vector<DepthUpdate> updates;
    std::size_t depth;
    bool clean_after_updates;
    SeqId expected_last_update_id;
    std::vector<Level> expected_bids;
    std::vector<Level> expected_asks;
    const char* name;
};

Level level(std::int64_t price, std::int64_t qty) {
    return {.price = Price{price}, .qty = Qty{qty}};
}

DepthUpdate update(std::uint64_t last_update_id, std::vector<Level> bids,
                   std::vector<Level> asks) {
    DepthUpdate result;
    result.last_update_id = SeqId{last_update_id};
    result.bids = std::move(bids);
    result.asks = std::move(asks);
    return result;
}

std::vector<TestCase> make_cases() {
    return {
    {
        .updates = {update(42,
                           {level(100, 1), level(102, 2), level(101, 3)},
                           {level(104, 4), level(103, 5), level(105, 6)})},
        .depth = 2,
        .clean_after_updates = false,
        .expected_last_update_id = SeqId{42},
        .expected_bids = {level(102, 2), level(101, 3)},
        .expected_asks = {level(103, 5), level(104, 4)},
        .name = "Sort levels and limit depth",
    },
    {
        .updates =
            {
                update(10, {level(100, 1), level(99, 2)},
                       {level(101, 3), level(102, 4)}),
                update(11, {level(100, 7), level(99, 0), level(98, 8)},
                       {level(101, 0), level(102, 9)}),
            },
        .depth = 10,
        .clean_after_updates = false,
        .expected_last_update_id = SeqId{11},
        .expected_bids = {level(100, 7), level(98, 8)},
        .expected_asks = {level(102, 9)},
        .name = "Update levels and remove zero quantities",
    },
    {
        .updates = {update(5, {level(100, 1)}, {level(101, 1)})},
        .depth = 10,
        .clean_after_updates = true,
        .expected_last_update_id = SeqId{5},
        .expected_bids = {},
        .expected_asks = {},
        .name = "Clean book",
    },
    };
}

void check_levels(const std::vector<Level>& actual,
                  const std::vector<Level>& expected) {
    REQUIRE(actual.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CHECK(actual[i].price == expected[i].price);
        CHECK(actual[i].qty == expected[i].qty);
    }
}

}  // namespace

TEST_CASE("NaiveBook applies depth updates", "[naive_book]") {
    const auto& cases = make_cases();
    for (const auto& test_case : cases) {
        DYNAMIC_SECTION(test_case.name) {
            NaiveBook book;
            for (const auto& depth_update : test_case.updates) {
                book.apply(depth_update);
            }
            if (test_case.clean_after_updates) {
                book.clean();
            }

            const auto view = book.getBookView(test_case.depth);
            CHECK(view->last_update_id == test_case.expected_last_update_id);
            check_levels(view->bids, test_case.expected_bids);
            check_levels(view->asks, test_case.expected_asks);
        }
    }
}
