#include <cstdlib>
#include <iostream>
#include <vector>

#include <glm/vec3.hpp>

#include "../src/world/tick/ScheduledBlockTickQueue.h"

namespace {

int fail(const char* message) {
    std::cerr << "[scheduled_block_tick_queue_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

std::vector<glm::ivec3> drainAll(ScheduledBlockTickQueue& queue, const uint64_t currentTick,
                                 const uint32_t budget = 4096) {
    std::vector<glm::ivec3> executed;
    queue.process(currentTick, budget, [&executed](const ScheduledBlockTick& tick) { executed.push_back(tick.pos); });
    return executed;
}

} // namespace

int main() {
    {
        // Due order: dueTick ascending, then higher Y first, then x ascending, then z ascending.
        ScheduledBlockTickQueue queue;
        queue.schedule(glm::ivec3(0, 5, 0), 3);
        queue.schedule(glm::ivec3(0, 9, 0), 2);
        queue.schedule(glm::ivec3(1, 9, 0), 2);
        queue.schedule(glm::ivec3(1, 9, 1), 2);
        queue.schedule(glm::ivec3(0, 0, 0), 1);
        queue.schedule(glm::ivec3(0, 0, 0), 3);

        const std::vector<glm::ivec3> executed = drainAll(queue, 2);
        const std::vector<glm::ivec3> expected = {glm::ivec3(0, 0, 0), glm::ivec3(0, 9, 0), glm::ivec3(1, 9, 0),
                                                  glm::ivec3(1, 9, 1)};
        if (executed != expected) {
            return fail("due entries must pop in deterministic tick/Y/x/z order");
        }
        if (queue.pendingCount() != 1) {
            return fail("the not-yet-due entry must remain pending");
        }
    }

    {
        // Earliest-wins dedup: a later due tick for the same position is dropped,
        // an earlier due tick replaces a later one.
        ScheduledBlockTickQueue queue;
        queue.schedule(glm::ivec3(0, 64, 0), 5);
        queue.schedule(glm::ivec3(0, 64, 0), 8);
        queue.schedule(glm::ivec3(1, 64, 0), 9);
        queue.schedule(glm::ivec3(1, 64, 0), 7);
        if (queue.pendingCount() != 2) {
            return fail("pendingCount must reflect unique positions, not heap duplicates");
        }

        const std::vector<glm::ivec3> executed = drainAll(queue, 10);
        const std::vector<glm::ivec3> expected = {glm::ivec3(0, 64, 0), glm::ivec3(1, 64, 0)};
        if (executed != expected) {
            return fail("each position must execute once at its earliest due tick");
        }
    }

    {
        // Stale heap duplicates are skipped without consuming budget.
        ScheduledBlockTickQueue queue;
        queue.schedule(glm::ivec3(0, 64, 0), 9);
        queue.schedule(glm::ivec3(0, 64, 0), 4);

        std::vector<glm::ivec3> executed;
        const uint32_t count = queue.process(
            10, 1, [&executed](const ScheduledBlockTick& tick) { executed.push_back(tick.pos); });
        if (count != 1 || executed.size() != 1 || executed[0] != glm::ivec3(0, 64, 0)) {
            return fail("the stale heap duplicate must not consume the budget");
        }
        if (queue.pendingCount() != 0) {
            return fail("no entries must remain after the winning tick executed");
        }
    }

    {
        // Budget limits executed entries per call; remaining due entries wait.
        ScheduledBlockTickQueue queue;
        queue.schedule(glm::ivec3(0, 64, 0), 1);
        queue.schedule(glm::ivec3(1, 64, 0), 1);
        queue.schedule(glm::ivec3(2, 64, 0), 1);

        const std::vector<glm::ivec3> first = drainAll(queue, 1, 2);
        if (first.size() != 2 || queue.pendingCount() != 1) {
            return fail("budgeted processing must leave queued entries for later calls");
        }
        const std::vector<glm::ivec3> second = drainAll(queue, 1);
        if (second.size() != 1 || queue.pendingCount() != 0) {
            return fail("remaining due entries must drain on the next call");
        }
    }

    {
        // Reentrant scheduling: fn may schedule new entries mid-run. Entries due
        // this tick join the same run in comparator order; future ticks wait.
        ScheduledBlockTickQueue queue;
        queue.schedule(glm::ivec3(0, 65, 0), 5);
        queue.schedule(glm::ivec3(0, 64, 0), 5);

        std::vector<glm::ivec3> executed;
        queue.process(5, 4096, [&](const ScheduledBlockTick& tick) {
            executed.push_back(tick.pos);
            if (tick.pos == glm::ivec3(0, 65, 0)) {
                queue.schedule(glm::ivec3(0, 66, 0), 5);
                queue.schedule(glm::ivec3(0, 63, 0), 6);
            }
        });

        const std::vector<glm::ivec3> expected = {glm::ivec3(0, 65, 0), glm::ivec3(0, 66, 0),
                                                  glm::ivec3(0, 64, 0)};
        if (executed != expected) {
            return fail("entries scheduled mid-run and due this tick must join the same run in order");
        }
        if (queue.pendingCount() != 1) {
            return fail("future-tick entries scheduled mid-run must remain pending");
        }
    }

    {
        // Rescheduling the executing position mid-run must not fire twice.
        ScheduledBlockTickQueue queue;
        queue.schedule(glm::ivec3(0, 64, 0), 5);

        int executions = 0;
        queue.process(5, 4096, [&](const ScheduledBlockTick&) {
            ++executions;
            queue.schedule(glm::ivec3(0, 64, 0), 7);
        });
        if (executions != 1 || queue.pendingCount() != 1) {
            return fail("a position rescheduled during its own execution must run once and stay pending later");
        }
    }

    {
        // clear() empties both the heap and the dedup table.
        ScheduledBlockTickQueue queue;
        queue.schedule(glm::ivec3(0, 64, 0), 5);
        queue.schedule(glm::ivec3(1, 64, 0), 6);
        queue.clear();
        if (queue.pendingCount() != 0) {
            return fail("clear must remove all pending entries");
        }
        if (!drainAll(queue, 100).empty()) {
            return fail("clear must leave nothing to execute");
        }
    }

    std::cout << "[scheduled_block_tick_queue_test] PASS\n";
    return EXIT_SUCCESS;
}
