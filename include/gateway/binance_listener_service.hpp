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

#include "domain/types.hpp"

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

namespace ob::observer {

enum class ListenerStatus : std::uint8_t {
    Connected,
    Disconnected,
    Running,
    Stopped,
    Error
};
struct DepthUpdateObserver {
    virtual ~DepthUpdateObserver() = default;
    virtual void onDepthUpdate(const domain::DepthUpdate& update) = 0;

    virtual void statusUpdate(const ListenerStatus& status) = 0;
};

class BinanceListenerService {
   public:
    BinanceListenerService(
        std::string host, std::string port,
        std::unordered_map<std::string, unsigned int> target);

    BinanceListenerService(std::string host, std::string port,
                           std::string target_path);

    void init();
    void snifer();
    void run();
    void stop();

    bool isRunning() const { return ws && ws->is_open(); }

    void subscribe(DepthUpdateObserver* observer);
    void unsubscribe(DepthUpdateObserver* observer);

   private:
    void notifyObservers(const domain::DepthUpdate& update);
    void notifyObservers(const ListenerStatus& status);

    std::vector<DepthUpdateObserver*> observers_;
    std::string build_target_path() const;

    std::string const host;
    std::string const port;
    std::unordered_map<std::string, unsigned int> target;
    std::string target_path;

    // Order matters: ioc and ctx must outlive ws!
    net::io_context ioc;
    ssl::context ctx{ssl::context::tls_client};
    std::optional<websocket::stream<beast::ssl_stream<beast::tcp_stream>>> ws;
};
};  // namespace ob::observer