#include "thread_pool.hpp"
#include <iostream>

ThreadPool::ThreadPool(size_t num_threads, size_t max_queue_capacity)
    : num_threads_(num_threads) {
    task_queue_ = std::make_unique<BoundedQueue<Task>>(max_queue_capacity);
    MetricsEngine::instance().set_queue_capacity(max_queue_capacity);
    MetricsEngine::instance().set_idle_workers(num_threads);

    for (size_t i = 0; i < num_threads_; ++i) {
        workers_.emplace_back(&ThreadPool::worker_loop, this, i);
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::worker_loop(size_t worker_id) {
    (void)worker_id;
    while (!stop_flag_.load()) {
        Task task;
        if (task_queue_->pop(task)) {
            MetricsEngine::instance().increment_active_workers();
            MetricsEngine::instance().update_queue_state(task_queue_->size(), task_queue_->get_avg_wait_time_ms());

            try {
                if (task) {
                    task();
                }
            } catch (const std::exception& e) {
                std::cerr << "[ThreadPool] Exception in worker task: " << e.what() << std::endl;
                MetricsEngine::instance().record_error();
            } catch (...) {
                std::cerr << "[ThreadPool] Unknown exception in worker task" << std::endl;
                MetricsEngine::instance().record_error();
            }

            MetricsEngine::instance().decrement_active_workers();
            MetricsEngine::instance().update_queue_state(task_queue_->size(), task_queue_->get_avg_wait_time_ms());
        }
    }
}

bool ThreadPool::submit(Task task, int timeout_ms) {
    if (stop_flag_.load()) return false;

    bool pushed = task_queue_->try_push(std::move(task), timeout_ms);
    MetricsEngine::instance().update_queue_state(task_queue_->size(), task_queue_->get_avg_wait_time_ms());

    if (!pushed) {
        MetricsEngine::instance().record_rejected_request();
    }
    return pushed;
}

void ThreadPool::shutdown() {
    if (stop_flag_.exchange(true)) {
        return; // Already shut down
    }

    if (task_queue_) {
        task_queue_->stop();
    }

    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

size_t ThreadPool::get_queue_size() {
    return task_queue_ ? task_queue_->size() : 0;
}

size_t ThreadPool::get_queue_capacity() const {
    return task_queue_ ? task_queue_->capacity() : 0;
}

double ThreadPool::get_avg_queue_wait_ms() const {
    return task_queue_ ? task_queue_->get_avg_wait_time_ms() : 0.0;
}

uint64_t ThreadPool::get_rejected_tasks() const {
    return task_queue_ ? task_queue_->get_total_rejected() : 0;
}
