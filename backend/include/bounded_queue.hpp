#ifndef BOUNDED_QUEUE_HPP
#define BOUNDED_QUEUE_HPP

#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>

template <typename T>
class BoundedQueue {
private:
    std::queue<std::pair<T, std::chrono::steady_clock::time_point>> queue_;
    size_t capacity_;
    std::mutex mutex_;
    std::condition_variable cv_not_empty_;
    std::condition_variable cv_not_full_;
    std::atomic<bool> stopped_{false};

    std::atomic<uint64_t> total_pushed_{0};
    std::atomic<uint64_t> total_rejected_{0};
    std::atomic<double> avg_wait_time_ms_{0.0};
    std::atomic<uint64_t> total_popped_{0};

public:
    explicit BoundedQueue(size_t capacity) : capacity_(capacity) {}

    ~BoundedQueue() {
        stop();
    }

    void stop() {
        stopped_.store(true);
        cv_not_empty_.notify_all();
        cv_not_full_.notify_all();
    }

    bool is_stopped() const {
        return stopped_.load();
    }

    size_t capacity() const {
        return capacity_;
    }

    size_t size() {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    // Attempt to push within timeout_ms. Returns false if timeout or full (backpressure rejection).
    bool try_push(T item, int timeout_ms) {
        if (stopped_.load()) return false;

        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.size() >= capacity_) {
            if (timeout_ms <= 0) {
                total_rejected_++;
                return false;
            }
            bool success = cv_not_full_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this]() {
                return queue_.size() < capacity_ || stopped_.load();
            });

            if (!success || stopped_.load() || queue_.size() >= capacity_) {
                total_rejected_++;
                return false;
            }
        }

        auto enqueue_time = std::chrono::steady_clock::now();
        queue_.push({std::move(item), enqueue_time});
        total_pushed_++;
        cv_not_empty_.notify_one();
        return true;
    }

    bool pop(T& out_item) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_not_empty_.wait(lock, [this]() {
            return !queue_.empty() || stopped_.load();
        });

        if (stopped_.load() && queue_.empty()) {
            return false;
        }

        auto front_pair = std::move(queue_.front());
        queue_.pop();
        out_item = std::move(front_pair.first);

        auto wait_duration = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - front_pair.second).count() / 1000.0;

        ++total_popped_;
        // Exponential moving average for wait time
        double cur_avg = avg_wait_time_ms_.load();
        avg_wait_time_ms_.store(cur_avg * 0.9 + wait_duration * 0.1);

        cv_not_full_.notify_one();
        return true;
    }

    double get_avg_wait_time_ms() const {
        return avg_wait_time_ms_.load();
    }

    uint64_t get_total_rejected() const {
        return total_rejected_.load();
    }
};

#endif // BOUNDED_QUEUE_HPP
