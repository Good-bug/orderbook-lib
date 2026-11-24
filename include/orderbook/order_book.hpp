#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "book_synchronizer.hpp"
#include "domain/types.hpp"
#include "gateway/binance_listener_service.hpp"
#include "orderbook/books/book_concept.hpp"

namespace ob::orderbook {

template <class T>
class EventQueue {
   public:
    void push(T event) {
        {
            std::scoped_lock lg(_m);
            events.push_back(std::move(event));
        }
        _cva.notify_one();
    }

    bool drain(std::vector<T>& out, const std::stop_token& st) {
        std::unique_lock lock(_m);
        _cva.wait(lock, st, [&] { return !events.empty(); });
        out.clear();
        std::swap(out, events);

        return !out.empty();
    }

   private:
    std::mutex _m;
    std::condition_variable_any _cva;
    std::vector<T> events;
};

struct StreamStarted {};  // a snapshot can be requested.
struct StreamLost {};     // Connection lost.

enum class OrderBookStatus : std::uint8_t {
    Running,
    Initiating,  // wait Snapshot && Buffering
    Stop
};

using Event = std::variant<domain::DepthUpdate, domain::DepthSnapshot,
                           StreamStarted, StreamLost>;

template <BookLike Book>
class OrderBook : public observer::DepthUpdateObserver {
   public:
    OrderBook();
    ~OrderBook() override;

    void onDepthUpdate(const domain::DepthUpdate& update) override;
    void statusUpdate(const observer::ListenerStatus& status) override;

    void start();

    [[nodiscard]] std::shared_ptr<const BookView> view() const;

   private:
    void requestSnapshot();
    void run(const std::stop_token& st);

    Action handle(const domain::DepthUpdate& update);
    Action handle(const domain::DepthSnapshot& snapshot);
    Action handle(const StreamStarted&);
    Action handle(const StreamLost&);

    void publish(std::size_t depth = 20);

    BookSynchronizer<Book> sync_;

    bool snapshot_inflight_{false};
    std::atomic<OrderBookStatus> status = OrderBookStatus::Stop;

    // TODO: upgrade to lock free
    std::atomic<std::shared_ptr<const BookView>> view_;

    std::mutex snapshot_m_;
    std::atomic<bool> snapshot_busy_{false};

    EventQueue<Event> events_q;
    std::jthread worker_;
    std::jthread snapshot_thread_;
};

}  // namespace ob::orderbook
