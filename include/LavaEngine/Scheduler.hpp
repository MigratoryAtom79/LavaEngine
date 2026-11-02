#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "WorkerPool.hpp"

namespace LavaEngine
{
    using JobID = uint32_t;

    /**
     * @brief One schedulable unit producing an int result, owned by a Scheduler.
     * @detail A Job runs `task` until it returns > 0 (complete), returns 0 to
     * repeat on the next execute(), or returns < 0 to cancel. `exit` runs
     * exactly once, when the job completes, cancels, or is destroyed.
     * `dependencies` gate execution: the job only runs once every listed JobID
     * has completed.
     * @note Ownership: Owned by the owning Scheduler (std::vector<Job>).
     * `task`/`exit` may capture what the job needs, but captured references
     * must live at least until the job is destroyed. The Scheduler is drained
     * before containers/modules are destroyed, so jobs must not capture
     * Container/Module/Resource that could die earlier.
     * @example
     * @code
     * JobID id = scheduler.createJob([&] { doWork(); return 1; });
     * scheduler.dependsOn(id, otherJob);
     * @endcode
     */
    struct Job
    {
        JobID id;
        std::function<int()> task;
        std::function<void()> exit;
        std::string name;
        std::vector<JobID> dependencies;
        std::chrono::microseconds duration;
    };

    /**
     * @brief Owns and advances a set of Jobs with dependency ordering.
     * @detail Execution contract:
     * - Single-threaded: jobs run serially, in creation order, never
     *   concurrently, on the thread that calls execute(). The Scheduler is not
     *   thread-safe and must be driven from one thread at a time. Tasks posted
     *   with postJobToMarket() are separate: they are executed by the owned
     *   WorkerPool and do not participate in the Job/dependency model.
     * - execute() runs every ready, incomplete job once per call. A job whose
     *   dependencies are not all completed is skipped until a later pass.
     * - A task returning > 0 completes the job, < 0 cancels it; both run exit()
     *   and count as finished for run()/jobCount(). Returning 0 keeps the job
     *   scheduled for the next pass (a repeating/frame job).
     * - Mutations from inside a task or exit callback (createJob,
     *   destroyJob, exitAll, dependsOn) are deferred until the pass finishes,
     *   so re-entrant use does not invalidate iterators or references.
     *   execute() itself is not re-entrant and throws if called within a pass.
     * - Exceptions thrown by a task propagate out of execute(); the job is not
     *   marked finished and runs again on a later pass. Deferred mutations are
     *   still flushed so the scheduler stays consistent.
     * - exitAll()/clear() run exit() once per live job and drain the scheduler.
     * - Job ordering is deterministic: within a pass, ready jobs run in the
     *   order they were created.
     * @note Ownership: Owned by `Application` (m_scheduler, by value). It
     * uniquely owns every Job and the lazily created WorkerPool.
     * Application::unloadGame() drains jobs and stops workers before
     * destroying frameworks/containers, so nothing outlives the objects it
     * captures.
     * @example
     * @code
     * Scheduler scheduler;
     * JobID init = scheduler.createJob([] { return 1; });
     * JobID loop = scheduler.createJob([&] { return game.mainloop(); });
     * scheduler.dependsOn(loop, init);
     * while (!scheduler.done()) scheduler.execute();
     * @endcode
     */
    class Scheduler
    {
    public:
        Scheduler() = default;
        ~Scheduler() = default;

        Scheduler(const Scheduler&) = delete;
        Scheduler& operator=(const Scheduler&) = delete;

        Scheduler(Scheduler&& other) noexcept;
        Scheduler& operator=(Scheduler&& other) noexcept;

        /**
         * @brief Registers a new job and returns its JobID.
         * @param task Callable invoked by execute(); its result decides fate.
         * @param exit Optional callback run exactly once when the job leaves
         *        the scheduler (complete, cancel, or destroy).
         * @param name Name of the job.
         * @return A fresh, unique JobID assigned immediately. During an
         *        execute() pass the job is only scheduled after the pass ends.
         */
        JobID createJob(std::function<int()> task, std::function<void()> exit = nullptr, const std::string& name = "");

        /**
         * @brief Returns a pointer to the job with the id, or nullptr.
         * @param id The JobID to look up.
         * @return Pointer into the Scheduler-owned vector. Valid only until the
         *         next mutation (createJob/destroyJob/exitAll/execute); it is a
         *         raw pointer, not a lifetime-checked handle.
         */
        Job* findJob(JobID id);

        /**
         * @brief Cancels a job: runs its exit() and removes it from scheduling.
         * @param id The JobID to destroy.
         * @note During execute() the removal is deferred until the pass ends.
         *       The id is also stripped from every remaining job's dependency
         *       list and from the completed set.
         */
        void destroyJob(JobID id);

        /**
         * @brief Runs exit() on every live job and drains the scheduler.
         * @note Jobs created by exit() callbacks are drained too. During an
         *       execute() pass the clear is deferred until the pass ends.
         */
        void exitAll();

        /**
         * @brief Advances the scheduler: runs each ready, incomplete job once.
         * @note Not re-entrant: calling this from inside a task throws.
         */
        void execute();

        /**
         * @brief Alias for exitAll(): destroys every job and clears state.
         */
        void clear();

        /**
         * @brief Makes job `id` wait until `dependency` has completed.
         * @param id The JobID that must wait.
         * @param dependency The JobID it depends on.
         * @throws std::runtime_error on self-dependency, missing ids, or a
         *         newly introduced dependency cycle.
         */
        void dependsOn(JobID id, JobID dependency);

        /**
         * @brief Returns the worker pool, creating it on first use.
         * @param workerCount Worker count used when creating the pool; 0 uses
         *        the WorkerPool default (hardware concurrency, minimum 1).
         *        Ignored once the pool exists.
         * @return The scheduler-owned pool.
         * @note Worker tasks must not touch main-thread-only engine state
         * (Container, Module, ResourceRegistry, EventBus, Vulkan).
         */
        WorkerPool& workerPool(std::size_t workerCount = 0);

        /**
         * @brief Returns the worker pool without creating it.
         * @return The pool, or nullptr when no pool has been created yet.
         * @note Safe to call from the main thread for introspection UIs.
         */
        [[nodiscard]] WorkerPool* workerPoolIfCreated() noexcept
        {
            return m_workerPool.get();
        }

        /**
         * @brief Returns the worker pool without creating it.
         * @return The pool, or nullptr when no pool has been created yet.
         */
        [[nodiscard]] const WorkerPool* workerPoolIfCreated() const noexcept
        {
            return m_workerPool.get();
        }

        /**
         * @brief Posts a fire-and-forget task to the pool's JobMarket.
         * @param task Callable run on a worker thread.
         * @param name Optional human-readable label.
         * @return False when the current pool has been stopped.
         * @note Does not create a Scheduler Job and does not participate in
         * JobID, dependency, or completion tracking.
         */
        bool postJobToMarket(
            std::function<void()> task,
            std::string name = {}
        );

        /**
         * @brief Stops and destroys the worker pool, if any.
         * @detail Waits for in-flight work and discards jobs that have not
         * been hired. The pool is recreated lazily by workerPool().
         * @note Invalidates any reference previously returned by workerPool().
         * Must not be called from a worker thread.
         */
        void stopWorkers();

        [[nodiscard]] std::size_t jobCount() const { return m_jobs.size(); }
        [[nodiscard]] std::vector<Job> jobs() const { return m_jobs; }
        [[nodiscard]] std::size_t completedJobCount() const { return m_jobs_completed.size(); }

        /**
         * @brief True when every live job is finished (completed or cancelled).
         */
        [[nodiscard]] bool done() const
        {
            return m_jobs_completed.size() >= m_jobs.size();
        }

    private:
        Job* findJobImpl(JobID id);
        const Job* findJobConst(JobID id) const;
        bool isCompleted(JobID id) const;
        bool dependsOnCycle(JobID id, JobID dependency) const;
        void applyDependency(JobID id, JobID dependency);
        void destroyJobNow(JobID id);
        void drainAll();
        void flushPending();

        std::vector<Job> m_jobs;
        std::vector<JobID> m_jobs_completed;
        std::vector<Job> m_pending_adds;
        std::vector<JobID> m_pending_removes;
        std::unique_ptr<WorkerPool> m_workerPool;
        JobID m_nextID = 0;
        bool m_iterating = false;
        bool m_pending_clear = false;
    };
}