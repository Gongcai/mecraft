#ifndef MECRAFT_THREADPOOL_H
#define MECRAFT_THREADPOOL_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

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
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;

    std::priority_queue<PrioritizedTask, std::vector<PrioritizedTask>,
                        std::greater<PrioritizedTask>> m_pending;

    std::atomic<int> m_activeCount{0};
    uint64_t m_sequenceCounter = 0;
    bool m_running = false;
    bool m_stopping = false;
    int m_numThreads = 0;
};

#endif // MECRAFT_THREADPOOL_H
