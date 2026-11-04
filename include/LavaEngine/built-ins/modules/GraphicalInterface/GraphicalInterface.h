#pragma once

#include "../../LavaVK/include/LavaVK/LavaVK.hpp"
#include "LavaEngine/Module.hpp"

namespace LavaEngine
{
    using namespace LavaVK;

    /**
     * @brief Module that wraps a compiled graphics pipeline for a Container.
     * @detail Owns the vertex/fragment Shaders and the GraphicsPipeline
     * built from the supplied render pass, pipeline layout and vertex layout.
     * @note Ownership: Owned by a Container via its ModuleRegistry. The
     * module owns its shaders and pipeline; it borrows the Device,
     * PipelineLayout, RenderPass and VertexLayout used at construction, so
     * those must outlive the module.
     * @example
     * @code
     * world.addModule<GraphicalPipline>(
     *     device, layout, renderPass, vertexLayout, "main.vert", "main.frag");
     * @endcode
     */
    class GraphicalPipline : public Module
    {
    public:


        GraphicalPipline(
            Device& device,
            PipelineLayout& layout,
            RenderPass& renderPass,
            VertexLayout& vertex_layout,
            const std::string& vertexShader,
            const std::string& fragmentShader,
            Topology topology = Topology::TRIANGLES,
            PolygonMode polygonMode = PolygonMode::FILL,
            CullMode cullMode = CullMode::NONE,
            FrontFace frontFace = FrontFace::COUNTER_CLOCKWISE,
            bool depthTest = true,
            bool depthWrite = true,
            bool blending = false
        );
        ~GraphicalPipline() override = default;

        [[nodiscard]]
        GraphicsPipeline& pipeline()
        {
            return m_pipeline;
        }

        [[nodiscard]]
        const GraphicsPipeline& pipeline() const
        {
            return m_pipeline;
        }

        [[nodiscard]]
        const Shader& vertexShader() const
        {
            return m_vertexShader;
        }

        [[nodiscard]]
        const Shader& fragmentShader() const
        {
            return m_fragmentShader;
        }

        [[nodiscard]]
        VertexLayout& vertexLayout()
        {
            return *m_vertexLayout;
        }

        [[nodiscard]]
        const VertexLayout& vertexLayout() const
        {
            return *m_vertexLayout;
        }

        [[nodiscard]]
        RenderPass& renderPass()
        {
            return *m_renderPass;
        }

        [[nodiscard]]
        const RenderPass& renderPass() const
        {
            return *m_renderPass;
        }

        [[nodiscard]]
        PipelineLayout& pipelineLayout()
        {
            return *m_layout;
        }

        [[nodiscard]]
        const PipelineLayout& pipelineLayout() const
        {
            return *m_layout;
        }

        void imgui() const override;

    private:
        Shader m_vertexShader;
        Shader m_fragmentShader;
        GraphicsPipeline m_pipeline;

        PipelineLayout* m_layout;
        RenderPass* m_renderPass;
        VertexLayout* m_vertexLayout;

        std::string m_vertexShaderFile;
        std::string m_fragmentShaderFile;
    };
}
