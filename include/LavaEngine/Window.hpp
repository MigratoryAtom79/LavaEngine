#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../../LavaVK/include/LavaVK/LavaVK.hpp"
#include "../../external/glfw-3.5.1//include/GLFW/glfw3.h"

#include "InputHandler.hpp"

namespace LavaEngine
{
    using namespace LavaVK;

    /**
     * @brief Owns a native GLFW window and its input state.
     * @detail Creates a Vulkan-capable GLFW window and an InputHandler bound
     * to it. Non-copyable and non-movable.
     * @note Ownership: Owned by `VulkanRenderer` as a value member. Window
     * owns the native `GLFWwindow` (destroyed in ~Window) and its
     * `InputHandler`. GLFW initialization/termination is process-wide; the
     * destructor calls glfwTerminate(), so exactly one Window may exist per
     * process. The type is immovable so ownership cannot transfer between
     * instances and a stale source object cannot tear down the live window.
     * @example
     * @code
     * Window window(1280, 720, "Game");
     * while (!window.shouldClose()) { window.getInputHandler().update(); }
     * @endcode
     */
    class Window
    {
    public:
        Window(int width, int height, const std::string& name);

        ~Window();

        // Copying and moving are not allowed: Windows own process-wide GLFW
        // state and the library supports exactly one Window per process.
        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;
        Window(Window&&) = delete;
        Window& operator=(Window&&) = delete;
        [[nodiscard]] int getWidth() const;
        [[nodiscard]] int getHeight() const;

        [[nodiscard]]
        GLFWwindow* getGlfwWindow() const;

        static std::vector<const char*> getRequiredInstanceExtensions();

        [[nodiscard]]
        InputHandler& getInputHandler()
        {
            return m_inputHandler;
        }

        [[nodiscard]]
        const InputHandler& getInputHandler() const
        {
            return m_inputHandler;
        }

        [[nodiscard]]
        bool shouldClose() const
        {
            return glfwWindowShouldClose(getGlfwWindow());
        }

        [[nodiscard]]
        float getTime() const
        {
            return static_cast<float>(glfwGetTime());
        }

        float tick()
        {
            const float dt = getTime() - m_lastFrameTime;
            m_lastFrameTime = getTime();
            return dt;
        }

    private:
        GLFWwindow* m_window = nullptr;
        InputHandler m_inputHandler;
        float m_lastFrameTime = 0;
    };
}