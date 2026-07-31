#ifndef MECRAFT_THREADPOOL_H
#define MECRAFT_THREADPOOL_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

/// Lightweight spinlock for low-contention hot paths (task queue push/pop).
/// Uses pause/yield backoff to reduce bus traffic while keeping latency low.
class SpinLock {
public:
    SpinLock() = default;
    SpinLock(const SpinLock&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;

    void lock() noexcept {
        for (int i = 0; m_flag.test_and_set(std::memory_order_acquire);) {
            if (++i > 64) {
                i = 0;
                std::this_thread::yield();
            }
        }
    }
    void unlock() noexcept { m_flag.clear(std::memory_order_release); }

private:
    std::atomic_flag m_flag = ATOMIC_FLAG_INIT;
};

class ThreadPool {
public:
    ThreadPool() = default;
    explicit ThreadPool(int numThreads);
    ~ThreadPool();

    void start();
    void shutdown();

    // Submit a task with priority (lower = higher priority)
    void submit(std::function<void()> task, int priority = 0);

    int numWorkers() const;
    int pendingCount() const;
    int activeCount() const;
    bool isRunning() const;

private:
    void workerLoop();

    struct PrioritizedTask {
        int priority;
        uint64_t sequence;
        std::function<void()> func;

        bool operator>(const PrioritizedTask& other) const {
            if (priority != other.priority)
                return priority > other.priority;
            return sequence > other.sequence;
        }
    };

    std::vector<std::thread> m_workers;
    mutable SpinLock m_queueLock; // hot-path: task push/pop
    mutable std::mutex m_stateMutex; // cold-path: start/shutdown/wait
    std::condition_variable m_cv;

    std::priority_queue<PrioritizedTask, std::vector<PrioritizedTask>, std::greater<PrioritizedTask>> m_pending;

    std::atomic<int> m_activeCount{0};
    std::atomic<int> m_pendingCount{0}; // mirrors m_pending.size() for lock-free CV predicate
    std::atomic<bool> m_stopping{false};
    std::atomic<bool> m_running{false};
    uint64_t m_sequenceCounter = 0;
    int m_numThreads = 0;
};

#endif // MECRAFT_THREADPOOL_H
