#ifndef THREAD_POOL_HPP
#define THREAD_POOL_HPP

#include "bounded_queue.hpp"
#include "metrics.hpp"
#include <vector>
#include <thread>
#include <functional>
#include <memory>
#include <atomic>

class ThreadPool {
public:
    using Task = std::function<void()>;

private:
    std::vector<std::thread> workers_;
    std::unique_ptr<BoundedQueue<Task>> task_queue_;
    std::atomic<bool> stop_flag_{false};
    size_t num_threads_;

    void worker_loop(size_t worker_id);

public:
    ThreadPool(size_t num_threads, size_t max_queue_capacity);
    ~ThreadPool();

    bool submit(Task task, int timeout_ms);
    void shutdown();

    size_t get_queue_size();
    size_t get_queue_capacity() const;
    double get_avg_queue_wait_ms() const;
    uint64_t get_rejected_tasks() const;
};

#endif // THREAD_POOL_HPP
