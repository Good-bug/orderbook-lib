#include "gateway/binance_listener_service.hpp"

#include <openssl/ssl.h>

#include <algorithm>
#include <format>
#include <glaze/glaze.hpp>
#include <iostream>
#include <stdexcept>
#include <utility>

#include "common/erorrs.hpp"
#include "protocol/binance_convert.hpp"
#include "protocol/binance_dto.hpp"

namespace ob::observer {
BinanceListenerService::BinanceListenerService(
    std::string host, std::string port,
    std::unordered_map<std::string, unsigned int> target)
    : host_(std::move(host)),
      port_(std::move(port)),
      target_(std::move(target)),
      ctx_(ssl::context::tls_client) {
    target_path_ = build_target_path();
}

BinanceListenerService::BinanceListenerService(std::string host,
                                               std::string port,
                                               std::string target_path)
    : host_(std::move(host)),
      port_(std::move(port)),
      target_path_(std::move(target_path)),
      ctx_(ssl::context::tls_client) {}

std::string BinanceListenerService::build_target_path() const {
    if (target_.empty()) {
        return "/ws/btcusdt@depth@100ms";
    }
    if (target_.size() == 1) {
        const auto& [symbol, depth] = *target_.begin();
        return std::format("/ws/{}@depth@{}ms", symbol, depth);
    }
    std::string path = "/stream?streams=";
    bool first = true;
    for (const auto& [symbol, depth] : target_) {
        if (!first) {
            path += '/';
        }
        path += std::format("{}@depth@{}ms", symbol, depth);
        first = false;
    }
    return path;
}

void BinanceListenerService::init() {
    ctx_.set_default_verify_paths();
    ctx_.set_verify_mode(ssl::verify_peer);

    // Reconstruct / initialize the WebSocket stream safely
    ws_.emplace(ioc_, ctx_);

    tcp::resolver resolver{ioc_};
    auto endpoints = resolver.resolve(host_, port_);
    beast::get_lowest_layer(*ws_).connect(endpoints);

    // TLS (SNI + hostname verification)
    if (!SSL_set_tlsext_host_name(ws_->next_layer().native_handle(),
                                  host_.c_str())) {
        throw beast::system_error{
            beast::error_code{static_cast<int>(::ERR_get_error()),
                              net::error::get_ssl_category()}};
    }
    ws_->next_layer().set_verify_callback(ssl::host_name_verification(host_));
    ws_->next_layer().handshake(ssl::stream_base::client);

    // WebSocket handshake
    const std::string path =
        target_path_.empty() ? build_target_path() : target_path_;
    ws_->handshake(host_ + ":" + port_, path);

    updateStatus(ListenerStatus::Connected);
}

void BinanceListenerService::snifer() {
    if (!ws_) {
        throw std::runtime_error(
            "WebSocket is not initialized. Call init() first.");
    }

    constexpr glz::opts opts{.error_on_unknown_keys = false};
    beast::flat_buffer buf;

    updateStatus(ListenerStatus::Running);

    for (;;) {
        beast::error_code ec;
        ws_->read(buf, ec);
        if (ec) {
            if (ec == websocket::error::closed) {
                break;
            }
            break;
        }

        auto msg = beast::buffers_to_string(buf.data());
        buf.consume(buf.size());

        auto update = ob::dto::DepthUpdateDto{};
        if (const auto parse_ec = glz::read<opts>(update, msg)) {
            std::cerr << "JSON parse error: "
                      << glz::format_error(parse_ec, msg) << '\n';
            continue;
        }

        auto update_response = ob::convert::to_domain(update);

        if (!update_response) {
            return;
        }

        notifyObservers(*update_response);
    }
}

void BinanceListenerService::run() {
    init();
    snifer();
}

void BinanceListenerService::stop() {
    if (ws_ && ws_->is_open()) {
        beast::error_code ec;
        ws_->close(websocket::close_code::normal, ec);
    }
}

void BinanceListenerService::subscribe(DepthUpdateObserver* observer) {
    if (observer) {
        observers_.push_back(observer);
    }
}

void BinanceListenerService::unsubscribe(DepthUpdateObserver* observer) {
    auto it = std::ranges::remove(observers_, observer).begin();
    observers_.erase(it, observers_.end());
}

void BinanceListenerService::notifyObservers(
    const domain::DepthUpdate& update) {
    for (auto* observer : observers_) {
        if (observer) {
            observer->onDepthUpdate(update);
        }
    }
}

void BinanceListenerService::notifyObservers(
    const ListenerStatusEvent& newStatus) {
    for (auto* observer : observers_) {
        if (observer) {
            observer->statusUpdate(newStatus);
        }
    }
}

void BinanceListenerService::updateStatus(ListenerStatus update) {
    status_ = update;
    notifyObservers(ListenerStatusEvent{status_});
}

void BinanceListenerService::updateStatus(ListenerStatus update,
                                          const WsError& error) {
    status_ = update;
    notifyObservers(ListenerStatusEvent{status_, &error});
}
};  // namespace ob::observer