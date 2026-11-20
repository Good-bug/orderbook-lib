#pragma once
#include <map>
#include <memory>

#include "domain/types.hpp"
#include "../sync_types.hpp"

namespace ob::orderbook {

class NaiveBook {
   public:
    void apply(const domain::DepthSnapshot& snapshot) {
        domain::applyLevels(bids, snapshot.bids);
        domain::applyLevels(asks, snapshot.asks);
        last_update_id_ = snapshot.last_update_id;
    };

    void clean() {
        bids.clear();
        asks.clear();
    };

    [[nodiscard]] constexpr domain::SeqId lastUpdateId() const {
        return last_update_id_;
    }

    [[nodiscard]] std::shared_ptr<BookView> getBookView(std::size_t depth) const {
        auto v = std::make_shared<BookView>();
        v->last_update_id =last_update_id_;
        for (auto it = bids.begin(); it != bids.end() && v->bids.size() < depth;
             ++it) {
            v->bids.push_back({.price = it->first, .qty = it->second});
        }
        for (auto it = asks.begin(); it != asks.end() && v->asks.size() < depth;
             ++it) {
            v->asks.push_back({.price = it->first, .qty = it->second});
        }

        return v;
    }

   private:
    domain::SeqId last_update_id_{0};
    std::map<domain::Price, domain::Qty, std::greater<>> bids;
    std::map<domain::Price, domain::Qty> asks;
};
}  // namespace ob::orderbook