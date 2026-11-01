#include "LavaEngine/Window.hpp"

#include <stdexcept>

namespace LavaEngine
{
    namespace
    {
        // Extracted so glfwInit()/glfwCreateWindow() can run as part of
        // m_window's initializer, instead of in the constructor body.
        // Members construct in declaration order (m_window before
        // m_inputHandler) - if this ran in the body instead, m_inputHandler
        // would already have been constructed with m_window still null
        // (its default member initializer), and InputHandler caches that
        // null GLFWwindow* forever since it doesn't hold a live reference
        // back to Window.
        GLFWwindow* createGlfwWindow(
            int width,
            int height,
            const std::string& name
        )
        {
            if (!glfwInit())
                throw std::runtime_error(
                    "Failed to initialize GLFW"
                );

            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

            GLFWwindow* window = glfwCreateWindow(
                width,
                height,
                name.c_str(),
                nullptr,
                nullptr
            );

            if (!window)
            {
                glfwTerminate();

                throw std::runtime_error(
                    "Failed to create GLFW window"
                );
            }

            return window;
        }
    }

    Window::Window(
        int width,
        int height,
        const std::string& name
    )
        : m_window(createGlfwWindow(width, height, name)),
          m_inputHandler(*this)
    {
    }

    Window::~Window()
    {
        if (m_window)
            glfwDestroyWindow(m_window);

        glfwTerminate();
    }

    GLFWwindow* Window::getGlfwWindow() const
    {
        return m_window;
    }

    std::vector<const char*>
    Window::getRequiredInstanceExtensions()
    {
        uint32_t count = 0;

        const char** extensions =
            glfwGetRequiredInstanceExtensions(&count);

        if (!extensions)
            throw std::runtime_error(
                "Failed to get GLFW Vulkan extensions"
            );

        return {
            extensions,
            extensions + count
        };
    }

    int Window::getWidth() const
    {
        int width = 0;
        glfwGetWindowSize(m_window, &width, nullptr);
        return width;
    }

    int Window::getHeight() const
    {
        int height = 0;
        glfwGetWindowSize(m_window, nullptr, &height);
        return height;
    }

}