#include "ThreadPool.h"

#include <algorithm>

ThreadPool::ThreadPool(int numThreads)
    : m_numThreads(numThreads) {}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::start() {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (m_running.load(std::memory_order_relaxed)) {
        return;
    }

    // Auto-detect thread count if not explicitly set: max(2, hw_concurrency - 1), capped at 12
    if (m_numThreads <= 0) {
        const int hw = static_cast<int>(std::thread::hardware_concurrency());
        m_numThreads = std::max(2, hw - 1);
        m_numThreads = std::min(m_numThreads, 12);
    }

    m_stopping.store(false, std::memory_order_relaxed);
    m_running.store(true, std::memory_order_release);

    for (int i = 0; i < m_numThreads; ++i) {
          m_workers.emplace_back(&ThreadPool::workerLoop, this);
    }
}

void ThreadPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (!m_running.load(std::memory_order_relaxed)) {
            return;
        }
        m_stopping.store(true, std::memory_order_release);
    }

    m_cv.notify_all();

    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_workers.clear();

    {
        std::lock_guard<SpinLock> qLock(m_queueLock);
        while (!m_pending.empty()) {
            m_pending.pop();
        }
    }
    m_pendingCount.store(0, std::memory_order_relaxed);

    m_running.store(false, std::memory_order_release);
}

void ThreadPool::submit(std::function<void()> task, int priority) {
    if (!m_running.load(std::memory_order_acquire) ||
        m_stopping.load(std::memory_order_acquire)) {
        return;
    }

    {
        std::lock_guard<SpinLock> lock(m_queueLock);
        PrioritizedTask pt;
        pt.priority = priority;
        pt.sequence = m_sequenceCounter++;
        pt.func = std::move(task);
        m_pending.push(std::move(pt));
    }
    m_pendingCount.fetch_add(1, std::memory_order_release);
    m_cv.notify_one();
}

int ThreadPool::numWorkers() const {
    return m_numThreads;
}

int ThreadPool::pendingCount() const {
    return m_pendingCount.load(std::memory_order_acquire);
}

int ThreadPool::activeCount() const {
    return m_activeCount.load(std::memory_order_acquire);
}

void ThreadPool::workerLoop() {
    while (true) {
        // Phase 1: wait for work — only hold m_stateMutex for the CV predicate
        {
            std::unique_lock<std::mutex> lock(m_stateMutex);
            m_cv.wait(lock, [this] {
                return m_stopping.load(std::memory_order_acquire) ||
                       m_pendingCount.load(std::memory_order_acquire) > 0;
            });
            if (m_stopping.load(std::memory_order_acquire) &&
                m_pendingCount.load(std::memory_order_acquire) == 0) {
                return; // shutdown and no remaining work
            }
        }
        // m_stateMutex is now released; workers don't contend on it while dequeuing

        // Phase 2: try to dequeue under m_queueLock only (no nested mutex)
        PrioritizedTask task;
        bool gotTask = false;
        {
            std::lock_guard<SpinLock> qLock(m_queueLock);
            if (m_pending.empty()) {
                // Spurious wakeup — pendingCount was stale, go back to wait
                continue;
            }
            task = std::move(const_cast<PrioritizedTask&>(m_pending.top()));
            m_pending.pop();
            gotTask = true;
        }
        // m_queueLock released immediately after dequeue — minimizes contention window

        if (gotTask) {
            m_pendingCount.fetch_sub(1, std::memory_order_release);

            m_activeCount.fetch_add(1, std::memory_order_relaxed);
            task.func();
            m_activeCount.fetch_sub(1, std::memory_order_release);
        }
    }
}
