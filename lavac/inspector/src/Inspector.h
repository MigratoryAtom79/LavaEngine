#pragma once

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <vector>

#if defined(__GNUG__)
#include <cxxabi.h>
#endif

#include <vulkan/vulkan.h>

#include <../../external/imgui/imgui.h>
#include <../../external/imgui/backends/imgui_impl_glfw.h>
#include <../../external/imgui/backends/imgui_impl_vulkan.h>
#include <../../external/imgui/misc/cpp/imgui_stdlib.h>

#include "../../src/lavac.h"

#include <LavaEngine/Logger.hpp>

namespace LavaEngine
{
    /**
     * @brief Dear ImGui editor attached to a running UserGame session.
     * @detail Draws ImGui panels (explorer, logger, viewport) over the
     * engine's VulkanRenderer while stepping the game.
     * @note Ownership: Owned by its creator (typically on the stack in
     * main). It borrows, non-owning, the UserGame/Application and the
     * VulkanRenderer. The cached renderer pointer must be re-resolved with
     * Application::findFramework<VulkanRenderer>() after a hot reload,
     * because unloadGame() resets the framework.
     * @example
     * @code
     * Inspector inspector(game);
     * inspector.inspect();
     * @endcode
     */
    class Inspector
    {
    public:
        explicit Inspector(UserGame& application)
            : m_application(application)
        {
            try
            {
                initialize();
            }
            catch (...)
            {
                detach();
                throw;
            }

            m_application.setReloadHooks(
                [this] { detach(); },
                [this]
                {
                    try
                    {
                        initialize();
                    }
                    catch (...)
                    {
                        detach();
                        throw;
                    }
                }
            );
        }

        ~Inspector()
        {
            m_application.clearReloadHooks();
            shutdown();
        }

        Inspector(const Inspector&) = delete;
        Inspector& operator=(const Inspector&) = delete;

        void inspect()
        {
            m_lastFrameTime = Clock::now();

            while (true)
            {
                if (!vulkan)
                {
                    std::fprintf(
                        stderr,
                        "Inspector: no renderer after reload; stopping.\n"
                    );
                    break;
                }

                const Clock::time_point frameStart =
                    Clock::now();

                glfwPollEvents();

                if (vulkan->window().shouldClose())
                    break;

                sampleFrameTime();

                beginFrame();

                draw();

                ImGui::Render();

                if (!paused)
                {
                    const int stepResult =
                        m_application.step();

                    const bool hasJobs =
                        m_scheduler &&
                        m_scheduler->jobCount() > 0;

                    if (stepResult &&
                        (hasJobs ||
                         m_application.application().exitRequested()))
                    {
                        break;
                    }
                }

                m_application.application().waitForNextFrame(
                    frameStart
                );
            }
        }

        // Uninstalls from the renderer and shuts down the ImGui backends.
        // Idempotent; reload hooks use it before the old framework is destroyed.
        void detach()
        {
            // Wait for GPU work before destroying ImGui's Vulkan objects.
            if (vulkan)
            {
                vkDeviceWaitIdle(
                    vulkan->device().native()
                );

                destroyViewportTarget();

                vulkan->clearOverlayCallback();
            }

            if (ImGui::GetCurrentContext())
            {
                ImGui_ImplVulkan_Shutdown();

                ImGui_ImplGlfw_Shutdown();

                ImGui::DestroyContext();
            }

            // The descriptor pool belongs to the old device and must die
            // before unloadGame() resets the framework.
            m_imguiPool.reset();

            vulkan = nullptr;
        }

    private:
        void initialize()
        {
            vulkan =
                m_application.application()
                             .findFramework<VulkanRenderer>();

            if (!vulkan)
            {
                throw std::runtime_error(
                    "Inspector requires VulkanRenderer"
                );
            }

            m_frameTimesMs.fill(0.0f);
            m_frameTimeCursor = 0;
            m_frameTimeSamples = 0;
            m_jobHistoryMs.clear();
            m_jobHistoryCursor.clear();
            m_jobHistorySamples = 0;

            IMGUI_CHECKVERSION();

            ImGui::CreateContext();

            ImGuiIO& io = ImGui::GetIO();

            io.ConfigFlags |=
                ImGuiConfigFlags_DockingEnable;

            if (!ImGui_ImplGlfw_InitForVulkan(
                vulkan->window().getGlfwWindow(),
                true))
            {
                ImGui::DestroyContext();

                throw std::runtime_error(
                    "Failed to initialize ImGui GLFW backend"
                );
            }

            ImGui_ImplVulkan_InitInfo initInfo{};

            initInfo.Instance =
                vulkan->instance().native();

            initInfo.PhysicalDevice =
                vulkan->device().physical();

            initInfo.Device =
                vulkan->device().native();

            initInfo.QueueFamily =
                vulkan->device().getQueueFamily(
                    QueueType::GRAPHICS
                );

            initInfo.Queue =
                vulkan->device()
                      .getQueue(QueueType::GRAPHICS)
                      .native();

            m_imguiPool =
                DescriptorPool::Builder(vulkan->device())
                .addPoolSize(DescriptorType::SampledImage, 16)
                .addPoolSize(DescriptorType::Sampler, 4)
                .setPoolFlags(VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT)
                .setMaxSets(20)
                .build();

            initInfo.DescriptorPool = m_imguiPool->native();

            initInfo.MinImageCount =
                vulkan->swapChain().imageCount();

            initInfo.ImageCount =
                vulkan->swapChain().imageCount();

            initInfo.PipelineInfoMain.RenderPass =
                vulkan->renderPass().native();

            initInfo.PipelineInfoMain.Subpass = 0;

            initInfo.PipelineInfoMain.MSAASamples =
                VK_SAMPLE_COUNT_1_BIT;

            if (!ImGui_ImplVulkan_Init(&initInfo))
            {
                ImGui_ImplGlfw_Shutdown();
                ImGui::DestroyContext();

                throw std::runtime_error(
                    "Failed to initialize ImGui Vulkan backend"
                );
            }

            // Scene render target for the "Viewport" panel. Sized to a
            // sensible default; resizeViewportTarget() will rebuild it to
            // match the panel's actual content region on the first draw.
            createViewportTarget(1280, 720);

            vulkan->setOverlayCallback(
                [](CommandBuffer& commandBuffer)
                {
                    ImDrawData* data = ImGui::GetDrawData();

                    if (!data)
                        return;

                    ImGui_ImplVulkan_RenderDrawData(
                        data,
                        commandBuffer.native()
                    );
                }
            );

            m_scheduler = m_application.application().getScheduler();

            ImGuiStyle& style = ImGui::GetStyle();

            // ==========================================
            // 1. SIZES & ROUNDING
            // ==========================================
            style.WindowRounding = 6.0f; // Round the corners of windows
            style.FrameRounding = 4.0f; // Round the corners of widgets (checkboxes, buttons)
            style.PopupRounding = 4.0f; // Round the corners of popups / tooltips
            style.GrabRounding = 4.0f; // Round the corners of sliders
            style.ScrollbarRounding = 12.0f; // Smooth capsule look for scrollbars
            style.TabRounding = 4.0f; // Round the corners of dockable tabs
            style.ItemSpacing = ImVec2(8, 6);

            // ==========================================
            // 2. FULL RED & CHARCOAL COLOR PALETTE
            // ==========================================

            // Core Text & Window Styles
            style.Colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.00f); // Clean off-white
            style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.45f, 0.45f, 1.00f); // Muted red-gray text
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f); // Dark charcoal background
            style.Colors[ImGuiCol_ChildBg] = ImVec4(0.03f, 0.03f, 0.03f, 1.00f); // Slightly darker sub-windows
            style.Colors[ImGuiCol_PopupBg] = ImVec4(0.06f, 0.06f, 0.06f, 1.00f); // Popups and dropdown blocks
            style.Colors[ImGuiCol_Border] = ImVec4(0.10f, 0.10f, 0.12f, 0.80f); // Soft crimson frame borders
            style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

            // Header & Title Styles
            style.Colors[ImGuiCol_TitleBg] = ImVec4(0.15f, 0.05f, 0.06f, 1.00f); // Deep maroon title background
            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.40f, 0.06f, 0.08f, 1.00f); // Prominent red active title
            style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.12f, 0.04f, 0.05f, 0.70f);
            style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.12f, 0.05f, 0.06f, 1.00f); // Top menu horizontal strip

            // Input Fields & Frames (Checkboxes, Inputs, Combos)
            style.Colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.08f, 0.09f, 1.00f); // Dark red-tinted input fields
            style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.26f, 0.12f, 0.14f, 1.00f);
            style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.36f, 0.15f, 0.18f, 1.00f);

            // Main Interactive Buttons
            style.Colors[ImGuiCol_Button] = ImVec4(0.55f, 0.08f, 0.12f, 1.00f); // Main structural crimson
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.75f, 0.12f, 0.16f, 1.00f); // Vibrant red hover
            style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.95f, 0.16f, 0.22f, 1.00f); // Clicking action highlight

            // Tree Nodes, Selectables & List Highlights
            style.Colors[ImGuiCol_Header] = ImVec4(0.45f, 0.08f, 0.10f, 1.00f); // Selectable items / tree blocks
            style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.65f, 0.10f, 0.14f, 1.00f);
            style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.85f, 0.14f, 0.18f, 1.00f);

            // Sliders, Checkmarks & Progress Bars
            style.Colors[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.16f, 0.22f, 1.00f); // Glowing red tick
            style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.60f, 0.10f, 0.14f, 1.00f); // Slider knob
            style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.85f, 0.14f, 0.18f, 1.00f);
            style.Colors[ImGuiCol_PlotLines] = ImVec4(0.85f, 0.14f, 0.18f, 1.00f); // Graphs lines
            style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.95f, 0.30f, 0.35f, 1.00f);
            style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.70f, 0.10f, 0.14f, 1.00f); // Histograms
            style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.90f, 0.15f, 0.20f, 1.00f);

            // Scrolling & Utility
            style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.08f, 0.04f, 0.04f, 1.00f); // Invisible/clean track match
            style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.25f, 0.10f, 0.12f, 1.00f); // Muted bar thumb
            style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.12f, 0.15f, 1.00f);
            style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.60f, 0.14f, 0.18f, 1.00f);

            // Window Management, Tab Bars, and Multi-Viewports
            style.Colors[ImGuiCol_Tab] = ImVec4(0.20f, 0.06f, 0.08f, 1.00f); // Dark background tabs
            style.Colors[ImGuiCol_TabHovered] = ImVec4(0.50f, 0.10f, 0.14f, 1.00f);
            style.Colors[ImGuiCol_TabActive] = ImVec4(0.65f, 0.12f, 0.16f, 1.00f); // Focused/front tab
            style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.12f, 0.04f, 0.05f, 1.00f);
            style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.30f, 0.06f, 0.08f, 1.00f);
            style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.45f, 0.08f, 0.10f, 0.50f); // Lower-right window triangles
            style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.75f, 0.12f, 0.16f, 0.70f);
            style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.95f, 0.16f, 0.22f, 0.90f);
            style.Colors[ImGuiCol_Separator] = ImVec4(0.25f, 0.10f, 0.12f, 1.00f); // Menu/layout splitting line
            style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.55f, 0.12f, 0.16f, 1.00f);
            style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.75f, 0.16f, 0.22f, 1.00f);

            // Grid Data Tables
            style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.22f, 0.08f, 0.10f, 1.00f); // Top row of data sheets
            style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.35f, 0.12f, 0.15f, 1.00f); // Outer structural grid grid
            style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.22f, 0.08f, 0.10f, 1.00f); // Inner subtle lines
            style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
            style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.03f); // 3% white zebra striping

            // Docking Preview & Backdrop
            style.Colors[ImGuiCol_DockingPreview] = ImVec4(0.75f, 0.12f, 0.16f, 0.60f);
            // Ghost blue alternative red block
            style.Colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.08f, 0.04f, 0.04f, 1.00f);
            style.Colors[ImGuiCol_DragDropTarget] = ImVec4(0.95f, 0.16f, 0.22f, 0.95f); // Dropping items over nodes
            style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.95f, 0.16f, 0.22f, 1.00f); // Gamepad/Keyboard ring marker
            style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.75f, 0.12f, 0.16f, 0.35f); // Text highlight background
            style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.08f, 0.04f, 0.04f, 0.55f);
            // Darkens app when modals appear
        }

        static void beginFrame()
        {
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
        }

        void draw()
        {
            // PassthruCentralNode leaves the region behind any *undocked*
            // area of the viewport transparent instead of painting an
            // opaque background over it. The scene itself is no longer
            // drawn full-screen underneath (it renders offscreen now and
            // is shown inside the Viewport panel below), but this still
            // keeps the dockspace from needlessly covering that area with
            // a flat background color.
            ImGui::DockSpaceOverViewport(
                0,
                nullptr,
                ImGuiDockNodeFlags_PassthruCentralNode
            );

            drawVulkanPanel();

            drawExplorerPanel();

            drawFilePanel();

            drawViewport();

            drawContainersPanel();

            drawPerformancePanel();

            drawLoggingPanel();

            if (vulkan->window().getInputHandler().isKeyPressed(GLFW_KEY_F1))
            {
                vulkan->window().getInputHandler().focus();
            }
        }


        /**
         * @brief Records how long the previous frame took, into a fixed-size
         * ring buffer used by #drawPerformancePanel for the frame time
         * graph and running average/min/max. Called once per iteration of
         * #inspect, before any drawing happens.
         */
        void sampleFrameTime()
        {
            const Clock::time_point now = Clock::now();

            const float ms =
                std::chrono::duration<float, std::milli>(
                    now - m_lastFrameTime
                ).count();

            m_lastFrameTime = now;

            m_frameTimesMs[m_frameTimeCursor] = ms;

            m_frameTimeCursor =
                (m_frameTimeCursor + 1) % m_frameTimesMs.size();

            m_frameTimeSamples = std::min<std::size_t>(
                m_frameTimeSamples + 1,
                m_frameTimesMs.size()
            );

            for (const Job& job : m_scheduler->jobs())
            {
                auto& history = m_jobHistoryMs[job.id]; // default-constructs a zeroed array on first sight
                const float ms = static_cast<float>(job.duration.count()) / 1000.0f;
                history[m_jobHistoryCursor[job.id]] = ms;
                m_jobHistoryCursor[job.id] = (m_jobHistoryCursor[job.id] + 1) % history.size();
            }

            m_jobHistorySamples = std::min<std::size_t>(m_jobHistorySamples + 1, 120);
        }


        /**
         * @brief Shows current/average/min/max frame time and FPS with a
         * rolling graph, plus scheduler job counts. Everything here is
         * measured by the Inspector itself (wall-clock time between
         * #inspect iterations) - the engine has no built-in profiler, so
         * this does not include a CPU/GPU breakdown or draw call counts.
         */
        void drawPerformancePanel()
        {
            if (ImGui::Begin("Performance"))
            {
                const std::size_t lastIndex =
                    (m_frameTimeCursor + m_frameTimesMs.size() - 1)
                    % m_frameTimesMs.size();

                const float currentMs = m_frameTimesMs[lastIndex];

                float sumMs = 0.0f;
                float minMs = FLT_MAX;
                float maxMs = 0.0f;

                for (std::size_t i = 0; i < m_frameTimeSamples; ++i)
                {
                    const float v = m_frameTimesMs[i];
                    sumMs += v;
                    minMs = std::min(minMs, v);
                    maxMs = std::max(maxMs, v);
                }

                const float avgMs =
                    m_frameTimeSamples > 0 ? sumMs / static_cast<float>(m_frameTimeSamples) : 0.0f;

                ImGui::Text(
                    "Frame time: %.2f ms  (%.1f FPS)",
                    currentMs,
                    currentMs > 0.0f ? 1000.0f / currentMs : 0.0f
                );

                ImGui::Text(
                    "Avg %.2f ms (%.1f FPS)   Min %.2f ms   Max %.2f ms",
                    avgMs,
                    avgMs > 0.0f ? 1000.0f / avgMs : 0.0f,
                    m_frameTimeSamples > 0 ? minMs : 0.0f,
                    maxMs
                );

                ImGui::PlotLines(
                    "##frame_times",
                    m_frameTimesMs.data(),
                    static_cast<int>(m_frameTimesMs.size()),
                    static_cast<int>(m_frameTimeCursor),
                    nullptr,
                    0.0f,
                    maxMs > 0.0f ? maxMs * 1.2f : 33.3f,
                    ImVec2(0, 80)
                );

                ImGui::Separator();

                if (m_scheduler)
                {
                    const std::size_t total = m_scheduler->jobCount();
                    const std::size_t completed = m_scheduler->completedJobCount();

                    ImGui::Text(
                        "Scheduler jobs: %zu total, %zu completed, %zu pending",
                        total,
                        completed,
                        total - completed
                    );

                    drawJobProfiler(currentMs);
                }
                else
                {
                    ImGui::TextDisabled("No scheduler available.");
                }
            }

            ImGui::End();
        }

        void drawJobProfiler(float frameDeltaMs)
        {
            const std::vector<Job>& jobs = m_scheduler->jobs();

            if (jobs.empty())
            {
                ImGui::TextDisabled("No jobs registered.");
                return;
            }

            // --- Timeline bar (execution order) ---

            float jobsTotalMs = 0.0f;

            struct Segment
            {
                JobID id;
                float ms;
                const std::string& name;
            };

            std::vector<Segment> segments;
            segments.reserve(jobs.size() + 1);

            for (const Job& job : jobs)
            {
                const float ms = static_cast<float>(job.duration.count()) / 1000.0f;
                segments.push_back({job.id, ms, job.name});
                jobsTotalMs += ms;
            }

            const float systemMs = std::max(0.0f, frameDeltaMs - jobsTotalMs);
            const float totalMs = jobsTotalMs + systemMs;
            const ImU32 systemColor = IM_COL32(120, 120, 120, 255);

            ImGui::Text(
                "Frame %.3f ms  (jobs %.3f ms + system %.3f ms)",
                frameDeltaMs,
                jobsTotalMs,
                systemMs
            );

            ImDrawList* drawList = ImGui::GetWindowDrawList();

            const ImVec2 barPos = ImGui::GetCursorScreenPos();
            const float barWidth = ImGui::GetContentRegionAvail().x;
            const float barHeight = 32.0f;

            ImGui::InvisibleButton("##job_timeline", ImVec2(barWidth, barHeight));

            if (totalMs > 0.0f)
            {
                float x = barPos.x;

                auto drawSegment = [&](float ms, ImU32 color, const std::string& name, bool isSystem)
                {
                    const float w = (ms / totalMs) * barWidth;

                    const ImVec2 p0(x, barPos.y);
                    const ImVec2 p1(x + w, barPos.y + barHeight);

                    drawList->AddRectFilled(p0, p1, color);
                    drawList->AddRect(p0, p1, IM_COL32(0, 0, 0, 80));

                    if (ImGui::IsMouseHoveringRect(p0, p1))
                    {
                        if (isSystem)
                        {
                            ImGui::SetTooltip("System - %.3f ms", ms);
                        }
                        else
                        {
                            ImGui::SetTooltip("Job %s - %.3f ms", name.c_str(), ms);
                        }
                    }

                    x += w;
                };

                for (const Segment& seg : segments)
                    drawSegment(seg.ms, colorForJobID(seg.id), seg.name, false);

                drawSegment(systemMs, systemColor, "System", true);
            }

            // --- History line chart (same job set, same colors) ---
            if (m_jobHistoryMs.empty())
            {
                ImGui::TextDisabled("No job history yet.");
                return;
            }

            float maxHistoryMs = 0.0f;
            for (const auto& [id, history] : m_jobHistoryMs)
                for (std::size_t i = 0; i < m_jobHistorySamples; ++i)
                    maxHistoryMs = std::max(maxHistoryMs, history[i]);

            if (maxHistoryMs <= 0.0f)
                maxHistoryMs = 1.0f;

            const ImVec2 plotSize(ImGui::GetContentRegionAvail().x, 150.0f);
            const ImVec2 topLeft = ImGui::GetCursorScreenPos();

            ImGui::InvisibleButton("##job_history_bg", plotSize);

            drawList->AddRectFilled(
                topLeft,
                ImVec2(topLeft.x + plotSize.x, topLeft.y + plotSize.y),
                IM_COL32(30, 30, 30, 255)
            );

            const std::size_t n = m_jobHistorySamples;

            if (n >= 2)
            {
                for (const auto& [id, history] : m_jobHistoryMs)
                {
                    const ImU32 color = colorForJobID(id);
                    const std::size_t cursor = m_jobHistoryCursor.at(id);

                    for (std::size_t i = 0; i + 1 < n; ++i)
                    {
                        const std::size_t idxA = (cursor + i) % history.size();
                        const std::size_t idxB = (cursor + i + 1) % history.size();

                        const float xA = topLeft.x + (static_cast<float>(i) / (n - 1)) * plotSize.x;
                        const float xB = topLeft.x + (static_cast<float>(i + 1) / (n - 1)) * plotSize.x;

                        const float yA = topLeft.y + plotSize.y - (history[idxA] / maxHistoryMs) * plotSize.y;
                        const float yB = topLeft.y + plotSize.y - (history[idxB] / maxHistoryMs) * plotSize.y;

                        drawList->AddLine(ImVec2(xA, yA), ImVec2(xB, yB), color, 2.5f);
                    }
                }
            }
            ImGui::Separator();

            // Shared legend - covers both the timeline bar above and the history
            // chart below, since both use colorForJobID for the same IDs.
            for (const Segment& seg : segments)
            {
                ImGui::ColorButton(
                    "##legend",
                    ImGui::ColorConvertU32ToFloat4(colorForJobID(seg.id)),
                    ImGuiColorEditFlags_NoTooltip,
                    ImVec2(12, 12)
                );
                ImGui::SameLine();
                ImGui::Text("Job %s - %.3f ms", seg.name.c_str(), seg.ms);
            }

            ImGui::ColorButton(
                "##legend_system",
                ImGui::ColorConvertU32ToFloat4(systemColor),
                ImGuiColorEditFlags_NoTooltip,
                ImVec2(12, 12)
            );
            ImGui::SameLine();
            ImGui::Text("System - %.3f ms", systemMs);


            ImGui::Text("Max: %.3f ms", maxHistoryMs);
        }

        static inline ImU32 colorForJobID(JobID id)
        {
            uint32_t x = id;
            x ^= x >> 16;
            x *= 0x7feb352dU;
            x ^= x >> 15;
            x *= 0x846ca68bU;
            x ^= x >> 16;

            const float hue = static_cast<float>(x % 360) / 360.0f;

            float r, g, b;
            ImGui::ColorConvertHSVtoRGB(hue, 0.65f, 0.95f, r, g, b);

            return IM_COL32(
                static_cast<int>(r * 255),
                static_cast<int>(g * 255),
                static_cast<int>(b * 255),
                255
            );
        }


        void drawContainersPanel()
        {
            if (ImGui::Begin("Containers & Events"))
            {
                if (ImGui::TreeNodeEx("Containers")){
                    const auto& containers =
                    m_application.application().containers();

                    if (containers.empty())
                    {
                        ImGui::TextDisabled("No containers.");
                    }

                    for (const auto& containerPtr : containers)
                    {
                        Container* container = containerPtr.get();

                        ImGui::PushID(container);

                        const std::string& name = container->name();

                        const bool open = ImGui::TreeNodeEx(
                            name.empty() ? "(unnamed container)" : name.c_str(),
                            ImGuiTreeNodeFlags_DefaultOpen
                        );

                        if (open)
                        {
                            drawModulesNode(container->modules());
                            drawResourcesNode(container->resources());

                            if (container->interface())
                                drawInterfaceNode(*container->interface());

                            if (ImGui::TreeNodeEx("Variables"))
                            {
                                if (container->variables().empty()) ImGui::Text("(no exposed variables)");
                                for (const Variable& variable : container->variables())
                                {
                                    if (variable.type == VarType::Bool) ImGui::Checkbox(
                                        variable.name.c_str(), static_cast<bool*>(variable.ptr));
                                    if (variable.type == VarType::Int) ImGui::InputInt(
                                        variable.name.c_str(), static_cast<int*>(variable.ptr));
                                    if (variable.type == VarType::Float) ImGui::InputFloat(
                                        variable.name.c_str(), static_cast<float*>(variable.ptr));
                                    if (variable.type == VarType::Double) ImGui::InputDouble(
                                        variable.name.c_str(), static_cast<double*>(variable.ptr));
                                    if (variable.type == VarType::String)
                                    {
                                        char buffer[256];
                                        buffer[0] = '\0';
                                        strncpy(
                                            buffer,
                                            static_cast<std::string*>(variable.ptr)->c_str(),
                                            sizeof(buffer) - 1
                                        );
                                        buffer[sizeof(buffer) - 1] = '\0';

                                        if (ImGui::InputText(
                                            variable.name.c_str(),
                                            buffer,
                                            sizeof(buffer)
                                        ))
                                        {
                                            *static_cast<std::string*>(variable.ptr) = buffer;
                                        }
                                    }
                                }

                                ImGui::TreePop();
                            }

                            container->imgui();

                            ImGui::TreePop();
                        }

                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
            }
            if (ImGui::TreeNodeEx("Events"))
            {
                EventBus& bus = m_application.application().eventBus();
                const auto& events = bus.events();

                if (events.empty())
                {
                    ImGui::TextDisabled("No events registered.");
                }

                bool anySubscribed = false;

                for (const EventBus::RegisteredEvent& event : events)
                {
                    const Container* subscriber = bus.subscriber(event.handle);

                    if (!subscriber) continue;

                    anySubscribed = true;

                    ImGui::PushID(static_cast<int>(event.handle));

                    ImGui::Text("Event %s", event.name.c_str());

                    ImGui::SameLine();

                    ImGui::TextDisabled(
                        "[%s]",
                        subscriber->name().empty()
                            ? "(unnamed container)"
                            : subscriber->name().c_str()
                    );

                    ImGui::SameLine();

                    if (ImGui::Button("Invoke"))
                    {
                        bus.emit(event.handle);
                    }

                    ImGui::PopID();
                }

                if (!anySubscribed)
                {
                    ImGui::TextDisabled("No container subscribed to any event.");
                }
                ImGui::TreePop();
            }

            ImGui::End();
        }

        void drawLoggingPanel()
        {
            if (ImGui::Begin("Logging"))
            {
                const std::vector<LogEntry> entries = m_application.application().getLogger()->entries();

                ImGui::Text("Messages: %zu", entries.size());

                ImGui::SameLine();

                if (ImGui::Button("Clear"))
                {
                    m_application.application().getLogger()->clearHistory();
                }

                ImGui::SameLine();

                ImGui::Checkbox("Auto-scroll", &m_logAutoScroll);

                ImGui::Separator();

                const char* levelNames[] = {"Trace", "Info", "Warning", "Error", "Fatal", "Debug"};

                ImGui::SetNextItemWidth(120.0f);

                if (ImGui::BeginCombo("##level_filter", levelNames[m_logFilterLevel]))
                {
                    for (int i = 0; i < 5; ++i)
                    {
                        const bool selected = (m_logFilterLevel == i);

                        if (ImGui::Selectable(levelNames[i], selected))
                            m_logFilterLevel = i;

                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }

                    ImGui::EndCombo();
                }

                ImGui::SameLine();

                ImGui::TextDisabled("(minimum level)");

                ImGui::Separator();

                if (ImGui::BeginChild(
                    "##log_entries",
                    ImVec2(0, 0),
                    ImGuiChildFlags_Borders
                ))
                {
                    for (const LogEntry& entry : entries)
                    {
                        if (static_cast<int>(entry.level) < m_logFilterLevel)
                            continue;

                        ImGui::PushStyleColor(
                            ImGuiCol_Text,
                            logColorForLevel(entry.level)
                        );

                        ImGui::TextWrapped(
                            "[%s] [%s] %s",
                            entry.timestamp.c_str(),
                            logLevelName(entry.level),
                            entry.message.c_str()
                        );

                        ImGui::PopStyleColor();
                    }

                    // Only re-pin to the bottom while the user is already
                    // at (or near) the bottom, so scrolling up to read
                    // history isn't fought by Auto-scroll.
                    const bool atBottom =
                        ImGui::GetScrollY() >=
                        ImGui::GetScrollMaxY() - 2.0f;

                    if (m_logAutoScroll && atBottom)
                    {
                        ImGui::SetScrollHereY(1.0f);
                    }
                }

                ImGui::EndChild();
            }

            ImGui::End();
        }

        static const char* logLevelName(LogLevel level)
        {
            switch (level)
            {
                case LogLevel::Trace:   return "Trace";
                case LogLevel::Info:    return "Info";
                case LogLevel::Warning: return "Warning";
                case LogLevel::Error:   return "Error";
                case LogLevel::Fatal:   return "Fatal";
                case LogLevel::Debug:   return "Debug";
            }

            return "Unknown";
        }

        static ImU32 logColorForLevel(LogLevel level)
        {
            switch (level)
            {
                case LogLevel::Trace:   return IM_COL32(160, 160, 160, 255);
                case LogLevel::Info:    return IM_COL32(80, 115, 250, 255);
                case LogLevel::Warning: return IM_COL32(240, 200, 80, 255);
                case LogLevel::Error:   return IM_COL32(235, 50, 60, 255);
                case LogLevel::Fatal:   return IM_COL32(205, 10, 20, 255);
                case LogLevel::Debug:   return IM_COL32(90, 200, 120, 255);
            }

            return IM_COL32(255, 255, 255, 255);
        }

        static void drawInterfaceNode(const Interface& interface)
        {
            const std::string header =
                "Interface: " + demangle(typeid(interface).name());

            if (ImGui::TreeNodeEx(
                header.c_str(),
                ImGuiTreeNodeFlags_DefaultOpen
            ))
            {
                interface.imgui();
                ImGui::TreePop();
            }
        }


        static void drawModulesNode(const ModuleRegistry& modules)
        {
            const std::string header =
                "Modules (" + std::to_string(modules.size()) + ")";

            if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (modules.empty())
                {
                    ImGui::TextDisabled("(none)");
                }

                for (const auto& modulePtr : modules.all())
                {

                    if (ImGui::TreeNodeEx(demangle(typeid(*modulePtr).name()).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        modulePtr->imgui();
                        ImGui::TreePop();
                    }

                }

                ImGui::TreePop();
            }
        }

        static void drawResourcesNode(const ResourceRegistry& resources)
        {
            const std::string header =
                "Resources (" + std::to_string(resources.size()) + ")";

            if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (resources.empty())
                {
                    ImGui::TextDisabled("(none)");
                }

                for (const auto& [id, entry] : resources.all())
                {
                    ImGui::BulletText(
                        "#%u  %s%s",
                        id,
                        demangle(typeid(*entry.resource).name()).c_str(),
                        entry.resource->isExported() ? "  [exported]" : ""
                    );
                }

                ImGui::TreePop();
            }
        }

        static std::string demangle(const char* mangled)
        {
#if defined(__GNUG__)
            int status = 0;

            std::unique_ptr<char, void(*)(void*)> result(
                abi::__cxa_demangle(mangled, nullptr, nullptr, &status),
                std::free
            );

            return (status == 0 && result) ? result.get() : mangled;
#else
            return mangled;
#endif
        }


        void drawVulkanPanel()
        {
            if (!vulkan)
                return;

            if (ImGui::Begin("Vulkan"))
            {
                ImGui::Text(
                    "Vulkan Framework"
                );

                ImGui::Separator();

                ImGui::Text(
                    "Window: %dx%d",
                    vulkan->window().getWidth(),
                    vulkan->window().getHeight()
                );

                ImGui::Text(
                    "Window title: %s",
                    vulkan->getName().c_str()
                );

                ImGui::Text(
                    "Swapchain images: %u",
                    vulkan->swapChain().imageCount()
                );

                ImGui::Separator();

                ImGui::Text(
                    "Viewport target: %ux%u",
                    m_viewportWidth,
                    m_viewportHeight
                );

                ImGui::Separator();

                drawPresentModeControl();
            }

            ImGui::End();
        }

        void drawExplorerPanel()
        {
            if (ImGui::Begin("Explorer"))
            {
                ImGui::PushID("Explorer");

                ImGui::Text(
                    "Path: %s",
                    m_explorerPath.string().c_str()
                );

                if (ImGui::Button("<- Up"))
                {
                    if (m_explorerPath.has_parent_path())
                    {
                        m_explorerPath = m_explorerPath.parent_path();
                    }
                }

                ImGui::SameLine();

                static char pathBuffer[4096];

                ImGui::SetNextItemWidth(-FLT_MIN);

                if (ImGui::InputText(
                    "##path",
                    pathBuffer,
                    sizeof(pathBuffer),
                    ImGuiInputTextFlags_EnterReturnsTrue
                ))
                {
                    std::filesystem::path newPath(pathBuffer);
                    std::error_code ec;

                    if (std::filesystem::is_directory(newPath, ec))
                    {
                        m_explorerPath = newPath;
                    }
                }
                // Only mirror the current path while the field is not
                // being edited, otherwise typing would be overwritten.
                else if (!ImGui::IsItemActive())
                {
                    std::snprintf(
                        pathBuffer,
                        sizeof(pathBuffer),
                        "%s",
                        m_explorerPath.string().c_str()
                    );
                }

                ImGui::Separator();

                if (ImGui::BeginChild(
                    "##explorer_list",
                    ImVec2(0, 0),
                    ImGuiChildFlags_Borders
                ))
                {
                    drawExplorerDirectory(m_explorerPath);
                }

                ImGui::EndChild();

                ImGui::PopID();
            }

            ImGui::End();
        }

        /**
         * @brief Recursively draws the contents of @p path. Directories
         * appear as collapsible tree nodes (directories first, then
         * files, alphabetical). Symlinks are listed but not followed, so
         * recursive listing cannot loop forever.
         */
        void drawExplorerDirectory(const std::filesystem::path& path)
        {
            std::error_code ec;

            std::vector<std::filesystem::directory_entry> entries;

            for (std::filesystem::directory_iterator it(
                path,
                std::filesystem::directory_options::skip_permission_denied,
                ec
            ), end;
                 it != end; it.increment(ec))
            {
                if (ec)
                {
                    ec.clear();
                    continue;
                }

                entries.push_back(*it);
            }

            std::sort(
                entries.begin(),
                entries.end(),
                [](const auto& a, const auto& b)
                {
                    const bool aDir = a.is_directory();
                    const bool bDir = b.is_directory();

                    if (aDir != bDir)
                        return aDir;

                    return a.path().filename().string() <
                           b.path().filename().string();
                }
            );

            for (const auto& entry : entries)
            {
                const std::filesystem::path& entryPath = entry.path();
                const std::string name = entryPath.filename().string();

                std::error_code entryEc;

                if (entry.is_symlink(entryEc) && !entryEc)
                {
                    ImGui::TextDisabled(
                        "%s (symlink)",
                        name.c_str()
                    );
                    continue;
                }

                ImGui::PushID(entryPath.c_str());

                if (entry.is_directory(entryEc) && !entryEc)
                {
                    const bool open = ImGui::TreeNodeEx(
                        name.c_str(),
                        ImGuiTreeNodeFlags_SpanAvailWidth
                    );

                    if (open)
                    {
                        drawExplorerDirectory(entryPath);
                        ImGui::TreePop();
                    }
                }
                else
                {
                    const bool isSelected =
                        (entryPath == m_explorerSelected);

                    if (ImGui::Selectable(name.c_str(), isSelected))
                    {
                        m_explorerSelected = entryPath;
                    }
                }

                ImGui::PopID();
            }
        }

        /**
         * @brief Editable preview of the file currently selected in the
         * Explorer. Content is loaded lazily when the selection changes,
         * edited in place, and written back to disk on Save.
         */
        void drawFilePanel()
        {
            if (ImGui::Begin("File"))
            {
                if (m_explorerSelected.empty())
                {
                    ImGui::TextDisabled(
                        "Select a file in the Explorer to view or "
                        "edit it."
                    );

                    ImGui::End();
                    return;
                }

                std::error_code ec;

                if (!std::filesystem::is_regular_file(
                    m_explorerSelected,
                    ec
                ) || ec)
                {
                    ImGui::TextDisabled(
                        "Selection is not a regular file."
                    );

                    ImGui::End();
                    return;
                }

                // Load the buffer the first time a (new) file is picked.
                if (m_filePanelCurrentPath != m_explorerSelected)
                {
                    loadFilePanel();
                }

                ImGui::TextWrapped(
                    "File: %s",
                    m_explorerSelected.string().c_str()
                );

                if (ImGui::Button("Reload"))
                {
                    loadFilePanel();
                }

                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip(
                        "Re-read the file from disk, discarding "
                        "unsaved changes."
                    );
                }

                if (m_filePanelDirty)
                {
                    ImGui::SameLine();
                    ImGui::TextColored(
                        ImVec4(1.0f, 0.75f, 0.10f, 1.0f),
                        "(modified)"
                    );
                }

                ImGui::SameLine();

                if (ImGui::Button(
                    m_filePanelDirty ? "Save" : "Save (up to date)"
                ))
                {
                    saveFilePanel();
                }

                ImGui::Separator();

                if (m_filePanelUnsupported)
                {
                    ImGui::TextWrapped(
                        "This file cannot be opened for editing "
                        "(too large or not readable)."
                    );

                    ImGui::End();
                    return;
                }

                if (ImGui::InputTextMultiline(
                    "##file_content",
                    &m_filePanelContent,
                    ImVec2(-FLT_MIN, -FLT_MIN),
                    ImGuiInputTextFlags_AllowTabInput
                ))
                {
                    m_filePanelDirty = true;
                }
            }

            ImGui::End();
        }

        /**
         * @brief Reads the currently selected explorer file into the
         * editing buffer, resetting the modified flag.
         */
        void loadFilePanel()
        {
            m_filePanelCurrentPath = m_explorerSelected;
            m_filePanelContent.clear();
            m_filePanelDirty = false;
            m_filePanelUnsupported = false;

            std::error_code ec;

            const std::uintmax_t size =
                std::filesystem::file_size(
                    m_filePanelCurrentPath,
                    ec
                );

            if (ec || size > kMaxEditFileBytes)
            {
                m_filePanelUnsupported = true;
                return;
            }

            std::ifstream stream(
                m_filePanelCurrentPath,
                std::ios::binary
            );

            if (!stream)
            {
                m_filePanelUnsupported = true;
                return;
            }

            m_filePanelContent.assign(
                std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char>()
            );
        }

        /**
         * @brief Writes the editing buffer back to the selected file.
         * The modified flag is only cleared on a successful write.
         */
        void saveFilePanel()
        {
            if (m_explorerSelected.empty())
                return;

            std::ofstream stream(
                m_explorerSelected,
                std::ios::binary | std::ios::trunc
            );

            if (!stream)
                return;

            stream.write(
                m_filePanelContent.data(),
                static_cast<std::streamsize>(
                    m_filePanelContent.size()
                )
            );

            if (stream)
            {
                m_filePanelDirty = false;
            }
        }

        /**
         * @brief Shows the swapchain's active present mode and a V-Sync
         * checkbox that toggles between it and Immediate (no vsync).
         * @details Vulkan fixes the present mode at swapchain-creation
         * time - there's no way to change it on a live swapchain - so
         * toggling this tears down and rebuilds the swapchain via
         * SwapChain::recreate(), the same mechanism already used for
         * window resizes.
         */
        void drawPresentModeControl()
        {
            SwapChain& swapChain = vulkan->swapChain();

            const VkPresentModeKHR active = swapChain.presentMode();

            ImGui::Text("Present mode: %s", presentModeName(active));

            bool vsync = (active != VK_PRESENT_MODE_IMMEDIATE_KHR);

            if (ImGui::Checkbox("V-Sync", &vsync))
            {
                swapChain.setPresentModePreference(
                    vsync
                        ? VK_PRESENT_MODE_MAILBOX_KHR
                        : VK_PRESENT_MODE_IMMEDIATE_KHR
                );

                // NOTE: the `passed` argument must match whichever
                // creation path your Vulkan framework actually uses
                // (createPassed vs. createDynamic) - use the same value
                // it already passes on window resize.
                swapChain.recreate(/* passed */ true);
            }

            ImGui::SameLine();
            ImGui::TextDisabled("(?)");

            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    "Unchecking requests Immediate present mode (no "
                    "vsync, may tear). Falls back to Fifo if the "
                    "surface/driver doesn't support Immediate."
                );
            }
        }

        static const char* presentModeName(VkPresentModeKHR mode)
        {
            switch (mode)
            {
            case VK_PRESENT_MODE_IMMEDIATE_KHR:
                return "Immediate (no vsync)";
            case VK_PRESENT_MODE_MAILBOX_KHR:
                return "Mailbox (vsync, triple-buffered)";
            case VK_PRESENT_MODE_FIFO_KHR:
                return "Fifo (vsync)";
            case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
                return "Fifo Relaxed (vsync, tears if late)";
            default:
                return "Unknown";
            }
        }

        /**
         * @brief Draws the dockable/movable/collapsible panel that hosts
         * the game's scene, rendered offscreen and displayed here as a
         * regular ImGui image.
         */
        void drawViewport()
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

            if (ImGui::Begin("Viewport"))
            {
                const ImVec2 avail = ImGui::GetContentRegionAvail();

                const auto width = static_cast<uint32_t>(avail.x);
                const auto height = static_cast<uint32_t>(avail.y);

                if (width > 0 && height > 0 &&
                    (width != m_viewportWidth || height != m_viewportHeight))
                {
                    resizeViewportTarget(width, height);
                }

                if (m_viewportTextureId)
                {
                    ImGui::Image(
                        m_viewportTextureId,
                        ImVec2(
                            static_cast<float>(m_viewportWidth),
                            static_cast<float>(m_viewportHeight)
                        )
                    );
                }
                // Did the user click the viewport?
                if (ImGui::IsWindowFocused())
                {
                    vulkan->window().getInputHandler().focus();
                }
                else
                {
                    vulkan->window().getInputHandler().unfocus();
                }
            }

            ImGui::End();

            ImGui::PopStyleVar();
        }

        void createViewportTarget(uint32_t width, uint32_t height)
        {
            const VkFormat colorFormat =
                vulkan->renderPass().getColorFormat();

            const VkFormat depthFormat =
                vulkan->renderPass().getDepthFormat();

            // Color attachment, also sampled by ImGui's shader.
            TextureCreateInfo colorInfo{
                .width = width,
                .height = height,
                .format = Format(colorFormat),
                .usage =
                ImageUsage::COLOR_ATTACHMENT |
                ImageUsage::SAMPLED
            };

            m_viewportColor =
                std::make_unique<Texture>(
                    vulkan->device(),
                    colorInfo
                );

            // Depth attachment (only needed as an attachment, never
            // sampled), matching the swapchain render pass's depth format.
            ImageCreateInfo depthInfo{
                .type = ImageType::IMAGE_2D,
                .extent = {width, height, 1},
                .format = depthFormat,
                .usage = ImageUsage::DEPTH_ATTACHMENT,
                .aspect = VK_IMAGE_ASPECT_DEPTH_BIT
            };

            m_viewportDepth =
                std::make_unique<Image>(
                    vulkan->device(),
                    depthInfo
                );

            m_viewportRenderPass =
                buildViewportRenderPass(
                    vulkan->device(),
                    colorFormat,
                    depthFormat
                );

            m_viewportFramebuffer =
                std::make_unique<Framebuffer>(
                    vulkan->device(),
                    *m_viewportRenderPass,
                    std::vector<Image*>{
                        &m_viewportColor->image(),
                        m_viewportDepth.get()
                    }
                );

            m_viewportTextureId =
                reinterpret_cast<ImTextureID>(
                    ImGui_ImplVulkan_AddTexture(
                        m_viewportColor->image().view(),
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                    )
                );

            m_viewportWidth = width;
            m_viewportHeight = height;

            vulkan->setOffscreenTarget(
                m_viewportFramebuffer.get(),
                m_viewportRenderPass.get(),
                VkExtent2D{width, height}
            );
        }


        void resizeViewportTarget(uint32_t width, uint32_t height)
        {
            vkDeviceWaitIdle(vulkan->device().native());

            vulkan->clearOffscreenTarget();

            if (m_viewportTextureId)
            {
                ImGui_ImplVulkan_RemoveTexture(
                    reinterpret_cast<VkDescriptorSet>(m_viewportTextureId)
                );
            }

            // Destruction order matters: framebuffer references the
            // images and render pass, so it must go first.
            m_viewportFramebuffer.reset();
            m_viewportColor.reset();
            m_viewportDepth.reset();
            m_viewportRenderPass.reset();

            createViewportTarget(width, height);
        }

        void destroyViewportTarget()
        {
            if (!vulkan)
                return;

            vkDeviceWaitIdle(vulkan->device().native());

            vulkan->clearOffscreenTarget();

            if (m_viewportTextureId)
            {
                ImGui_ImplVulkan_RemoveTexture(
                    reinterpret_cast<VkDescriptorSet>(m_viewportTextureId)
                );
            }

            m_viewportFramebuffer.reset();
            m_viewportColor.reset();
            m_viewportDepth.reset();
            m_viewportRenderPass.reset();

            m_viewportTextureId = {};
            m_viewportWidth = 0;
            m_viewportHeight = 0;
        }

        static std::unique_ptr<RenderPass> buildViewportRenderPass(
            Device& device,
            VkFormat colorFormat,
            VkFormat depthFormat
        )
        {
            VkAttachmentDescription colorAttachment{};
            colorAttachment.format = colorFormat;
            colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkAttachmentReference colorAttachmentRef{};
            colorAttachmentRef.attachment = 0;
            colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkAttachmentDescription depthAttachment{};
            depthAttachment.format = depthFormat;
            depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkAttachmentReference depthAttachmentRef{};
            depthAttachmentRef.attachment = 1;
            depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &colorAttachmentRef;
            subpass.pDepthStencilAttachment =
                (depthFormat != VK_FORMAT_UNDEFINED) ? &depthAttachmentRef : nullptr;

            VkSubpassDependency dependency{};
            dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
            dependency.dstSubpass = 0;
            dependency.srcStageMask =
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            dependency.srcAccessMask = 0;
            dependency.dstStageMask =
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            dependency.dstAccessMask =
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

            std::array<VkAttachmentDescription, 2> attachments = {
                colorAttachment, depthAttachment
            };

            const uint32_t attachmentCount =
                (depthFormat != VK_FORMAT_UNDEFINED) ? 2 : 1;

            VkRenderPassCreateInfo renderPassInfo{};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            renderPassInfo.attachmentCount = attachmentCount;
            renderPassInfo.pAttachments = attachments.data();
            renderPassInfo.subpassCount = 1;
            renderPassInfo.pSubpasses = &subpass;
            renderPassInfo.dependencyCount = 1;
            renderPassInfo.pDependencies = &dependency;

            return std::make_unique<RenderPass>(&device, renderPassInfo);
        }

        void shutdown()
        {
            detach();
        }

    private:
        UserGame& m_application;
        Scheduler* m_scheduler;

        VulkanRenderer* vulkan = nullptr;
        std::unique_ptr<DescriptorPool> m_imguiPool;

        uint32_t m_viewportWidth = 0;
        uint32_t m_viewportHeight = 0;

        std::filesystem::path m_explorerPath =
            std::filesystem::current_path();

        std::filesystem::path m_explorerSelected;

        std::filesystem::path m_filePanelCurrentPath;
        std::string m_filePanelContent;
        bool m_filePanelDirty = false;
        bool m_filePanelUnsupported = false;

        static constexpr std::uintmax_t kMaxEditFileBytes = 10'000'000;

        std::unique_ptr<RenderPass> m_viewportRenderPass;
        std::unique_ptr<Texture> m_viewportColor;
        std::unique_ptr<Image> m_viewportDepth;
        std::unique_ptr<Framebuffer> m_viewportFramebuffer;

        ImTextureID m_viewportTextureId{};

        using Clock = std::chrono::steady_clock;

        Clock::time_point m_lastFrameTime{};
        std::array<float, 120> m_frameTimesMs{};
        std::size_t m_frameTimeCursor = 0;
        std::size_t m_frameTimeSamples = 0;
        std::unordered_map<JobID, std::array<float, 120>> m_jobHistoryMs;
        std::unordered_map<JobID, std::size_t> m_jobHistoryCursor;
        std::size_t m_jobHistorySamples = 0; // shared cursor/sample count across all jobs

        bool m_logAutoScroll = true;
        int m_logFilterLevel = 0;   // index into levelNames[] (LavaEngine::LogLevel order)

        bool paused = false;
    };
}
