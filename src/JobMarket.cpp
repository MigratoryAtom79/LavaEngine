#include "../include/LavaEngine/JobMarket.hpp"

namespace LavaEngine
{
    JobMarket::JobMarket(std::size_t capacity)
        : m_capacity(capacity)
    {
    }

    bool JobMarket::post(WorkItem item)
    {
        {
            std::unique_lock<std::mutex> lock(m_mutex);

            m_notFull.wait(
                lock,
                [this]
                {
                    return m_closed || hasRoom();
                }
            );

            if (m_closed)
                return false;

            m_jobs.push_back(std::move(item));
        }

        m_cv.notify_one();

        return true;
    }

    bool JobMarket::tryPost(WorkItem item)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            if (m_closed || !hasRoom())
                return false;

            m_jobs.push_back(std::move(item));
        }

        m_cv.notify_one();

        return true;
    }

    bool JobMarket::tryTake(WorkItem& out)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            if (m_jobs.empty())
                return false;

            out = std::move(m_jobs.front());
            m_jobs.pop_front();
        }

        m_notFull.notify_one();

        return true;
    }

    bool JobMarket::waitTake(WorkItem& out)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        m_cv.wait(
            lock,
            [this]
            {
                return m_closed || !m_jobs.empty();
            }
        );

        if (m_jobs.empty())
            return false;

        out = std::move(m_jobs.front());
        m_jobs.pop_front();

        lock.unlock();
        m_notFull.notify_one();

        return true;
    }

    void JobMarket::close()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            m_closed = true;
            m_jobs.clear();
        }

        m_cv.notify_all();
        m_notFull.notify_all();
    }

    bool JobMarket::closed() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        return m_closed;
    }

    std::size_t JobMarket::size() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        return m_jobs.size();
    }

    bool JobMarket::empty() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        return m_jobs.empty();
    }

    std::size_t JobMarket::capacity() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        return m_capacity;
    }

    std::vector<std::string> JobMarket::queuedNames() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        std::vector<std::string> names;
        names.reserve(m_jobs.size());

        for (const WorkItem& job : m_jobs)
            names.push_back(job.name);

        return names;
    }
}
