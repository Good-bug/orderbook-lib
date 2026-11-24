#pragma once
#include <concepts>
#include <cstddef>
#include <memory>

#include "../sync_types.hpp"
#include "domain/types.hpp"

namespace ob::orderbook {

template <class B>
concept BookLike =
    std::default_initializable<B> &&
    requires(B b, const B cb, const domain::DepthUpdate& u,
             const domain::DepthSnapshot& s, std::size_t depth) {
        { b.apply(u) } -> std::same_as<void>;
        { b.apply(s) } -> std::same_as<void>;
        { b.clean() } -> std::same_as<void>;
        { cb.lastUpdateId() } -> std::convertible_to<domain::SeqId>;
        {
            cb.getBookView(depth)
        } -> std::convertible_to<std::shared_ptr<const BookView>>;
    };

}  // namespace ob::orderbook