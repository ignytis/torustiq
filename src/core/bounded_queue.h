#ifndef TORUSTIQ_CORE_BOUNDED_QUEUE_H_
#define TORUSTIQ_CORE_BOUNDED_QUEUE_H_

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace torustiq {

// Thread-safe FIFO with backpressure: Push blocks while the queue is full.
template <typename T> class BoundedQueue {
  public:
    explicit BoundedQueue(size_t capacity) : capacity_(std::max<size_t>(capacity, 1)) {}

    // Blocks while full. Returns false if the queue was closed (the item is dropped).
    bool Push(T item) {
        std::unique_lock lock(mutex_);
        not_full_.wait(lock, [this] { return closed_ || items_.size() < capacity_; });
        if (closed_) {
            return false;
        }
        items_.push_back(std::move(item));
        not_empty_.notify_one();
        return true;
    }

    // Blocks while empty. Returns nullopt once the queue is closed and drained.
    std::optional<T> Pop() {
        std::unique_lock lock(mutex_);
        not_empty_.wait(lock, [this] { return closed_ || !items_.empty(); });
        if (items_.empty()) {
            return std::nullopt;
        }
        T item = std::move(items_.front());
        items_.pop_front();
        not_full_.notify_one();
        return item;
    }

    // No more pushes; consumers still receive the pending items.
    void Close() {
        std::lock_guard lock(mutex_);
        closed_ = true;
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    // Closes the queue and discards pending items.
    void Abort() {
        std::lock_guard lock(mutex_);
        closed_ = true;
        items_.clear();
        not_empty_.notify_all();
        not_full_.notify_all();
    }

  private:
    const size_t capacity_;
    std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::deque<T> items_;
    bool closed_ = false;
};

} // namespace torustiq

#endif // TORUSTIQ_CORE_BOUNDED_QUEUE_H_
