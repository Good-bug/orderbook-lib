#pragma once

// #include <algorithm>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <cctype>
// #include <format>
#include <glaze/glaze.hpp>

#include <stdexcept>
#include <string>
// #include <string_view>
#include <print>

#include "domain/types.hpp"
#include "protocol/binance_convert.hpp"
#include "protocol/binance_dto.hpp"


namespace ob::rest {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

class BinanceRestDepthClient {
   public:
    explicit BinanceRestDepthClient(std::string host = "data-api.binance.vision",
                                   std::string_view scheme = "https",
                                   unsigned short port = 443)
        : host_(std::move(host)), scheme_(scheme), port_(port) {}

    [[nodiscard]] domain::DepthSnapshot fetch_snapshot(
        const std::string& symbol, std::size_t limit = 100) const {
        if (symbol.empty()) {
            throw std::invalid_argument("symbol must not be empty");
        }

        if (limit == 0) {
            throw std::invalid_argument("limit must be greater than zero");
        }

        static constexpr std::string_view kPath = "/api/v3/depth";

        std::string normalized_symbol = symbol;
        std::ranges::transform(normalized_symbol,
                       normalized_symbol.begin(), [](unsigned char ch) {
                           return static_cast<char>(std::toupper(ch));
                       });

        net::io_context ioc;
        ssl::context ctx{ssl::context::tls_client};
        ctx.set_default_verify_paths();

        ssl::stream<tcp::socket> stream{ioc, ctx};
        tcp::resolver resolver{ioc};
        auto endpoints = resolver.resolve(host_, std::to_string(port_));
        net::connect(stream.next_layer(), endpoints);

        if (!SSL_set_tlsext_host_name(stream.native_handle(), host_.c_str())) {
            throw std::runtime_error("Failed to set SNI hostname for Binance REST client");
        }

        stream.handshake(ssl::stream_base::client);

        const std::string target = std::format(
            "{}?symbol={}&limit={}",
            kPath,
            normalized_symbol,
            limit);

        http::request<http::string_body> req{http::verb::get, target, 11};
        req.set(http::field::host, host_);
        req.set(http::field::accept, "application/json");
        req.set(http::field::user_agent, "feedhandler/0.1");

        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);

        if (res.result() != http::status::ok) {
            throw std::runtime_error(
                std::format("Binance REST depth request failed: {} {}",
                            static_cast<int>(res.result()),
                            res.reason()));
        }

        constexpr glz::opts opts{.error_on_unknown_keys = false};

        auto snapshot = dto::DepthSnapshotDto{};
        const auto parse_status = glz::read<opts>(snapshot, res.body());
        if (parse_status) {
            throw std::runtime_error(
                std::format("Failed to parse Binance depth snapshot: {}", glz::format_error(parse_status, res.body())));
        }

        auto domain_snapshot = convert::to_domain(snapshot, 8);

        if (!domain_snapshot) {
            // TODO: error
            // std::println("ERROR");
        }
        return *domain_snapshot;
    }

    [[nodiscard]] std::string host() const { return host_; }
    [[nodiscard]] unsigned short port() const { return port_; }

   private:
    std::string host_;
    std::string scheme_;
    unsigned short port_;
};

}  // namespace ob::rest
