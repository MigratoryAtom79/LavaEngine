#include "../include/LavaEngine/Scheduler.hpp"

#include <algorithm>
#include <stdexcept>

namespace LavaEngine
{
    JobID Scheduler::createJob(
        std::function<int()> task,
        std::function<void()> exit,
        const std::string& name
    )
    {
        JobID id = m_nextID++;

        if (m_iterating)
        {
            m_pending_adds.push_back(
                Job{
                    .id = id,
                    .task = std::move(task),
                    .exit = std::move(exit),
                    .name = name
                }
            );
        }
        else
        {
            m_jobs.push_back(
                Job{
                    .id = id,
                    .task = std::move(task),
                .exit = std::move(exit),
                .name = name
                }
            );
        }

        return id;
    }

    Scheduler::Scheduler(Scheduler&& other) noexcept
        : m_jobs(std::move(other.m_jobs)),
          m_jobs_completed(std::move(other.m_jobs_completed)),
          m_pending_adds(std::move(other.m_pending_adds)),
          m_pending_removes(std::move(other.m_pending_removes)),
          m_nextID(other.m_nextID),
          m_iterating(other.m_iterating),
          m_pending_clear(other.m_pending_clear)
    {
        other.m_nextID = 0;
        other.m_iterating = false;
        other.m_pending_clear = false;
    }

    Scheduler& Scheduler::operator=(Scheduler&& other) noexcept
    {
        if (this == &other)
            return *this;

        m_jobs = std::move(other.m_jobs);
        m_jobs_completed = std::move(other.m_jobs_completed);
        m_pending_adds = std::move(other.m_pending_adds);
        m_pending_removes = std::move(other.m_pending_removes);
        m_nextID = other.m_nextID;
        m_iterating = other.m_iterating;
        m_pending_clear = other.m_pending_clear;

        other.m_nextID = 0;
        other.m_iterating = false;
        other.m_pending_clear = false;

        return *this;
    }

    Job* Scheduler::findJob(JobID id)
    {
        return findJobImpl(id);
    }

    Job* Scheduler::findJobImpl(JobID id)
    {
        for (auto& job : m_jobs)
        {
            if (job.id == id)
                return &job;
        }

        return nullptr;
    }

    const Job* Scheduler::findJobConst(JobID id) const
    {
        for (const auto& job : m_jobs)
        {
            if (job.id == id)
                return &job;
        }

        return nullptr;
    }

    bool Scheduler::isCompleted(JobID id) const
    {
        return std::find(
            m_jobs_completed.begin(),
            m_jobs_completed.end(),
            id
        ) != m_jobs_completed.end();
    }

    bool Scheduler::dependsOnCycle(JobID id, JobID dependency) const
    {
        std::vector<JobID> stack{ dependency };
        std::vector<JobID> visited;

        while (!stack.empty())
        {
            JobID current = stack.back();
            stack.pop_back();

            if (current == id)
                return true;

            if (std::find(
                visited.begin(),
                visited.end(),
                current
            ) != visited.end())
            {
                continue;
            }

            visited.push_back(current);

            const Job* job = findJobConst(current);

            if (!job)
                continue;

            for (JobID dep : job->dependencies)
            {
                stack.push_back(dep);
            }
        }

        return false;
    }

    void Scheduler::applyDependency(JobID id, JobID dependency)
    {
        if (id == dependency)
            throw std::runtime_error(
                "A job cannot depend on itself"
            );

        if (!findJobImpl(id))
            throw std::runtime_error(
                "Invalid JobID"
            );

        if (!findJobImpl(dependency))
            throw std::runtime_error(
                "Invalid dependency JobID"
            );

        Job* job = findJobImpl(id);

        if (std::find(
            job->dependencies.begin(),
            job->dependencies.end(),
            dependency
        ) != job->dependencies.end())
        {
            return;
        }

        if (dependsOnCycle(id, dependency))
            throw std::runtime_error(
                "Dependency cycle detected"
            );

        job->dependencies.push_back(dependency);
    }

    void Scheduler::dependsOn(
        JobID id,
        JobID dependency
    )
    {
        applyDependency(id, dependency);
    }

    void Scheduler::destroyJobNow(JobID id)
    {
        auto it = std::find_if(
            m_jobs.begin(),
            m_jobs.end(),
            [id](const Job& job)
            {
                return job.id == id;
            }
        );

        if (it == m_jobs.end())
            return;

        // Move the job out first so exit() re-entry cannot invalidate
        // the iterator we are about to erase.
        Job job = std::move(*it);
        m_jobs.erase(it);

        // Remove this job from dependency lists.
        for (auto& remaining : m_jobs)
        {
            std::erase(remaining.dependencies, id);
        }

        // Remove from completed jobs.
        std::erase(m_jobs_completed, id);

        // Finally run exit() on the moved-out copy.
        if (job.exit)
            job.exit();
    }

    void Scheduler::destroyJob(JobID id)
    {
        if (m_iterating)
        {
            if (std::find(
                m_pending_removes.begin(),
                m_pending_removes.end(),
                id
            ) == m_pending_removes.end())
            {
                m_pending_removes.push_back(id);
            }
            return;
        }

        destroyJobNow(id);
    }

    void Scheduler::drainAll()
    {
        while (!m_jobs.empty())
        {
            Job job = std::move(m_jobs.back());
            m_jobs.pop_back();

            if (job.exit)
                job.exit();
        }
    }

    void Scheduler::exitAll()
    {
        if (m_iterating)
        {
            m_pending_clear = true;
            return;
        }

        drainAll();

        m_jobs_completed.clear();
        m_pending_adds.clear();
        m_pending_removes.clear();
    }

    void Scheduler::flushPending()
    {
        if (m_pending_clear)
        {
            m_pending_clear = false;
            m_jobs_completed.clear();
            m_pending_adds.clear();
            m_pending_removes.clear();
            drainAll();
            return;
        }

        for (Job& job : m_pending_adds)
        {
            m_jobs.push_back(std::move(job));
        }
        m_pending_adds.clear();

        for (JobID id : m_pending_removes)
        {
            destroyJobNow(id);
        }
        m_pending_removes.clear();
    }

    void Scheduler::execute()
    {
        if (m_iterating)
            throw std::runtime_error("execute() is not re-entrant");

        m_iterating = true;

        try
        {
            const std::size_t count = m_jobs.size();

            for (std::size_t i = 0; i < count; ++i)
            {
                Job& job = m_jobs[i];

                // A completed job doesn't execute again.
                if (isCompleted(job.id))
                    continue;

                // A job destroyed during this pass is removed at flush time.
                if (std::find(
                    m_pending_removes.begin(),
                    m_pending_removes.end(),
                    job.id
                ) != m_pending_removes.end())
                {
                    continue;
                }

                bool ready = true;

                for (JobID dependency : job.dependencies)
                {
                    if (!isCompleted(dependency))
                    {
                        ready = false;
                        break;
                    }
                }

                if (!ready)
                    continue;

                auto start = std::chrono::high_resolution_clock::now();
                const int result = job.task();
                auto end = std::chrono::high_resolution_clock::now();
                job.duration =
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        end - start
                    );

                // Returning 0 keeps the job scheduled; > 0 completes it and
                // < 0 cancels it. Both terminal results count as finished and
                // run exit() exactly once.
                if (result != 0)
                {
                    m_jobs_completed.push_back(job.id);

                    if (job.exit)
                    {
                        auto exitFn = std::move(job.exit);
                        exitFn();
                    }
                }
            }
        }
        catch (...)
        {
            m_iterating = false;
            flushPending();
            throw;
        }

        m_iterating = false;

        flushPending();
    }

    void Scheduler::clear()
    {
        exitAll();
    }
}