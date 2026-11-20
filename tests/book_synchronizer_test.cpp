#include "orderbook/book_synchronizer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "domain/types.hpp"
#include "orderbook/books/naive_book.hpp"
#include "quill/SimpleSetup.h"

namespace {

using ob::domain::DepthSnapshot;
using ob::domain::DepthUpdate;
using ob::domain::Level;
using ob::domain::Price;
using ob::domain::Qty;
using ob::domain::SeqId;
using ob::orderbook::Action;
using ob::orderbook::BookSynchronizer;
using ob::orderbook::NaiveBook;
using Event = std::variant<DepthUpdate, DepthSnapshot>;

struct Step {
    Event event;
    Action expected_action;
};

struct TestCase {
    std::vector<Step> steps;
    SeqId expected_last_update_id;
    std::vector<Level> expected_bids;
    std::vector<Level> expected_asks;
    const char* name;
};

Level level(std::int64_t price, std::int64_t qty) {
    return {.price = Price{price}, .qty = Qty{qty}};
}

DepthSnapshot snapshot(std::uint64_t last_update_id,
                      std::vector<Level> bids = {},
                      std::vector<Level> asks = {}) {
    return {
        .last_update_id = SeqId{last_update_id},
        .bids = std::move(bids),
        .asks = std::move(asks),
    };
}

DepthUpdate update(std::uint64_t first_update_id,
                   std::uint64_t last_update_id,
                   std::vector<Level> bids = {},
                   std::vector<Level> asks = {}) {
    DepthUpdate result;
    result.first_update_id = SeqId{first_update_id};
    result.last_update_id = SeqId{last_update_id};
    result.bids = std::move(bids);
    result.asks = std::move(asks);
    return result;
}

quill::Logger* test_logger() {
    static auto* logger = [] {
        auto* instance = quill::simple_logger("stdout");
        instance->set_log_level(quill::LogLevel::None);
        return instance;
    }();
    return logger;
}

std::vector<TestCase> make_cases() {
    return {
    {
        .steps = {
            {.event=update(99, 100, {level(90, 9)}), .expected_action=Action::None},
            {.event=update(101, 101, {level(100, 2)}), .expected_action=Action::None},
            {.event=update(102, 102, {}, {level(110, 3)}), .expected_action=Action::None},
            {.event=snapshot(100, {level(99, 1)}, {level(111, 1)}), .expected_action=Action::None},
        },
        .expected_last_update_id = SeqId{102},
        .expected_bids = {level(100, 2), level(99, 1)},
        .expected_asks = {level(110, 3), level(111, 1)},
        .name = "Buffer updates and replay after snapshot",
    },
    {
        .steps = {
            {.event=snapshot(10, {level(99, 1)}), .expected_action=Action::None},
            {.event=update(12, 12, {level(100, 1)}), .expected_action=Action::RequestSnapshot},
            {.event=update(11, 11, {level(98, 2)}), .expected_action=Action::None},
            {.event=snapshot(10, {level(99, 1)}), .expected_action=Action::None},
        },
        .expected_last_update_id = SeqId{11},
        .expected_bids = {level(99, 1), level(98, 2)},
        .expected_asks = {},
        .name = "Request snapshot after live sequence gap",
    },
    {
        .steps = {
            {.event=update(102, 102, {level(102, 1)}), .expected_action=Action::None},
            {.event=snapshot(100, {level(100, 1)}), .expected_action=Action::RequestSnapshot},
            {.event=update(101, 101, {level(101, 1)}), .expected_action=Action::None},
            {.event=snapshot(100, {level(100, 1)}), .expected_action=Action::None},
        },
        .expected_last_update_id = SeqId{101},
        .expected_bids = {level(101, 1), level(100, 1)},
        .expected_asks = {},
        .name = "Discard buffered updates after snapshot gap",
    },
    {
        .steps = {
            {.event=update(101, 101, {level(101, 1)}), .expected_action=Action::None},
            {.event=update(103, 103, {level(103, 1)}), .expected_action=Action::None},
            {.event=snapshot(100, {level(100, 1)}), .expected_action=Action::RequestSnapshot},
        },
        .expected_last_update_id = SeqId{101},
        .expected_bids = {level(101, 1), level(100, 1)},
        .expected_asks = {},
        .name = "Detect sequence gap during buffered replay",
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

TEST_CASE("BookSynchronizer processes updates and snapshots",
          "[book_synchronizer]") {
    for (const auto& test_case : make_cases()) {
        DYNAMIC_SECTION(test_case.name) {
            BookSynchronizer<NaiveBook> synchronizer{test_logger()};
            for (const auto& step : test_case.steps) {
                const auto action = std::visit(
                    [&synchronizer](const auto& event) {
                        using EventType = std::decay_t<decltype(event)>;
                        if constexpr (std::is_same_v<EventType, DepthUpdate>) {
                            return synchronizer.onUpdate(event);
                        } else {
                            return synchronizer.onSnapshot(event);
                        }
                    },
                    step.event);
                CHECK(action == step.expected_action);
            }

            const auto view = synchronizer.book().getBookView(100);
            CHECK(view->last_update_id ==
                  test_case.expected_last_update_id);
            check_levels(view->bids, test_case.expected_bids);
            check_levels(view->asks, test_case.expected_asks);
        }
    }
}
