#pragma once
#include <format>
#include <string_view>
#include <utility>

/// Defines the interface required by fixed-size, trivially copyable error
/// types.
template <typename T>
concept FastError = std::is_trivially_copyable_v<T> && requires(T e) {
    { e.system() } -> std::same_as<std::string_view>;
    { e.code() } -> std::convertible_to<int>;
    { e.description() } -> std::same_as<std::string_view>;
    { e.details() } -> std::same_as<std::string_view>;
};

/// Stores a small string without heap allocation.
/// @tparam Capacity Maximum number of characters, including the null
/// terminator.
template <std::size_t Capacity = 128>
struct FixedString {
    /// Fixed-size character buffer.
    std::array<char, Capacity> buf{};
    /// Number of characters currently stored in @ref buf.
    uint8_t len = 0;

    /// Constructs an empty fixed string.
    constexpr FixedString() noexcept = default;

    /// Constructs a fixed string from a string view.
    /// @param sv Source string; characters beyond Capacity - 1 are discarded.
    constexpr FixedString(std::string_view sv) noexcept { assign(sv); }

    /// Assigns a value to the fixed string.
    /// @param sv Source string; characters beyond Capacity - 1 are discarded.
    constexpr void assign(std::string_view sv) noexcept {
        const std::size_t n = std::min(sv.size(), Capacity - 1);
        if (n > 0) {
            std::copy_n(sv.begin(), n, buf.data());
        }
        buf[n] =
            '\0';  ///< Keep a null terminator for optional C-API compatibility.
        len = static_cast<uint8_t>(n);
    }

    /// Returns a view of the stored characters.
    /// @return String view without the null terminator.
    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return {buf.data(), len};
    }
};

/// Stores an optional fixed-size error description.
/// @tparam MsgCapacity Maximum number of characters in the description.
template <std::size_t MsgCapacity = 0>
struct ErrorPayload {
    /// Fixed-size storage for the error details.
    FixedString<MsgCapacity> msg{};
    /// Constructs an empty error payload.
    constexpr ErrorPayload() noexcept = default;
    /// Constructs an error payload from a string view.
    /// @param sv Error details.
    constexpr ErrorPayload(std::string_view sv) noexcept : msg(sv) {}
    /// Returns the error details.
    /// @return Stored error details.
    [[nodiscard]] constexpr std::string_view details() const noexcept {
        return msg.view();
    }
};

/// Specializes @ref ErrorPayload for errors that do not contain text.
template <>
struct ErrorPayload<0> {
    /// Constructs an empty error payload.
    constexpr ErrorPayload() noexcept = default;
    /// Constructs an empty payload from a discarded string view.
    constexpr ErrorPayload(std::string_view) noexcept {}
    /// Returns no error details.
    /// @return An empty string view.
    [[nodiscard]] constexpr std::string_view details() const noexcept {
        return {};
    }
};

/// Provides consistent std::format formatting for @ref FastError values.
/// @tparam E Trivially copyable error type satisfying @ref FastError.
template <FastError E>
struct BaseErrorFormatter : std::formatter<std::string_view> {
    /// Formats an error as system, code, description, and optional details.
    /// @param err Error value to format.
    /// @param ctx Formatting context.
    /// @return Iterator positioned after the formatted output.
    auto format(const E& err, std::format_context& ctx) const {
        if (err.details().empty()) {
            return std::format_to(ctx.out(), "[{}:{}] {}", err.system(),
                                  err.code(), err.description());
        }
        return std::format_to(ctx.out(), "[{}:{}] {} — {}", err.system(),
                              err.code(), err.description(), err.details());
    }
};

/// Enumerates protocol and application-level WebSocket close codes.
enum class ListenerCode : std::uint16_t {
    /** @name Standard protocol codes (RFC 6455)
     *  @{
     */
    /// The connection completed successfully.
    NormalClosure = 1000,
    /// An endpoint is leaving because its server or client is going away.
    GoingAway = 1001,
    /// An endpoint detected a protocol violation.
    ProtocolError = 1002,
    /// The peer received data in a format it cannot accept.
    UnsupportedData = 1003,
    /// A payload data frame is invalid.
    InvalidPayload = 1007,
    /// The peer violated a policy.
    PolicyViolation = 1008,
    /// A message exceeds the implementation's message size limit.
    MessageTooBig = 1009,
    /// The server encountered an unexpected condition.
    InternalError = 1011,
    /// The server is restarting.
    ServiceRestart = 1012,
    /// The server is temporarily overloaded.
    TryAgainLater = 1013,
    /** @} */

    /** @name Application-level codes (4000-4999)
     *  @{
     */
    /// Authentication is required.
    AuthRequired = 4001,
    /// The supplied authentication token is invalid or expired.
    InvalidToken = 4002,
    /// A channel subscription failed.
    SubscriptionFailed = 4003,
    /// The message rate limit was exceeded.
    RateLimitExceeded = 4004,
    /// The heartbeat ping timed out.
    PingTimeout = 4005,
    /// A WebSocket frame contains malformed JSON.
    InvalidJsonFrame = 4006
    /** @} */
};

/// Represents a WebSocket protocol or application error.
struct WsError : ErrorPayload<128> {
    /// WebSocket close code associated with the error.
    ListenerCode errCode;

    /// Constructs a WebSocket error.
    /// @param code WebSocket close code.
    /// @param reason Optional additional error details.
    constexpr WsError(ListenerCode code, std::string_view reason = "") noexcept
        : ErrorPayload<128>(reason), errCode(code) {}

    /// Returns the error system identifier.
    /// @return "WS".
    [[nodiscard]] constexpr std::string_view system() const noexcept {
        return "WS";
    }
    /// Returns the numeric WebSocket close code.
    /// @return Numeric value of @ref errCode.
    [[nodiscard]] constexpr int code() const noexcept {
        return std::to_underlying(errCode);
    }
    /// Returns a human-readable description of the WebSocket error.
    /// @return Error description.
    [[nodiscard]] constexpr std::string_view description() const noexcept {
        switch (errCode) {
            case ListenerCode::NormalClosure:
                return "Normal closure";
            case ListenerCode::GoingAway:
                return "Endpoint going away";
            case ListenerCode::ProtocolError:
                return "WebSocket protocol error";
            case ListenerCode::UnsupportedData:
                return "Unsupported data type";
            case ListenerCode::InvalidPayload:
                return "Invalid frame payload data";
            case ListenerCode::PolicyViolation:
                return "Policy violation";
            case ListenerCode::MessageTooBig:
                return "Message too big";
            case ListenerCode::InternalError:
                return "Internal server error";
            case ListenerCode::ServiceRestart:
                return "Service is restarting";
            case ListenerCode::TryAgainLater:
                return "Temporary overload, try again later";

            case ListenerCode::AuthRequired:
                return "Authentication required";
            case ListenerCode::InvalidToken:
                return "Invalid or expired session token";
            case ListenerCode::SubscriptionFailed:
                return "Channel subscription failed";
            case ListenerCode::RateLimitExceeded:
                return "Message rate limit exceeded";
            case ListenerCode::PingTimeout:
                return "Heartbeat ping timeout";
            case ListenerCode::InvalidJsonFrame:
                return "Malformed JSON in frame";
            default:
                return "Unknown WebSocket error";
        }
    }
};

/// Provides std::format support for @ref WsError.
template <>
struct std::formatter<WsError> : BaseErrorFormatter<WsError> {};

static_assert(FastError<WsError>);

/// Enumerates REST API HTTP status codes used by error values.
enum class HttpCode : std::uint16_t {
    /** @name Client errors (4xx)
     *  @{
     */
    /// The request is malformed or invalid.
    BadRequest = 400,
    /// Authentication is required or has failed.
    Unauthorized = 401,
    /// Payment is required to continue.
    PaymentRequired = 402,
    /// The server refuses to authorize the request.
    Forbidden = 403,
    /// The requested resource was not found.
    NotFound = 404,
    /// The HTTP method is not allowed for the resource.
    MethodNotAllowed = 405,
    /// The server timed out while waiting for the request.
    RequestTimeout = 408,
    /// The request conflicts with the current resource state.
    Conflict = 409,
    /// The resource is no longer available.
    Gone = 410,
    /// The request payload exceeds the allowed size.
    PayloadTooLarge = 413,
    /// JSON fields failed validation.
    UnprocessableEntity = 422,
    /// The client exceeded the configured request rate limit.
    TooManyRequests = 429,
    /** @} */

    /** @name Server errors (5xx)
     *  @{
     */
    /// The server encountered an unexpected condition.
    InternalServerError = 500,
    /// The requested HTTP method is not implemented.
    NotImplemented = 501,
    /// An upstream server returned an invalid response.
    BadGateway = 502,
    /// The server is temporarily unavailable.
    ServiceUnavailable = 503,
    /// The upstream server did not produce a timely response.
    GatewayTimeout = 504
    /** @} */
};

/// Represents an HTTP REST API error.
struct RestError : ErrorPayload<128> {
    /// HTTP status code associated with the error.
    HttpCode errCode;

    /// Constructs a REST API error.
    /// @param code HTTP status code.
    /// @param reason Optional additional error details.
    constexpr RestError(HttpCode code, std::string_view reason = "") noexcept
        : ErrorPayload<128>(reason), errCode(code) {}

    /// Returns the error system identifier.
    /// @return "REST".
    [[nodiscard]] constexpr std::string_view system() const noexcept {
        return "REST";
    }
    /// Returns the numeric HTTP status code.
    /// @return Numeric value of @ref errCode.
    [[nodiscard]] constexpr int code() const noexcept {
        return std::to_underlying(errCode);
    }
    /// Returns a human-readable description of the HTTP error.
    /// @return Error description.
    [[nodiscard]] constexpr std::string_view description() const noexcept {
        switch (errCode) {
            case HttpCode::BadRequest:
                return "Bad Request";
            case HttpCode::Unauthorized:
                return "Unauthorized";
            case HttpCode::PaymentRequired:
                return "Payment Required";
            case HttpCode::Forbidden:
                return "Forbidden";
            case HttpCode::NotFound:
                return "Resource Not Found";
            case HttpCode::MethodNotAllowed:
                return "Method Not Allowed";
            case HttpCode::RequestTimeout:
                return "Request Timeout";
            case HttpCode::Conflict:
                return "Resource Conflict";
            case HttpCode::Gone:
                return "Resource Gone";
            case HttpCode::PayloadTooLarge:
                return "Payload Too Large";
            case HttpCode::UnprocessableEntity:
                return "Validation Failed (Unprocessable Entity)";
            case HttpCode::TooManyRequests:
                return "Too Many Requests";

            case HttpCode::InternalServerError:
                return "Internal Server Error";
            case HttpCode::NotImplemented:
                return "Not Implemented";
            case HttpCode::BadGateway:
                return "Bad Gateway";
            case HttpCode::ServiceUnavailable:
                return "Service Unavailable";
            case HttpCode::GatewayTimeout:
                return "Gateway Timeout";
            default:
                return "Unknown HTTP Error";
        }
    }
};

/// Provides std::format support for @ref RestError.
template <>
struct std::formatter<RestError> : BaseErrorFormatter<RestError> {};
static_assert(FastError<RestError>);
