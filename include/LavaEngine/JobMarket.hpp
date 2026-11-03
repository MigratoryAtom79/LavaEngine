#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace LavaEngine
{
    /**
     * @brief Thread-safe queue of work consumed by a WorkerPool.
     * @detail Producers post WorkItems and worker threads wait for them.
     * Items with a higher `priority` value are taken first; equal priorities
     * keep FIFO order. Closing the market rejects further posts, discards
     * items that have not been taken, and wakes every waiting worker so it
     * can exit.
     * @note Ownership: Owned by a WorkerPool. WorkItems are owned by the
     * market until take; callers keep ownership of anything their task
     * captures.
     * @example
     * @code
     * JobMarket market;
     * market.post({"grow", [] { ... }});
     * JobMarket::WorkItem item;
     * if (market.waitTake(item)) item.task();
     * @endcode
     */
    class JobMarket
    {
    public:
        /**
         * @brief One named unit of work posted to the market.
         * @note Ownership: Owned by the market while queued. `task` captures
         * whatever it needs; those captures must outlive execution.
         */
        struct WorkItem
        {
            std::string name;
            std::function<void()> task;
            int priority = 0;
        };

        /**
         * @brief Creates a market, optionally with a bounded queue.
         * @param capacity Maximum queued items; 0 means unbounded.
         */
        explicit JobMarket(std::size_t capacity = 0);

        /**
         * @brief Enqueues work and wakes one waiting worker.
         * @param item The work to post; `priority` orders it in the queue.
         * @return False when the market is closed; the item is not queued.
         * @note Blocks while a bounded market is full.
         */
        bool post(WorkItem item);

        /**
         * @brief Enqueues a task with an optional name and priority.
         * @param task Callable run on a worker thread.
         * @param name Optional human-readable label.
         * @param priority Higher values are taken first; ties stay FIFO.
         * @return False when the market is closed.
         */
        bool post(
            std::function<void()> task,
            std::string name = {},
            int priority = 0
        );

        /**
         * @brief Enqueues work without blocking when the market is full.
         * @param item The work to post; `priority` orders it in the queue.
         * @return False when the market is closed or full.
         */
        bool tryPost(WorkItem item);

        /**
         * @brief Enqueues a task without blocking when the market is full.
         * @param task Callable run on a worker thread.
         * @param name Optional human-readable label.
         * @param priority Higher values are taken first; ties stay FIFO.
         * @return False when the market is closed or full.
         */
        bool tryPost(
            std::function<void()> task,
            std::string name = {},
            int priority = 0
        );

        /**
         * @brief Takes the oldest queued item without blocking.
         * @param out Receives the item when one is available.
         * @return True when an item was taken.
         */
        bool tryTake(WorkItem& out);

        /**
         * @brief Blocks until work is available or the market closes.
         * @param out Receives the item when one is available.
         * @return False when the market is closed and has no work.
         */
        bool waitTake(WorkItem& out);

        /**
         * @brief Closes the market, discards queued work, and wakes all waiters.
         * @note Idempotent. An item already taken by a worker still runs.
         */
        void close();

        [[nodiscard]]
        bool closed() const;

        [[nodiscard]]
        std::size_t size() const;

        [[nodiscard]]
        bool empty() const;

        [[nodiscard]]
        std::size_t capacity() const;

        /**
         * @brief Returns a snapshot of queued job names in execution order.
         * @return Copies of the names, highest priority first and FIFO within
         *         a priority; empty names are preserved.
         */
        [[nodiscard]]
        std::vector<std::string> queuedNames() const;

    private:
        [[nodiscard]]
        bool hasRoom() const
        {
            return m_capacity == 0 || m_jobs.size() < m_capacity;
        }

        mutable std::mutex m_mutex;
        std::condition_variable m_cv;
        std::condition_variable m_notFull;
        std::multimap<int, WorkItem, std::greater<int>> m_jobs;
        std::size_t m_capacity = 0;
        bool m_closed = false;
    };
}
