#include "orderbook/order_book.hpp"

#include <memory>
#include <thread>
#include <variant>
#include <vector>

#include "orderbook/books/naive_book.hpp"
#include "quill/LogMacros.h"
#include "quill/SimpleSetup.h"
#include "rest/binance_rest_depth_client.hpp"

namespace ob::orderbook {
namespace {

quill::Logger* orderbookLogger() {
    static auto* logger = quill::simple_logger("orderbook.log");
    logger->set_log_level(quill::LogLevel::Debug);
    return logger;
}

}  // namespace

template <BookLike Book>
OrderBook<Book>::OrderBook() : sync_(orderbookLogger()) {
    LOG_INFO(orderbookLogger(), "Order book initialized");
}

template <BookLike Book>
OrderBook<Book>::~OrderBook() = default;

template <BookLike Book>
void OrderBook<Book>::onDepthUpdate(const domain::DepthUpdate& update) {
    events_q.push(update);
}

template <BookLike Book>
void OrderBook<Book>::statusUpdate(const observer::ListenerStatusEvent& event) {
    switch (event.status) {
        case observer::ListenerStatus::Running: {
            LOG_INFO(orderbookLogger(), "Binance listener is running");
            events_q.push(StreamStarted{});
            break;
        }
        case observer::ListenerStatus::Connected:
            LOG_INFO(orderbookLogger(), "Binance listener connected");
            break;
        case observer::ListenerStatus::Disconnected:
            LOG_WARNING(orderbookLogger(), "Binance listener disconnected");
            events_q.push(StreamLost{});
            break;
        case observer::ListenerStatus::Stopped:
            LOG_INFO(orderbookLogger(), "Binance listener stopped");
            break;
        case observer::ListenerStatus::Error:
            LOG_ERROR(orderbookLogger(), "Binance listener reported an error");
            break;
    }
}

template <BookLike Book>
void OrderBook<Book>::start() {
    LOG_INFO(orderbookLogger(), "Starting order book");
    status = OrderBookStatus::Initiating;
    worker_ = std::jthread([this](const std::stop_token& st) { run(st); });
}

template <BookLike Book>
std::shared_ptr<const BookView> OrderBook<Book>::view() const {
    return view_.load();
}

template <BookLike Book>
void OrderBook<Book>::requestSnapshot() {
    LOG_INFO(orderbookLogger(),
             "Requesting Binance depth snapshot for btcusdt");
    if (snapshot_inflight_) {
        return;
    }
    snapshot_inflight_ = true;
    snapshot_thread_ = std::jthread(
        [this](const std::stop_token& st) {  // Join the previous thread; it is
                                             // already shutting down.
            rest::BinanceRestDepthClient client;
            for (auto delay = std::chrono::milliseconds{200};
                 !st.stop_requested();
                 delay = std::min(delay * 2, std::chrono::milliseconds{5000})) {
                try {
                    auto snap = client.fetch_snapshot("btcusdt", 100);
                    if (!st.stop_requested()) {
                        events_q.push(std::move(snap));
                    }
                    return;
                } catch (const std::exception& e) {
                    LOG_WARNING(orderbookLogger(), "snapshot failed: {}",
                                e.what());
                }
                std::mutex m;
                std::condition_variable_any cv;
                std::unique_lock lk(m);
                cv.wait_for(lk, st, delay, [] {
                    return false;
                });  // Backoff; interrupted by the stop token.
            }
        });
}

template <BookLike Book>
void OrderBook<Book>::run(const std::stop_token& st) {
    std::vector<Event> e;
    while (events_q.drain(e, st)) {
        for (auto& ev : e) {
            Action a = std::visit(
                [this](auto& event_item) { return this->handle(event_item); },
                ev);

            if (a == Action::RequestSnapshot) {
                requestSnapshot();
            }
        }
        publish();
    }
}

template <BookLike Book>
Action OrderBook<Book>::handle(const domain::DepthUpdate& update) {
    return sync_.onUpdate(update);
}

template <BookLike Book>
Action OrderBook<Book>::handle(const domain::DepthSnapshot& snapshot) {
    snapshot_inflight_ = false;
    const auto action = sync_.onSnapshot(snapshot);
    if (action == Action::None) {
        LOG_INFO(orderbookLogger(), "Order book synchronized at update {}",
                 snapshot.last_update_id.value);
    }
    return action;
}

template <BookLike T>
Action OrderBook<T>::handle(const StreamStarted&) {
    return sync_.onStreamStarted();
}

template <BookLike T>
Action OrderBook<T>::handle(const StreamLost&) {
    return sync_.onStreamLost();
}

template <BookLike Book>
void OrderBook<Book>::publish(std::size_t depth) {
    view_.store(sync_.book().getBookView(depth));
}

}  // namespace ob::orderbook

template class ob::orderbook::OrderBook<ob::orderbook::NaiveBook>;
