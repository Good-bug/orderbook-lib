#pragma once

#include <array>
#include <cstdint>
#include <glaze/glaze.hpp>
#include <optional>
#include <string>
#include <vector>

namespace ob::dto {

/**
 * @brief Represents a price/quantity pair encoded as a decimal string pair.
 *
 * Binance serializes order-book levels as ["price", "quantity"] JSON arrays,
 * using string values to preserve decimal precision.
 */
using LevelDto = std::array<std::string, 2>;

/**
 * @brief Subscription request payload sent to the Binance WebSocket stream.
 */
struct SubscribeRequestDto {
    std::string method{"SUBSCRIBE"};  ///< WebSocket subscription method.
    std::vector<std::string> params;  ///< Stream symbols to subscribe to.
    std::uint64_t id{1};               ///< Request identifier.
};

/**
 * @brief Acknowledgement returned by Binance for a successful subscription.
 */
struct SubscribeAckDto {
    std::optional<std::string> result;  ///< Optional acknowledgement payload.
    std::int64_t id{};                  ///< Subscription request identifier.
};

/**
 * @brief A depth update event received from the Binance order book stream.
 */
struct DepthUpdateDto {
    std::string event_type;          ///< Event type, typically "depthUpdate".
    std::uint64_t event_time{};      ///< Event timestamp in milliseconds.
    std::string symbol;              ///< Trading symbol.
    std::uint64_t first_update_id{}; ///< First update identifier in the event.
    std::uint64_t last_update_id{};  ///< Last update identifier in the event.
    std::vector<LevelDto> bids;      ///< Bids in the update payload.
    std::vector<LevelDto> asks;      ///< Asks in the update payload.
};

/**
 * @brief Snapshot of a Binance depth stream at a given update identifier.
 */
struct DepthSnapshotDto {
    std::uint64_t last_update_id{};  ///< Last update id for the snapshot.
    std::vector<LevelDto> bids;     ///< Current bid levels in the snapshot.
    std::vector<LevelDto> asks;     ///< Current ask levels in the snapshot.
};

}  // namespace ob::dto

/**
 * @brief Glaze serialization metadata for Binance depth update events.
 */
template <>
struct glz::meta<ob::dto::DepthUpdateDto> {
    using T = ob::dto::DepthUpdateDto;
    static constexpr auto value =
        object("e", &T::event_type, "E", &T::event_time, "s", &T::symbol, "U",
               &T::first_update_id, "u", &T::last_update_id, "b", &T::bids, "a",
               &T::asks);
};

/**
 * @brief Glaze serialization metadata for Binance depth snapshots.
 */
template <>
struct glz::meta<ob::dto::DepthSnapshotDto> {
    using T = ob::dto::DepthSnapshotDto;
    static constexpr auto value = object("lastUpdateId", &T::last_update_id,
                                         "bids", &T::bids, "asks", &T::asks);
};
