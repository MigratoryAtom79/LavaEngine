#include "../include/LavaEngine/InputHandler.hpp"

#include "../include/LavaEngine/Window.hpp"

namespace LavaEngine
{
    InputHandler::InputHandler(Window& window)
        : m_window(window.getGlfwWindow())
    {
        double x = 0.0;
        double y = 0.0;
        glfwGetCursorPos(m_window, &x, &y);

        m_mousePosition = {static_cast<float>(x), static_cast<float>(y)};
        m_previousMousePosition = m_mousePosition;
    }

    InputHandler::InputHandler(InputHandler&& other) noexcept
        : m_window(other.m_window),
          m_currentKeys(other.m_currentKeys),
          m_previousKeys(other.m_previousKeys),
          m_currentMouseButtons(other.m_currentMouseButtons),
          m_previousMouseButtons(other.m_previousMouseButtons),
          m_mousePosition(other.m_mousePosition),
          m_previousMousePosition(other.m_previousMousePosition),
          m_mouseDelta(other.m_mouseDelta),
          m_firstUpdate(other.m_firstUpdate),
          m_focused(other.m_focused)
    {
        other.m_window = nullptr;
    }

    InputHandler& InputHandler::operator=(InputHandler&& other) noexcept
    {
        if (this == &other)
            return *this;

        m_window = other.m_window;
        m_currentKeys = other.m_currentKeys;
        m_previousKeys = other.m_previousKeys;
        m_currentMouseButtons = other.m_currentMouseButtons;
        m_previousMouseButtons = other.m_previousMouseButtons;
        m_mousePosition = other.m_mousePosition;
        m_previousMousePosition = other.m_previousMousePosition;
        m_mouseDelta = other.m_mouseDelta;
        m_firstUpdate = other.m_firstUpdate;
        m_focused = other.m_focused;

        other.m_window = nullptr;

        return *this;
    }

    void InputHandler::update()
    {
        glfwPollEvents();
        if (!m_focused) return;
        if (!m_window) return;
        m_previousKeys = m_currentKeys;
        m_previousMouseButtons = m_currentMouseButtons;
        m_previousMousePosition = m_mousePosition;

        for (int key = 0; key < KeyCount; ++key)
            m_currentKeys[key] = glfwGetKey(m_window, key) == GLFW_PRESS;

        for (int button = 0; button < MouseButtonCount; ++button)
            m_currentMouseButtons[button] = glfwGetMouseButton(m_window, button) == GLFW_PRESS;

        double x = 0.0;
        double y = 0.0;
        glfwGetCursorPos(m_window, &x, &y);
        m_mousePosition = {static_cast<float>(x), static_cast<float>(y)};

        if (m_firstUpdate)
        {
            // Avoids a large spurious delta on the very first frame,
            // before any previous position has actually been observed.
            m_previousMousePosition = m_mousePosition;
            m_firstUpdate = false;
        }

        m_mouseDelta = m_mousePosition - m_previousMousePosition;
    }

    bool InputHandler::isKeyDown(int key) const
    {
        if (!m_focused) return false;
        if (key < 0 || key >= KeyCount)
            return false;

        return m_currentKeys[key];
    }

    bool InputHandler::isKeyPressed(int key) const
    {
        if (!m_focused) return false;
        if (key < 0 || key >= KeyCount)
            return false;

        return m_currentKeys[key] && !m_previousKeys[key];
    }

    bool InputHandler::isKeyReleased(int key) const
    {
        if (!m_focused) return false;
        if (key < 0 || key >= KeyCount)
            return false;

        return !m_currentKeys[key] && m_previousKeys[key];
    }

    bool InputHandler::isMouseButtonDown(int button) const
    {
        if (!m_focused) return false;
        if (button < 0 || button >= MouseButtonCount)
            return false;

        return m_currentMouseButtons[button];
    }

    bool InputHandler::isMouseButtonPressed(int button) const
    {
        if (!m_focused) return false;

        if (button < 0 || button >= MouseButtonCount)
            return false;

        return m_currentMouseButtons[button] && !m_previousMouseButtons[button];
    }

    bool InputHandler::isMouseButtonReleased(int button) const
    {
        if (!m_focused) return false;

        if (button < 0 || button >= MouseButtonCount)
            return false;

        return !m_currentMouseButtons[button] && m_previousMouseButtons[button];
    }

    void InputHandler::setCursorMode(int mode) const
    {
        if (!m_window)
            return;

        glfwSetInputMode(m_window, GLFW_CURSOR, mode);
    }
}