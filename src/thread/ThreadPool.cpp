#include "ThreadPool.h"

#include <algorithm>

ThreadPool::ThreadPool(int numThreads)
    : m_numThreads(numThreads) {}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::start() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running) {
        return;
    }

    // Auto-detect thread count if not explicitly set: max(2, hw_concurrency - 1), capped at 8
    if (m_numThreads <= 0) {
        const int hw = static_cast<int>(std::thread::hardware_concurrency());
        m_numThreads = std::max(2, hw - 1);
        m_numThreads = std::min(m_numThreads, 8);
    }

    m_stopping = false;
    m_running = true;

    for (int i = 0; i < m_numThreads; ++i) {
        m_workers.emplace_back(&ThreadPool::workerLoop, this);
    }
}

void ThreadPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running) {
            return;
        }
        m_stopping = true;
    }

    m_cv.notify_all();

    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_workers.clear();

    std::lock_guard<std::mutex> lock(m_mutex);
    m_running = false;

    // Drain remaining tasks
    while (!m_pending.empty()) {
        m_pending.pop();
    }
}

void ThreadPool::submit(std::function<void()> task, int priority) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running || m_stopping) {
            return;
        }

        PrioritizedTask pt;
        pt.priority = priority;
        pt.sequence = m_sequenceCounter++;
        pt.func = std::move(task);
        m_pending.push(std::move(pt));
    }
    m_cv.notify_one();
}

int ThreadPool::numWorkers() const {
    return m_numThreads;
}

int ThreadPool::pendingCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<int>(m_pending.size());
}

int ThreadPool::activeCount() const {
    return m_activeCount.load(std::memory_order_relaxed);
}

void ThreadPool::workerLoop() {
    while (true) {
        PrioritizedTask task;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return m_stopping || !m_pending.empty(); });

            if (m_stopping && m_pending.empty()) {
                return;
            }

            task = m_pending.top();
            m_pending.pop();
        }

        m_activeCount.fetch_add(1, std::memory_order_relaxed);
        task.func();
        m_activeCount.fetch_sub(1, std::memory_order_relaxed);
    }
}
