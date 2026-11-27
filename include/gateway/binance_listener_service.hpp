#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/erorrs.hpp"
#include "domain/types.hpp"

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

namespace ob::observer {

/// Represents the current lifecycle or connection state of a listener.
enum class ListenerStatus : std::uint8_t {
    Connected,
    Disconnected,
    Running,
    Stopped,
    Error
};

/// Describes a listener status change and an optional associated error.
struct ListenerStatusEvent {
    explicit constexpr ListenerStatusEvent(ListenerStatus s,
                                           const WsError* e = nullptr) noexcept
        : status(s), error(e) {}

    ListenerStatus status;
    const WsError* error = nullptr;

    /// Returns true when an associated error is present.
    [[nodiscard]] bool hasError() const noexcept { return error != nullptr; }
};

/// Interface for consumers of market-depth updates and listener status changes.
struct DepthUpdateObserver {
    virtual ~DepthUpdateObserver() = default;

    virtual void onDepthUpdate(const domain::DepthUpdate& update) = 0;
    virtual void statusUpdate(const ListenerStatusEvent& event) = 0;
};

/// Establishes and manages a Binance WebSocket connection.
class BinanceListenerService {
   public:
    /// Constructs a listener for a host and subscription target.
    BinanceListenerService(
        std::string host, std::string port,
        std::unordered_map<std::string, unsigned int> target);

    /// Constructs a listener for a host and explicit target path.
    BinanceListenerService(std::string host, std::string port,
                           std::string target_path);

    /// Initializes the listener.
    void init();
    /// Starts the receive loop.
    void snifer();
    /// Runs the I/O processing loop.
    void run();
    /// Stops the listener.
    void stop();

    /// Returns true while the WebSocket is open.
    bool isRunning() const { return ws_ && ws_->is_open(); }

    /// Registers an observer.
    void subscribe(DepthUpdateObserver* observer);
    /// Unregisters an observer.
    void unsubscribe(DepthUpdateObserver* observer);

   private:
    void notifyObservers(const domain::DepthUpdate& update);
    void notifyObservers(const ListenerStatusEvent& status);

    void updateStatus(ListenerStatus update);
    void updateStatus(ListenerStatus update, const WsError& error);

    std::string build_target_path() const;

    std::vector<DepthUpdateObserver*> observers_;

    std::string const host_;
    std::string const port_;
    std::unordered_map<std::string, unsigned int> target_;
    std::string target_path_;

    ListenerStatus status_{ListenerStatus::Stopped};

    net::io_context ioc_;
    ssl::context ctx_{ssl::context::tls_client};
    std::optional<websocket::stream<beast::ssl_stream<beast::tcp_stream>>> ws_;
};
};  // namespace ob::observer