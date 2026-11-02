#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "JobMarket.hpp"
#include "TaskGroup.hpp"

namespace LavaEngine
{
    /**
     * @brief Fixed pool of worker threads fed by an owned JobMarket.
     * @detail A pool starts `workerCount` threads. While idle a worker is
     * "unemployed" and blocks on the market; posting a work item wakes one
     * worker, which is "hired" and runs the item before returning to the
     * market. Tasks that throw are forwarded to the optional ErrorHandler so
     * a worker never dies from an uncaught exception.
     * @note Ownership: Owns its JobMarket and worker threads. Non-copyable
     * and non-movable; keep it behind a pointer when ownership must move.
     * Posted tasks run on worker threads and must not touch main-thread-only
     * engine state (Container, Module, ResourceRegistry, EventBus, Vulkan).
     * stop() must be called from a thread that is not one of the workers.
     * @example
     * @code
     * WorkerPool pool(4);
     * pool.post([]{ doParallelWork(); });
     * pool.stop(); // joins in-flight work, discards unhired jobs
     * @endcode
     */
    class WorkerPool
    {
    public:
        using ErrorHandler = std::function<void(std::exception_ptr)>;

        /**
         * @brief Point-in-time state of one worker, for introspection UIs.
         */
        struct WorkerSnapshot
        {
            std::size_t id = 0;
            bool busy = false;
            std::string jobName;
        };

        /**
         * @brief Starts the worker threads.
         * @param workerCount Number of workers; 0 is treated as 1.
         * @param onError Called on a worker thread when a task throws.
         *        When empty, the exception is printed to stderr.
         * @param marketCapacity Maximum queued jobs; 0 means unbounded.
         */
        explicit WorkerPool(
            std::size_t workerCount = std::thread::hardware_concurrency(),
            ErrorHandler onError = {},
            std::size_t marketCapacity = 0
        );

        ~WorkerPool();

        WorkerPool(const WorkerPool&) = delete;
        WorkerPool& operator=(const WorkerPool&) = delete;
        WorkerPool(WorkerPool&&) = delete;
        WorkerPool& operator=(WorkerPool&&) = delete;

        /**
         * @brief Posts a work item and hires one unemployed worker.
         * @return False when the pool has been stopped.
         */
        bool post(JobMarket::WorkItem item);

        /**
         * @brief Posts a task and hires one unemployed worker.
         * @param task Callable run on a worker thread.
         * @param name Optional human-readable label.
         * @return False when the pool has been stopped.
         */
        bool post(std::function<void()> task, std::string name = {});

        /**
         * @brief Runs fn over [begin, end) in parallel and waits for all of it.
         * @param begin First index (inclusive).
         * @param end One past the last index (exclusive).
         * @param grain Minimum elements per task; 0 is treated as 1.
         * @param fn Called as fn(i) from worker threads. It must be safe to
         *        call concurrently and must outlive this call.
         * @note Blocks the calling thread until the whole range is processed,
         * so consecutive calls form phase barriers. Nested calls (made from a
         * worker) and calls on a stopped pool run inline on the calling thread
         * to avoid deadlock.
         */
        template <typename F>
        void parallelFor(
            std::size_t begin,
            std::size_t end,
            std::size_t grain,
            F&& fn
        )
        {
            if (begin >= end)
                return;

            if (grain == 0)
                grain = 1;

            const std::size_t count = end - begin;

            if (currentThreadIsWorker() || stopped())
            {
                for (std::size_t i = begin; i < end; ++i)
                    fn(i);

                return;
            }

            const std::size_t partitions =
                (count - 1) / grain + 1;

            if (partitions <= 1)
            {
                for (std::size_t i = begin; i < end; ++i)
                    fn(i);

                return;
            }

            TaskGroup group(partitions);
            std::exception_ptr inlineError;

            for (std::size_t part = 0; part < partitions; ++part)
            {
                const std::size_t first = begin + part * grain;
                const std::size_t last =
                    first + std::min(grain, end - first);

                bool posted = false;

                try
                {
                    posted = post(
                        [&fn, group, first, last]
                        {
                            struct Done
                            {
                                TaskGroup group;
                                ~Done() { group.done(); }
                            } done{group};

                            for (std::size_t i = first; i < last; ++i)
                                fn(i);
                        }
                    );
                }
                catch (...)
                {
                    posted = false;
                }

                if (posted)
                    continue;

                try
                {
                    for (std::size_t i = first; i < last; ++i)
                        fn(i);
                }
                catch (...)
                {
                    if (!inlineError)
                        inlineError = std::current_exception();
                }

                group.done();
            }

            group.wait();

            if (inlineError)
                std::rethrow_exception(inlineError);
        }

        /**
         * @brief Runs fn over [0, count) in parallel and waits.
         * @param count Number of indices.
         * @param grain Minimum elements per task.
         * @param fn Called as fn(i); see the four-argument overload.
         */
        template <typename F>
        void parallelFor(
            std::size_t count,
            std::size_t grain,
            F&& fn
        )
        {
            parallelFor(
                std::size_t{0},
                count,
                grain,
                std::forward<F>(fn)
            );
        }

        /**
         * @brief Runs fn over [0, count) in parallel with an automatic grain.
         * @param count Number of indices.
         * @param fn Called as fn(i); see the four-argument overload.
         */
        template <typename F>
        void parallelFor(
            std::size_t count,
            F&& fn
        )
        {
            const std::size_t workers =
                workerCount() == 0 ? 1 : workerCount();

            const std::size_t grain =
                count == 0
                    ? 1
                    : std::max<std::size_t>(
                          1,
                          count / (workers * 8)
                      );

            parallelFor(
                std::size_t{0},
                count,
                grain,
                std::forward<F>(fn)
            );
        }

        /**
         * @brief Closes the market, discards unhired jobs, and joins workers.
         * @detail In-flight tasks finish before this returns. Idempotent.
         * Must not be called from a worker thread.
         */
        void stop();

        [[nodiscard]]
        bool stopped() const;

        [[nodiscard]]
        std::size_t workerCount() const;

        [[nodiscard]]
        std::size_t idleWorkerCount() const;

        /**
         * @brief Returns a snapshot of every worker's current state.
         * @return One entry per worker, indexed by worker id.
         */
        [[nodiscard]]
        std::vector<WorkerSnapshot> workerSnapshots() const;

        JobMarket& market();

        const JobMarket& market() const;

    private:
        static bool currentThreadIsWorker();

        void run(std::size_t id);

        void handleError(std::exception_ptr error) const;

        JobMarket m_market;
        std::vector<std::thread> m_workers;
        std::atomic<std::size_t> m_idle{0};
        std::atomic<bool> m_stopped{false};
        std::once_flag m_stopOnce;
        ErrorHandler m_onError;
        mutable std::mutex m_statusMutex;
        std::vector<WorkerSnapshot> m_status;
    };
}
