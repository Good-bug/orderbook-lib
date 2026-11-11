#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <stop_token>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "domain/types.hpp"
#include "gateway/binance_listener_service.hpp"

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

struct ResetEvent {};

enum class OrderBookStatus : std::uint8_t {
    Running,
    Initiating,  // wait Snapshot && Buffering
    // Sync,
    Stop
};

struct BookView {
    std::vector<domain::Level> bids;  // Sorted from best price.
    std::vector<domain::Level> asks;
    domain::SeqId last_update_id{0};
    // InstrumentSpec spec;          // So consumers know the scale.
};

using Event =
    std::variant<domain::DepthUpdate, domain::DepthSnapshot, ResetEvent>;

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
    void applyLevels(const domain::DepthSnapshot& snapshot);

    void handle(const domain::DepthUpdate& update);
    void handle(const domain::DepthSnapshot& snapshot);
    void handle(const ResetEvent&);
    
    void publish(std::size_t depth = 20);

    std::atomic<OrderBookStatus> status = OrderBookStatus::Stop;
    std::queue<domain::DepthUpdate> _buffer;

    domain::SeqId last_update_id{0};

    std::map<domain::Price, domain::Qty, std::greater<>> bids;
    std::map<domain::Price, domain::Qty> asks;

    // TODO: upgrade to lock free
    std::atomic<std::shared_ptr<const BookView>> view_;

    EventQueue<Event> events_q;
    std::jthread _worker;
};

}  // namespace ob::orderbook
