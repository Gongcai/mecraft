#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "../src/thread/ThreadPool.h"

namespace {
int fail(const char* message) {
    std::cerr << "[thread_pool_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

template <typename Predicate> bool waitUntil(Predicate&& predicate, const int timeoutMs = 2000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}
} // namespace

int main() {
    {
        ThreadPool pool(2);
        std::atomic<int> completed{0};

        pool.start();
        for (int i = 0; i < 8; ++i) {
            pool.submit([&completed]() { completed.fetch_add(1, std::memory_order_relaxed); });
        }

        if (!waitUntil([&completed]() { return completed.load(std::memory_order_relaxed) == 8; })) {
            return fail("all submitted tasks should complete");
        }

        if (pool.pendingCount() != 0 || pool.activeCount() != 0) {
            return fail("pool should be idle after draining all work");
        }

        pool.shutdown();
    }

    {
        ThreadPool pool(1);
        std::mutex gateMutex;
        std::vector<int> executionOrder;
        std::atomic<int> orderedCompleted{0};
        bool gateOpen = false;

        pool.start();
        pool.submit(
            [&]() {
                std::unique_lock<std::mutex> lock(gateMutex);
                while (!gateOpen) {
                    lock.unlock();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    lock.lock();
                }
            },
            -1);

        pool.submit(
            [&]() {
                std::lock_guard<std::mutex> lock(gateMutex);
                executionOrder.push_back(2);
                orderedCompleted.fetch_add(1, std::memory_order_relaxed);
            },
            2);
        pool.submit(
            [&]() {
                std::lock_guard<std::mutex> lock(gateMutex);
                executionOrder.push_back(0);
                orderedCompleted.fetch_add(1, std::memory_order_relaxed);
            },
            0);
        pool.submit(
            [&]() {
                std::lock_guard<std::mutex> lock(gateMutex);
                executionOrder.push_back(1);
                orderedCompleted.fetch_add(1, std::memory_order_relaxed);
            },
            0);
        pool.submit(
            [&]() {
                std::lock_guard<std::mutex> lock(gateMutex);
                executionOrder.push_back(3);
                orderedCompleted.fetch_add(1, std::memory_order_relaxed);
            },
            1);

        if (!waitUntil([&pool]() { return pool.pendingCount() == 4; })) {
            return fail("priority test should queue all work behind the gate task");
        }

        {
            std::lock_guard<std::mutex> lock(gateMutex);
            gateOpen = true;
        }

        if (!waitUntil([&orderedCompleted]() { return orderedCompleted.load(std::memory_order_relaxed) == 4; })) {
            return fail("priority test tasks should all run after the gate opens");
        }

        const std::vector<int> expectedOrder{0, 1, 3, 2};
        {
            std::lock_guard<std::mutex> lock(gateMutex);
            if (executionOrder != expectedOrder) {
                return fail("lower priority values should execute first and preserve FIFO within the same priority");
            }
        }

        pool.shutdown();
    }

    {
        ThreadPool pool(1);
        std::atomic<int> postShutdownRuns{0};

        pool.start();
        pool.shutdown();
        pool.submit([&postShutdownRuns]() { postShutdownRuns.fetch_add(1, std::memory_order_relaxed); });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (postShutdownRuns.load(std::memory_order_relaxed) != 0) {
            return fail("shutdown pool should reject new tasks");
        }
    }

    std::cout << "[thread_pool_test] PASS\n";
    return EXIT_SUCCESS;
}
