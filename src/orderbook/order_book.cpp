#include "orderbook/order_book.hpp"

#include <memory>
#include <thread>
#include <variant>
#include <vector>

#include "quill/LogMacros.h"
#include "quill/SimpleSetup.h"
#include "orderbook/books/naive_book.hpp"
#include "rest/binance_rest_depth_client.hpp"

namespace ob::orderbook {
namespace {

quill::Logger* orderbookLogger() {
    static auto* logger = quill::simple_logger("orderbook.log");
    logger->set_log_level(quill::LogLevel::Debug);
    return logger;
}

}  // namespace

template <class T>
OrderBook<T>::OrderBook() : sync_(orderbookLogger()) {
    LOG_INFO(orderbookLogger(), "Order book initialized");
}

template <class T>
OrderBook<T>::~OrderBook() = default;

template <class T>
void OrderBook<T>::onDepthUpdate(const domain::DepthUpdate& update) {
    events_q.push(update);
}

template <class T>
void OrderBook<T>::statusUpdate(const observer::ListenerStatus& status) {
    switch (status) {
        case observer::ListenerStatus::Running: {
            LOG_INFO(orderbookLogger(), "Binance listener is running");
            requestSnapshot();
            break;
        }

        case observer::ListenerStatus::Connected:
            LOG_INFO(orderbookLogger(), "Binance listener connected");
            break;
        case observer::ListenerStatus::Disconnected:
            LOG_WARNING(orderbookLogger(), "Binance listener disconnected");
            break;
        case observer::ListenerStatus::Stopped:
            LOG_INFO(orderbookLogger(), "Binance listener stopped");
            break;
        case observer::ListenerStatus::Error:
            LOG_ERROR(orderbookLogger(), "Binance listener reported an error");
            break;
    }
}

template <class T>
void OrderBook<T>::start() {
    LOG_INFO(orderbookLogger(), "Starting order book");
    status = OrderBookStatus::Initiating;
    requestSnapshot();
    _worker = std::jthread([this](const std::stop_token& st) { run(st); });
}

template <class T>
std::shared_ptr<const BookView> OrderBook<T>::view() const {
    return view_.load();
}

template <class T>
void OrderBook<T>::requestSnapshot() {
    LOG_INFO(orderbookLogger(), "Requesting Binance depth snapshot for btcusdt");
    std::thread([this] {
        rest::BinanceRestDepthClient restClient;
        events_q.push(restClient.fetch_snapshot("btcusdt", 100));
    }).detach();
}

template <class T>
void OrderBook<T>::run(const std::stop_token& st) {
    std::vector<Event> e;
    while (events_q.drain(e, st)) {
        for (auto& ev : e) {
            Action a = std::visit(
                [this](auto& event_item) { return this->handle(event_item); }, ev);

            if (a == Action::RequestSnapshot) {
                requestSnapshot();
            }
            publish();
        }
    }
}

template <class T>
Action OrderBook<T>::handle(const domain::DepthUpdate& update) {
    return sync_.onUpdate(update);
}

template <class T>
Action OrderBook<T>::handle(const domain::DepthSnapshot& snapshot) {
    const auto action = sync_.onSnapshot(snapshot);
    if (action == Action::None) {
        LOG_INFO(orderbookLogger(), "Order book synchronized at update {}",
                 snapshot.last_update_id.value);
    }
    return action;
}

template <class T>
void OrderBook<T>::publish(std::size_t depth) {
    view_.store(sync_.book().getBookView(depth));
}

}  // namespace ob::orderbook

template class ob::orderbook::OrderBook<ob::orderbook::NaiveBook>;
