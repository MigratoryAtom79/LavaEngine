#include "../include/LavaEngine/WorkerPool.hpp"

#include <cstdio>

namespace LavaEngine
{
    namespace
    {
        thread_local bool t_isWorkerThread = false;
    }

    WorkerPool::WorkerPool(
        std::size_t workerCount,
        ErrorHandler onError,
        std::size_t marketCapacity
    )
        : m_market(marketCapacity),
          m_onError(std::move(onError))
    {
        if (workerCount == 0)
            workerCount = 1;

        m_status.reserve(workerCount);
        m_workers.reserve(workerCount);

        for (std::size_t i = 0; i < workerCount; ++i)
        {
            m_status.push_back(
                WorkerSnapshot{
                    .id = i,
                    .busy = false,
                    .jobName = {}
                }
            );
        }

        try
        {
            for (std::size_t i = 0; i < workerCount; ++i)
            {
                m_workers.emplace_back(
                    [this, i]
                    {
                        run(i);
                    }
                );
            }
        }
        catch (...)
        {
            m_market.close();

            for (std::thread& worker : m_workers)
            {
                if (worker.joinable())
                    worker.join();
            }

            throw;
        }
    }

    WorkerPool::~WorkerPool()
    {
        stop();
    }

    bool WorkerPool::post(JobMarket::WorkItem item)
    {
        return m_market.post(std::move(item));
    }

    bool WorkerPool::post(
        std::function<void()> task,
        std::string name
    )
    {
        return post(
            JobMarket::WorkItem{
                std::move(name),
                std::move(task)
            }
        );
    }

    void WorkerPool::stop()
    {
        std::call_once(
            m_stopOnce,
            [this]
            {
                m_stopped.store(true, std::memory_order_release);

                m_market.close();

                for (std::thread& worker : m_workers)
                {
                    if (worker.joinable())
                        worker.join();
                }
            }
        );
    }

    bool WorkerPool::stopped() const
    {
        return m_stopped.load(std::memory_order_acquire);
    }

    std::size_t WorkerPool::workerCount() const
    {
        return m_workers.size();
    }

    std::size_t WorkerPool::idleWorkerCount() const
    {
        return m_idle.load(std::memory_order_relaxed);
    }

    std::vector<WorkerPool::WorkerSnapshot> WorkerPool::workerSnapshots() const
    {
        std::lock_guard<std::mutex> lock(m_statusMutex);

        return m_status;
    }

    JobMarket& WorkerPool::market()
    {
        return m_market;
    }

    const JobMarket& WorkerPool::market() const
    {
        return m_market;
    }

    bool WorkerPool::currentThreadIsWorker()
    {
        return t_isWorkerThread;
    }

    void WorkerPool::run(std::size_t id)
    {
        t_isWorkerThread = true;

        JobMarket::WorkItem item;

        while (true)
        {
            m_idle.fetch_add(1, std::memory_order_relaxed);

            const bool hired = m_market.waitTake(item);

            m_idle.fetch_sub(1, std::memory_order_relaxed);

            if (!hired)
            {
                t_isWorkerThread = false;
                return;
            }

            {
                std::lock_guard<std::mutex> lock(m_statusMutex);

                if (id < m_status.size())
                {
                    m_status[id].busy = true;
                    m_status[id].jobName = item.name;
                }
            }

            try
            {
                if (item.task)
                    item.task();
            }
            catch (...)
            {
                handleError(std::current_exception());
            }

            {
                std::lock_guard<std::mutex> lock(m_statusMutex);

                if (id < m_status.size())
                {
                    m_status[id].busy = false;
                    m_status[id].jobName.clear();
                }
            }
        }
    }

    void WorkerPool::handleError(std::exception_ptr error) const
    {
        try
        {
            if (m_onError)
            {
                m_onError(error);
                return;
            }

            if (error)
                std::rethrow_exception(error);
        }
        catch (const std::exception& e)
        {
            std::fprintf(
                stderr,
                "LavaEngine::WorkerPool: unhandled exception: %s\n",
                e.what()
            );

            return;
        }
        catch (...)
        {
            std::fprintf(
                stderr,
                "LavaEngine::WorkerPool: unhandled unknown exception\n"
            );

            return;
        }

        std::fprintf(
            stderr,
            "LavaEngine::WorkerPool: unhandled empty exception\n"
        );
    }
}
