#include "../include/LavaEngine/TaskGroup.hpp"

namespace LavaEngine
{
    TaskGroup::TaskGroup(std::size_t count)
        : m_state(std::make_shared<State>())
    {
        m_state->remaining = count;
    }

    void TaskGroup::done()
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);

        if (m_state->remaining == 0)
            return;

        --m_state->remaining;

        if (m_state->remaining == 0)
            m_state->cv.notify_all();
    }

    void TaskGroup::wait()
    {
        std::unique_lock<std::mutex> lock(m_state->mutex);

        m_state->cv.wait(
            lock,
            [this]
            {
                return m_state->remaining == 0;
            }
        );
    }

    void TaskGroup::add(std::size_t count)
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);

        m_state->remaining += count;
    }

    std::size_t TaskGroup::remaining() const
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);

        return m_state->remaining;
    }
}
