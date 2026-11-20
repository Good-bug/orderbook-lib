#include <cstddef>
#include <format>
#include <queue>
#include <string>

#include "quill/LogMacros.h"
#include "quill/Logger.h"
#include "domain/types.hpp"
#include "sync_types.hpp"

namespace ob::orderbook {
template <class T>
class BookSynchronizer {
   public:
    explicit BookSynchronizer(quill::Logger* logger) : logger_(logger) {}

    Action onUpdate(const domain::DepthUpdate& update) {
        if (state_ == State::WaitingSnapshot) {
            buffer_.push(update);
            LOG_DEBUG(logger_,
                      "Buffered depth update {}-{} while waiting for snapshot ({} updates buffered)",
                      update.first_update_id.value, update.last_update_id.value,
                      buffer_.size());
            return Action::None;
        }

        if (update.first_update_id.value != book_.lastUpdateId().value + 1) {
            return reset(std::format("Sequence gap: expected update {}, received range {}-{}",
                        book_.lastUpdateId().value + 1,
                        update.first_update_id.value,
                        update.last_update_id.value));
        }

        LOG_DEBUG(logger_, "Applying live depth update {}-{}",
                  update.first_update_id.value, update.last_update_id.value);
        book_.apply(update);
        return Action::None;
    }

    Action onSnapshot(const domain::DepthSnapshot& snapshot) {
        const auto buffered_count = buffer_.size();
        LOG_DEBUG(logger_,
                  "Applying snapshot {} with {} buffered updates",
                  snapshot.last_update_id.value, buffered_count);
        book_.clean();
        book_.apply(snapshot);

        std::size_t stale_count = 0;
        while (!buffer_.empty() &&
               book_.lastUpdateId() >= buffer_.front().last_update_id) {
            buffer_.pop();
            ++stale_count;
        }
        if (stale_count > 0) {
            LOG_DEBUG(logger_,
                      "Discarded {} buffered updates at current sequence boundary {}",
                      stale_count, book_.lastUpdateId().value);
        }

        if (!buffer_.empty() && book_.lastUpdateId().value + 1 !=
                                    buffer_.front().first_update_id.value) {

            return reset(std::format("Snapshot {} cannot continue at buffered update {}; {} buffered updates remain",
                        snapshot.last_update_id.value,
                        buffer_.front().first_update_id.value, buffer_.size()));
        }

        std::size_t replayed_count = 0;
        while (!buffer_.empty()) {
            if (buffer_.front().first_update_id.value !=
                book_.lastUpdateId().value + 1) {
                return reset(std::format(
                    "Sequence gap while replaying buffered updates: expected update {}, received range {}-{}",
                    book_.lastUpdateId().value + 1,
                    buffer_.front().first_update_id.value,
                    buffer_.front().last_update_id.value));
            }
            book_.apply(buffer_.front());
            buffer_.pop();
            ++replayed_count;
        }

        state_ = State::Live;
        LOG_DEBUG(logger_,
                  "Snapshot synchronization complete at update {}; replayed {} buffered updates",
                  snapshot.last_update_id.value, replayed_count);
        return Action::None;
    }

    Action reset(const std::string& error) {
        state_ = State::WaitingSnapshot;
        buffer_ = {};
        LOG_WARNING(logger_,
                    "Order book synchronization lost; requesting a new snapshot. Reset case: {}",
                    error);
        return Action::RequestSnapshot;
    }

    const T& book() const { return book_; }

   private:
    quill::Logger* logger_;
    T book_;
    State state_{State::WaitingSnapshot};

    std::queue<domain::DepthUpdate> buffer_;
};
}  // namespace ob::orderbook