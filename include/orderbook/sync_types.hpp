#pragma once
#include <cstdint>
#include <vector>
#include "domain/types.hpp"

namespace ob::orderbook {
enum class State : std::uint8_t { WaitingSnapshot, Live };
enum class Action : std::uint8_t { None, RequestSnapshot };


struct BookView {
    std::vector<domain::Level> bids;  // Sorted from best price.
    std::vector<domain::Level> asks;
    domain::SeqId last_update_id{0};
};
}  // namespace ob::orderbook