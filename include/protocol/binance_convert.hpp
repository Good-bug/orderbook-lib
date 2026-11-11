#pragma once

#include <version>
#if !defined(__cpp_lib_expected) || __cpp_lib_expected < 202202L
#error \
    "std::expected is unavailable: build with C++23 and a supporting standard library"
#endif

#include <algorithm>
#include <expected>
#include <string>
#include <string_view>

#include "domain/types.hpp"
#include "protocol/binance_dto.hpp"

namespace ob::convert {

/**
 * @brief Result type used by conversion helpers.
 * @tparam T The successful domain value type.
 */
template <class T>
using Result = std::expected<T, domain::ParseError>;

namespace detail {

/**
 * @brief Checks whether a character is an ASCII decimal digit.
 * @param c Candidate character.
 * @return True if c is in the range ['0'..'9'].
 */
constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

/**
 * @brief Returns the maximum representable value for the fixed-point type.
 * @return Largest supported value for domain::FixedInt.
 */
constexpr domain::FixedInt max_fixed_value() noexcept {
    return static_cast<domain::FixedInt>(
        (static_cast<domain::FixedUInt>(1) << 127) - 1);
}

}  // namespace detail

/**
 * @brief Parses a Binance fixed-point decimal string into the domain value
 * type.
 *
 * The incoming text uses a decimal representation with up to @c domain::kScale
 * fractional digits. Values are validated for format, overflow, and precision
 * loss.
 *
 * @param value Input string such as "123.45" or "0.00000001".
 * @return The parsed fixed-point value or a parse error.
 */

[[nodiscard]] constexpr Result<domain::FixedInt> parse_int128(
    std::string_view value) noexcept {
    constexpr domain::FixedInt kMax = detail::max_fixed_value();
    // TODO: consider using char_traits to validate the string more efficiently,
    // but keep the error reporting strict and consistent with the current
    // implementation.
    // TODO: add support for negative values if needed, currently only positive
    // values are supported.
    // TODO: add suport of custom point precision, currently only 8 decimal
    // places are supported.
    constexpr domain::FixedInt kMaxIntegerPart = kMax / domain::kScaleFactor;

    // Parse the integer part first. Reject any value that exceeds the maximum
    // whole-number range, while keeping the decimal format validation strict.
    std::size_t i = 0;
    domain::FixedInt int_part = 0;
    std::size_t int_digits = 0;
    for (; i < value.size() && detail::is_digit(value[i]); ++i, ++int_digits) {
        const domain::FixedInt digit = value[i] - '0';
        if (int_part > (kMaxIntegerPart - digit) / 10) {
            return std::unexpected(domain::ParseError::Overflow);
        }
        int_part = int_part * 10 + digit;
    }
    if (int_digits == 0) {
        return std::unexpected(domain::ParseError::BadFormat);
    }

    // Parse the optional fractional portion, but only retain up to the
    // configured scale. Any extra non-zero digits beyond the scale are treated
    // as precision loss.
    domain::FixedInt fractional_part = 0;
    int fractional_digits = 0;
    if (i < value.size() && value[i] == '.') {
        ++i;
        const std::size_t fraction_start = i;
        for (; i < value.size() && detail::is_digit(value[i]); ++i) {
            const domain::FixedInt digit = value[i] - '0';
            if (fractional_digits < domain::kScale) {
                fractional_part = fractional_part * 10 + digit;
                ++fractional_digits;
            } else if (digit != 0) {
                return std::unexpected(domain::ParseError::PrecisionLoss);
            }
        }
        if (i == fraction_start) {
            return std::unexpected(domain::ParseError::BadFormat);
        }
    }

    // Anything trailing after the integer/fraction parsing is invalid.
    if (i != value.size()) {
        return std::unexpected(domain::ParseError::BadFormat);
    }

    // Align the fractional accumulator to the fixed-point scale and ensure the
    // final combined value does not overflow the allowed range.
    for (; fractional_digits < domain::kScale; ++fractional_digits) {
        fractional_part *= 10;
    }
    if (int_part > (kMax - fractional_part) / domain::kScaleFactor) {
        return std::unexpected(domain::ParseError::Overflow);
    }
    return int_part * domain::kScaleFactor + fractional_part;
}

inline Result<domain::FixedInt> parse_fixed(std::string_view s,
                                           std::uint8_t decimals) {
    if (decimals != domain::kScale) {
        return std::unexpected(domain::ParseError::PrecisionLoss);
    }
    return parse_int128(s);
}

inline std::string format_fixed(domain::FixedInt v, std::uint8_t decimals) {
    (void)decimals;
    const bool negative = v < 0;
    const domain::FixedUInt magnitude =
        negative ? static_cast<domain::FixedUInt>(-(v + 1)) + 1
                 : static_cast<domain::FixedUInt>(v);
    auto integer_part =
        magnitude / static_cast<domain::FixedUInt>(domain::kScaleFactor);
    auto fractional_part =
        magnitude % static_cast<domain::FixedUInt>(domain::kScaleFactor);

    std::string integer_digits;
    do {
        integer_digits.push_back(static_cast<char>('0' + integer_part % 10));
        integer_part /= 10;
    } while (integer_part != 0);
    std::ranges::reverse(integer_digits);

    if (fractional_part == 0) {
        return (negative ? "-" : "") + integer_digits;
    }

    std::string fractional_digits(domain::kScale, '0');
    for (int i = domain::kScale - 1; i >= 0; --i) {
        fractional_digits[i] = static_cast<char>('0' + fractional_part % 10);
        fractional_part /= 10;
    }
    while (fractional_digits.back() == '0') {
        fractional_digits.pop_back();
    }
    return (negative ? "-" : "") + integer_digits + "." + fractional_digits;
}

static_assert(parse_int128("0").value() == 0);
static_assert(parse_int128("100").value() == 100 * domain::kScaleFactor);
static_assert(parse_int128("0.00000001").value() == 1);
static_assert(parse_int128("1.50").value() == 150'000'000);
static_assert(parse_int128("0.123456780000").value() == 12'345'678);
static_assert(parse_int128("12345678901234567890.12345678").value() >
              static_cast<domain::FixedInt>(INT64_MAX));
static_assert(parse_int128("1701411834604692317316873037158.84105727")
                  .value() == detail::max_fixed_value());
static_assert(parse_int128("1701411834604692317316873037158.84105728")
                  .error() == domain::ParseError::Overflow);
static_assert(parse_int128("1701411834604692317316873037159").error() ==
              domain::ParseError::Overflow);
static_assert(parse_int128("0.000000001").error() ==
              domain::ParseError::PrecisionLoss);
static_assert(parse_int128("").error() == domain::ParseError::BadFormat);
static_assert(parse_int128("1.").error() == domain::ParseError::BadFormat);
static_assert(parse_int128(".5").error() == domain::ParseError::BadFormat);
static_assert(parse_int128("-1").error() == domain::ParseError::BadFormat);
static_assert(parse_int128("1e-8").error() == domain::ParseError::BadFormat);

/**----------------------------------------TMP------------------------------------------
 */

/**
 * @brief Converts a Binance order-book level into the internal domain type.
 * @param wire_level Raw level payload from the exchange.
 * @return Parsed price and quantity, or a conversion error.
 */
[[nodiscard]] inline Result<domain::Level> to_domain(
    const dto::LevelDto& wire_level, std::uint8_t decimals) noexcept {
    const auto price = parse_fixed(wire_level[0], decimals);
    if (!price) {
        return std::unexpected(price.error());
    }
    const auto quantity = parse_fixed(wire_level[1], decimals);
    if (!quantity) {
        return std::unexpected(quantity.error());
    }

    if (*price <= 0) {
        return std::unexpected(domain::ParseError::NonPositivePrice);
    }
    return domain::Level{.price = domain::Price{*price},
                         .qty = domain::Qty{*quantity}};
}

/**
 * @brief Converts a Binance depth update message into the domain delta model.
 * @param wire_update Raw websocket depth update payload.
 * @return Parsed update or a validation/conversion error.
 */
[[nodiscard]] inline Result<domain::DepthUpdate> to_domain(
    const dto::DepthUpdateDto& wire_update, std::uint8_t decimals = 8) {
    if (wire_update.event_type != "depthUpdate") {
        return std::unexpected(domain::ParseError::WrongEvent);
    }
    if (wire_update.first_update_id <= 0 ||
        wire_update.last_update_id < wire_update.first_update_id) {
        return std::unexpected(domain::ParseError::BadSeq);
    }

    domain::DepthUpdate update;
    update.symbol = wire_update.symbol;
    update.event_time_ms = wire_update.event_time;
    update.first_update_id = domain::SeqId{wire_update.first_update_id};
    update.last_update_id = domain::SeqId{wire_update.last_update_id};
    update.bids.reserve(wire_update.bids.size());
    update.asks.reserve(wire_update.asks.size());

    for (const auto& level : wire_update.bids) {
        auto converted = to_domain(level, decimals);
        if (!converted) {
            return std::unexpected(converted.error());
        }
        update.bids.push_back(*converted);
    }
    for (const auto& level : wire_update.asks) {
        auto converted = to_domain(level, decimals);
        if (!converted) {
            return std::unexpected(converted.error());
        }
        update.asks.push_back(*converted);
    }
    return update;
}

/**
 * @brief Converts a Binance depth snapshot into the domain snapshot model.
 * @param wire_snapshot Raw snapshot payload from the exchange.
 * @return Parsed snapshot or validation/conversion error.
 */
[[nodiscard]] inline Result<domain::DepthSnapshot> to_domain(
    const dto::DepthSnapshotDto& wire_snapshot, std::uint8_t decimals) {
    domain::DepthSnapshot snapshot;
    snapshot.last_update_id = domain::SeqId{wire_snapshot.last_update_id};
    snapshot.bids.reserve(wire_snapshot.bids.size());
    snapshot.asks.reserve(wire_snapshot.asks.size());

    for (const auto& level : wire_snapshot.bids) {
        auto converted = to_domain(level, decimals);
        if (!converted) {
            return std::unexpected(converted.error());
        }
        snapshot.bids.push_back(*converted);
    }
    for (const auto& level : wire_snapshot.asks) {
        auto converted = to_domain(level, decimals);
        if (!converted) {
            return std::unexpected(converted.error());
        }
        snapshot.asks.push_back(*converted);
    }
    return snapshot;
}

/**
 * @brief Serializes a fixed-point domain value back to a decimal string.
 * @param value Internal fixed-point number.
 * @return Canonical decimal representation without trailing zero fractional
 * digits.
 */
[[nodiscard]] inline std::string format_fixed_int124(domain::FixedInt value) {
    const bool negative = value < 0;
    const domain::FixedUInt magnitude =
        negative ? static_cast<domain::FixedUInt>(-(value + 1)) + 1
                 : static_cast<domain::FixedUInt>(value);
    auto integer_part =
        magnitude / static_cast<domain::FixedUInt>(domain::kScaleFactor);
    auto fractional_part =
        magnitude % static_cast<domain::FixedUInt>(domain::kScaleFactor);

    std::string integer_digits;
    do {
        integer_digits.push_back(static_cast<char>('0' + integer_part % 10));
        integer_part /= 10;
    } while (integer_part != 0);
    std::ranges::reverse(integer_digits);

    if (fractional_part == 0) {
        return (negative ? "-" : "") + integer_digits;
    }

    std::string fractional_digits(domain::kScale, '0');
    for (int i = domain::kScale - 1; i >= 0; --i) {
        fractional_digits[i] = static_cast<char>('0' + fractional_part % 10);
        fractional_part /= 10;
    }
    while (fractional_digits.back() == '0') {
        fractional_digits.pop_back();
    }
    return (negative ? "-" : "") + integer_digits + "." + fractional_digits;
}

}  // namespace ob::convert
