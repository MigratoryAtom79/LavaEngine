#include "Mesh.h"

#include <limits>
#include <stdexcept>
#include <utility>

namespace LavaEngine
{
    void Mesh::setVertices(
        const void* vertices,
        std::size_t count,
        std::size_t stride
    )
    {
        if (count > 0 && stride == 0)
            throw std::invalid_argument("Mesh vertex stride must be non-zero");

        if (count > 0 && count > std::numeric_limits<std::size_t>::max() / stride)
            throw std::length_error("Mesh vertex data is too large");

        if (count == 0)
        {
            m_vertices.clear();
            m_vertexStride = 0;
            invalidateGpu();
            return;
        }

        if (!vertices)
            throw std::invalid_argument(
                "Mesh vertex data must not be null"
            );

        const std::size_t bytes = count * stride;

        m_vertices.resize(bytes);

        std::memcpy(m_vertices.data(), vertices, bytes);

        m_vertexStride = stride;

        invalidateGpu();
    }

    void Mesh::setIndices(std::vector<VertexIndex>&& indices)
    {
        m_indices = std::move(indices);
        invalidateGpu();
    }

    void Mesh::setIndices(std::span<const VertexIndex> indices)
    {
        m_indices.assign(indices.begin(), indices.end());
        invalidateGpu();
    }

    void Mesh::clear()
    {
        m_vertices.clear();
        m_indices.clear();
        m_vertexStride = 0;
        releaseGpu();
    }

    bool Mesh::upload(LavaVK::Device& device)
    {
        if (m_vertices.empty())
            return false;

        LavaVK::BufferCreateInfo vertexInfo{};
        vertexInfo.size = m_vertices.size();
        vertexInfo.usage = LavaVK::BufferUsage::Vertex;
        vertexInfo.memory = LavaVK::MemoryUsage::CPU_TO_GPU;

        std::optional<LavaVK::Buffer> vertexBuffer;
        vertexBuffer.emplace(device, vertexInfo);
        vertexBuffer->upload(m_vertices.data(), m_vertices.size());

        std::optional<LavaVK::Buffer> indexBuffer;

        if (!m_indices.empty())
        {
            LavaVK::BufferCreateInfo indexInfo{};
            indexInfo.size = m_indices.size() * sizeof(VertexIndex);
            indexInfo.usage = LavaVK::BufferUsage::Index;
            indexInfo.memory = LavaVK::MemoryUsage::CPU_TO_GPU;

            indexBuffer.emplace(device, indexInfo);
            indexBuffer->upload(m_indices.data(), indexInfo.size);
        }

        m_vertexBuffer = std::move(vertexBuffer);
        m_indexBuffer = std::move(indexBuffer);

        return true;
    }

    void Mesh::releaseGpu()
    {
        m_indexBuffer.reset();
        m_vertexBuffer.reset();
    }
}
