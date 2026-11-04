#include "GraphicalInterface.h"

#include "imgui.h"

namespace LavaEngine
{
    GraphicalPipline::GraphicalPipline(
        Device& device,
        PipelineLayout& layout,
        RenderPass& renderPass,
        VertexLayout& vertex_layout,
        const std::string& vertexShader,
        const std::string& fragmentShader,
        Topology topology,
        PolygonMode polygonMode,
        CullMode cullMode,
        FrontFace frontFace,
        bool depthTest,
        bool depthWrite,
        bool blending
    ) : m_vertexShader(device, vertexShader)
        , m_fragmentShader(device, fragmentShader),
        m_pipeline(
            device,
            {
                .vertexShader = &m_vertexShader,
                .fragmentShader = &m_fragmentShader,

                .layout = &layout,
                .renderPass = &renderPass,
                .vertexLayout = &vertex_layout,

                .topology = topology,

                .polygonMode = polygonMode,
                .cullMode = cullMode,
                .frontFace = frontFace,

                .depthTest = depthTest,
                .depthWrite = depthWrite,

                .blending = blending
            }
        ),
        m_layout(&layout),
        m_renderPass(&renderPass),
        m_vertexLayout(&vertex_layout),
        m_vertexShaderFile(vertexShader),
        m_fragmentShaderFile(fragmentShader)
    {
    }


    void GraphicalPipline::imgui() const
    {
        ImGui::Text("Vertex Shader: %s", m_vertexShaderFile.c_str());
        ImGui::Text("Fragment Shader: %s", m_fragmentShaderFile.c_str());
    }
}
