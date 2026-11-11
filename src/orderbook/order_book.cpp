#include "orderbook/order_book.hpp"

#include <memory>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "rest/binance_rest_depth_client.hpp"

namespace ob::orderbook {

OrderBook::OrderBook() = default;

OrderBook::~OrderBook() = default;

void OrderBook::onDepthUpdate(const domain::DepthUpdate& update) {
    events_q.push(update);
}

void OrderBook::statusUpdate(const observer::ListenerStatus& status) {
    switch (status) {
        case observer::ListenerStatus::Running: {
            requestSnapshot();
            break;
        }

        case observer::ListenerStatus::Connected:
        case observer::ListenerStatus::Disconnected:
        case observer::ListenerStatus::Stopped:
        case observer::ListenerStatus::Error:
            break;
    }
}

void OrderBook::start() {
    status = OrderBookStatus::Initiating;
    _worker = std::jthread([this](const std::stop_token& st) { run(st); });
}

std::shared_ptr<const BookView> OrderBook::view() const { return view_.load(); }

void OrderBook::requestSnapshot() {
    std::thread([this] {
        rest::BinanceRestDepthClient restClient;
        events_q.push(restClient.fetch_snapshot("btcusdt", 100));
    }).detach();
}

void OrderBook::run(const std::stop_token& st) {
    std::vector<Event> e;
    while (events_q.drain(e, st)) {
        for (auto& ev : e) {
            std::visit([this](auto& event_item) { handle(event_item); }, ev);
        }
    }
}

void OrderBook::applyLevels(const domain::DepthSnapshot& snapshot) {
    domain::applyLevels(bids, snapshot.bids);
    domain::applyLevels(asks, snapshot.asks);
    last_update_id = snapshot.last_update_id;
}

void OrderBook::handle(const domain::DepthUpdate& update) {
    if (status == OrderBookStatus::Initiating) {
        _buffer.push(update);
        return;
    }

    if (update.first_update_id.value != last_update_id.value + 1) {
        handle(ResetEvent{});
        return;
    }

    applyLevels(update);
    publish();
}

void OrderBook::handle(const domain::DepthSnapshot& snapshot) {
    bids.clear();
    asks.clear();
    applyLevels(snapshot);

    while (!_buffer.empty() &&
           last_update_id >= _buffer.front().last_update_id) {
        _buffer.pop();
    }

    if (!_buffer.empty() &&
        last_update_id.value + 1 != _buffer.front().first_update_id.value) {
        handle(ResetEvent{});
        return;
    }

    while (!_buffer.empty()) {
        applyLevels(_buffer.front());
        _buffer.pop();
    }

    publish();
    status = OrderBookStatus::Running;
}

void OrderBook::handle(const ResetEvent&) {
    status = OrderBookStatus::Initiating;
    _buffer = {};
    requestSnapshot();
}

void OrderBook::publish(std::size_t depth) {
    auto v = std::make_shared<BookView>();
    v->last_update_id = last_update_id;
    for (auto it = bids.begin(); it != bids.end() && v->bids.size() < depth;
         ++it) {
        v->bids.push_back({.price = it->first, .qty = it->second});
    }
    for (auto it = asks.begin(); it != asks.end() && v->asks.size() < depth;
         ++it) {
        v->asks.push_back({.price = it->first, .qty = it->second});
    }
    view_.store(std::move(v));
}

}  // namespace ob::orderbook
