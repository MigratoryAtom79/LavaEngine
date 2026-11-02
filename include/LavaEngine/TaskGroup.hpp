#pragma once

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>

namespace LavaEngine
{
    /**
     * @brief Counts outstanding work and blocks until every task is done.
     * @detail A host thread constructs a group for N tasks, hands copies of
     * it to the tasks, and calls wait() after dispatching them. Every task
     * calls done() exactly once; wait() returns when the count reaches zero.
     * Copies share one counter, so the group may be captured by value.
     * @note Ownership: Shared value type. The internal state is heap-allocated
     * and reference-counted, so a TaskGroup can safely outlive the function
     * that created it as long as every captured copy is destroyed in turn.
     * This is a one-shot group per phase; create a new group for the next
     * phase. Extra done() calls after the count reaches zero are ignored.
     * @example
     * @code
     * TaskGroup group(tasks.size());
     * for (auto& task : tasks)
     *     pool.post([&group, &task] { task(); group.done(); });
     * group.wait(); // phase barrier
     * @endcode
     */
    class TaskGroup
    {
    public:
        explicit TaskGroup(std::size_t count = 0);

        /**
         * @brief Records one finished task.
         * @note Ignored once the group has reached zero.
         */
        void done();

        /**
         * @brief Blocks until every task has called done().
         */
        void wait();

        /**
         * @brief Adds more work to the group before it is awaited.
         * @param count Number of additional tasks to expect.
         * @note Must not race with wait(); add before waiting.
         */
        void add(std::size_t count = 1);

        [[nodiscard]]
        std::size_t remaining() const;

    private:
        struct State
        {
            mutable std::mutex mutex;
            std::condition_variable cv;
            std::size_t remaining = 0;
        };

        std::shared_ptr<State> m_state;
    };
}
