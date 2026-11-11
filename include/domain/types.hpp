/**
 * @file types.hpp
 * @brief Domain types used by the feed handler.
 */

#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ob::domain {

/**
 * @brief Signed fixed-point integer used for prices and quantities.
 */
using FixedInt = __int128_t;

struct Int128Hash {
    std::size_t operator()(FixedInt val) const noexcept {
        auto low = static_cast<std::uint64_t>(val);
        auto high = static_cast<std::uint64_t>(val >> 64);

        // Fast bit mixing
        return low ^ (high + 0x9e3779b97f4a7c15ULL + (low << 6) + (low >> 2));
    }
};

/**
 * @brief Unsigned fixed-point integer used during calculations.
 */
using FixedUInt = __uint128_t;

struct UInt128Hash {
    std::size_t operator()(FixedUInt val) const noexcept {
        auto low = static_cast<std::uint64_t>(val);
        auto high = static_cast<std::uint64_t>(val >> 64);

        // Fast bit mixing
        return low ^ (high + 0x9e3779b97f4a7c15ULL + (low << 6) + (low >> 2));
    }
};

/**
 * @brief Number of decimal places used by fixed-point values.
 */
inline constexpr int kScale = 8;

/**
 * @brief Scale factor for converting fixed-point tick values to decimal values.
 */
inline constexpr FixedInt kScaleFactor = 100'000'000;

/**
 * @brief Represents a price in fixed-point tick units.
 *
 * @details The stored value in @c ticks is interpreted as a decimal price by
 * dividing it by @c kScaleFactor.
 */
struct Price {
    FixedInt value{};
    auto operator<=>(const Price&) const = default;
};

/**
 * @brief Represents a quantity in fixed-point tick units.
 */
struct Qty {
    FixedInt value{};
    auto operator<=>(const Qty&) const = default;

    /**
     * @return true if the quantity is exactly zero.
     */
    [[nodiscard]] constexpr bool is_zero() const noexcept { return value == 0; }
};

/**
 * @brief Sequence identifier used for ordering and validation.
 */
struct SeqId {
    std::uint64_t value{};
    auto operator<=>(const SeqId&) const = default;
};

/**
 * @brief A single price/quantity level in the order book.
 */
struct Level {
    Price price;
    Qty qty;
};

/**
 * @brief Full order book snapshot for a symbol.
 */
struct DepthSnapshot {
    SeqId last_update_id;  ///< Last applied sequence identifier.
    std::vector<Level> bids;
    std::vector<Level> asks;
};


/**
 * @brief Incremental order book update for a symbol.
 *
 * @details Contains the symbol, event time, sequence range, and the bid/ask
 * changes contained in a single update message.
 */
struct DepthUpdate : DepthSnapshot {
    std::string symbol;
    std::uint64_t event_time_ms{};
    SeqId first_update_id;  ///< First sequence identifier in the delta.
};

template <class Side>
void applyLevels(Side& side, const std::vector<Level>& levels) {
    for (const auto& l : levels) {
        if (l.qty.value == 0) {
            side.erase(l.price);  // Remove the level.
        } else {
            side[l.price] = l.qty;  // Set the absolute value, not emplace.
        }
    }
}

/**
 * @brief Error reasons generated while parsing feed messages.
 */
enum class ParseError : std::uint8_t {
    BadFormat,
    Overflow,
    PrecisionLoss,
    NonPositivePrice,
    BadSeq,
    WrongEvent,
};

/**
 * @brief Converts a @c ParseError to a short string label.
 *
 * @param e Error code to describe.
 * @return String representation of the error.
 */
constexpr std::string_view to_string(ParseError e) noexcept {
    switch (e) {
        case ParseError::BadFormat:
            return "BadFormat";
        case ParseError::Overflow:
            return "Overflow";
        case ParseError::PrecisionLoss:
            return "PrecisionLoss";
        case ParseError::NonPositivePrice:
            return "NonPositivePrice";
        case ParseError::BadSeq:
            return "BadSeq";
        case ParseError::WrongEvent:
            return "WrongEvent";
    }
    return "Unknown";
}

}  // namespace ob::domain