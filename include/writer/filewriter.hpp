#pragma once

#include <condition_variable>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "domain/types.hpp"
#include "gateway/binance_listener_service.hpp"

namespace ob::writer {

namespace detail {
/**
 * @brief Resolves a relative output path to the nearest project root.
 *
 * The function walks up the current directory tree until it finds a parent
 * containing a CMakeLists.txt file or a .git directory and then resolves the
 * supplied path against that location.
 *
 * @param p Path to resolve.
 * @return The original path if it is absolute; otherwise a path relative to the
 *         nearest project root.
 */
inline std::filesystem::path resolve_output_path(
    const std::filesystem::path& p) {
    if (p.is_absolute()) {
        return p;
    }
    for (auto dir = std::filesystem::current_path(); dir.has_relative_path();
         dir = dir.parent_path()) {
        if (std::filesystem::exists(dir / "CMakeLists.txt") ||
            std::filesystem::exists(dir / ".git")) {
            return dir / p;
        }
    }
    return p;
}
}  // namespace detail

/**
 * @brief Asynchronously writes DepthUpdate records to a CSV file.
 *
 * A formatter callback converts each domain::DepthUpdate instance into a single
 * CSV row without a trailing newline. Rows are enqueued in a background thread
 * and written to disk in FIFO order.
 */
class AsyncDepthUpdateCsvWriter : public observer::DepthUpdateObserver {
   public:
    using CsvFormatter = std::function<std::string(const domain::DepthUpdate&)>;

    /**
     * @brief Opens the destination file in append mode and starts the writer
     * thread.
     *
     * @param filepath Output file path. Relative paths are resolved against the
     *                 nearest project root.
     * @param formatter Function that converts a DepthUpdate into one CSV row.
     * @throws std::runtime_error if the output file cannot be opened.
     * @throws std::invalid_argument if the formatter is empty.
     */
    AsyncDepthUpdateCsvWriter(const std::string& filepath,
                              CsvFormatter formatter)
        : filepath_(filepath),
          output_(detail::resolve_output_path(filepath_),
                  std::ios::out | std::ios::app),
          formatter_(std::move(formatter)) {
        if (!output_) {
            throw std::runtime_error("Unable to open output file: " +
                                     filepath_.string());
        }
        if (!formatter_) {
            throw std::invalid_argument("CSV formatter must not be empty");
        }
        worker_ = std::thread(&AsyncDepthUpdateCsvWriter::run, this);
    }

    AsyncDepthUpdateCsvWriter(const AsyncDepthUpdateCsvWriter&) = delete;
    AsyncDepthUpdateCsvWriter& operator=(const AsyncDepthUpdateCsvWriter&) =
        delete;

    /**
     * @brief Destroys the writer and flushes any remaining queued rows.
     *
     * The destructor suppresses exceptions from close() to keep object teardown
     * safe in the face of write failures.
     */
    ~AsyncDepthUpdateCsvWriter() {
        try {
            close();
        } catch (...) {
            // Destructors must not propagate write errors.
            const auto error = std::current_exception();
            try {
                if (error) {
                    std::rethrow_exception(error);
                }
            } catch (const std::exception& exception) {
                std::cerr << "Error in AsyncDepthUpdateCsvWriter destructor: "
                          << exception.what() << '\n';
            } catch (...) {
                std::cerr << "Unknown error in AsyncDepthUpdateCsvWriter "
                             "destructor\n";
            }
        }
    }

    void onDepthUpdate(const domain::DepthUpdate& update) override {
        write(update);
    }

    void statusUpdate(const observer::ListenerStatus& status) override {
        // TODO
    }

    /**
     * @brief Queues a DepthUpdate for asynchronous CSV output.
     *
     * The formatter is called before locking the queue so the formatting
     * callback does not block the queue mutex.
     *
     * @param update Depth update to serialize.
     * @throws std::runtime_error if the writer is closed.
     */
    void write(const domain::DepthUpdate& update) {
        // Format outside the lock so custom formatting does not block queue
        // use.
        std::string row = formatter_(update);
        std::scoped_lock lock(mutex_);

        rethrowWorkerError();

        if (stopping_) {
            throw std::runtime_error("Cannot write to a closed CSV writer");
        }

        rows_.push(std::move(row));
        condition_.notify_one();
    }

    /**
     * @brief Stops accepting new writes and waits for queued rows to flush.
     *
     * This method sets the stop flag, wakes the worker, and then joins the
     * background thread before rethrowing any worker-thread exception.
     */
    void close() {
        {
            std::scoped_lock lock(mutex_);
            // The worker drains queued rows before exiting.
            stopping_ = true;
        }
        condition_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
            rethrowWorkerError();
        }
    }

   private:
    /**
     * @brief Background worker loop that serializes queued rows to disk.
     *
     * The worker waits for data, removes one row from the queue, writes it to
     * the output stream, and flushes when the queue becomes empty.
     */
    void run() {
        try {
            for (;;) {
                std::string row;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    condition_.wait(
                        lock, [this] { return stopping_ || !rows_.empty(); });
                    if (rows_.empty() && stopping_) {
                        break;
                    }
                    // Take a row under the lock, then perform I/O without it.
                    row = std::move(rows_.front());
                    rows_.pop();
                }

                output_ << row << '\n';
                if (!output_) {
                    throw std::runtime_error("Failed while writing CSV data");
                }

                // Flush immediately if the queue has no more pending rows
                {
                    std::scoped_lock lock(mutex_);
                    if (rows_.empty()) {
                        output_.flush();
                    }
                }
            }
            output_.flush();
            if (!output_) {
                throw std::runtime_error("Failed to flush CSV data");
            }
        } catch (...) {
            std::scoped_lock lock(mutex_);
            // Save the failure for callers and discard rows that cannot be
            // written.
            worker_error_ = std::current_exception();
            stopping_ = true;
            while (!rows_.empty()) {
                rows_.pop();
            }
        }
        condition_.notify_all();
    }

    /**
     * @brief Rethrows any exception captured in the worker thread.
     */
    void rethrowWorkerError() const {
        if (worker_error_) {
            std::rethrow_exception(worker_error_);
        }
    }

    std::filesystem::path filepath_;
    std::ofstream output_;
    CsvFormatter formatter_;
    std::queue<std::string> rows_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::thread worker_;
    bool stopping_ = false;
    std::exception_ptr worker_error_;
};

}  // namespace ob::writer
