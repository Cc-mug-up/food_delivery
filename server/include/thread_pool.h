// ============================================================
// 固定大小线程池 — 任务队列 + 条件变量
// ============================================================
#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <future>

class ThreadPool {
public:
    using Task = std::function<void()>;

    explicit ThreadPool(size_t num_threads) : stop_(false) {
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& w : workers_) {
            if (w.joinable()) w.join();
        }
    }

    // 提交任务, 返回 future
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
        using RetType = decltype(f(args...));
        auto task = std::make_shared<std::packaged_task<RetType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        std::future<RetType> future = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_) throw std::runtime_error("ThreadPool: submit on stopped pool");
            tasks_.emplace([task] { (*task)(); });
        }
        cv_.notify_one();
        return future;
    }

    // 简易提交 (无返回值)
    void enqueue(Task t) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_) return;
            tasks_.emplace(std::move(t));
        }
        cv_.notify_one();
    }

    size_t pending_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return tasks_.size();
    }

    size_t thread_count() const { return workers_.size(); }

private:
    void worker_loop() {
        while (true) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

    std::vector<std::thread> workers_;
    std::queue<Task>         tasks_;
    mutable std::mutex       mutex_;
    std::condition_variable  cv_;
    std::atomic<bool>        stop_;
};
