#pragma once

#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace SimplePaint
{
// Validate once, before GPU upload. Vertex needs positionX/Y/Z, normalX/Y/Z, materialIndex.
// A conservative common-cone test guarantees every barycentrically interpolated
// normal stays nonzero. Bad meshes must be corrected by their author, not in PS.
template<class Vertex>
void ValidateMesh(std::span<const Vertex> vertices, std::span<const std::uint32_t> indices,
    std::uint32_t materialCount)
{
    if (vertices.empty() || indices.empty() || indices.size() % 3 != 0 || materialCount == 0)
        throw std::invalid_argument("SimplePaint requires a nonempty indexed triangle mesh and materials.");
    for (const auto& v : vertices)
    {
        const double norm2 = double(v.normalX)*v.normalX + double(v.normalY)*v.normalY + double(v.normalZ)*v.normalZ;
        if (!std::isfinite(v.positionX) || !std::isfinite(v.positionY) || !std::isfinite(v.positionZ) ||
            std::abs(v.positionX) > 1.0e6f || std::abs(v.positionY) > 1.0e6f || std::abs(v.positionZ) > 1.0e6f ||
            !std::isfinite(norm2) || norm2 < 0.25 || norm2 > 4.0 || v.materialIndex >= materialCount)
            throw std::invalid_argument("SimplePaint vertex requires finite bounded position, normal length [0.5,2], and valid material index.");
    }
    for (std::size_t i = 0; i < indices.size(); i += 3)
    {
        if (indices[i] >= vertices.size() || indices[i+1] >= vertices.size() || indices[i+2] >= vertices.size())
            throw std::invalid_argument("SimplePaint mesh index is out of bounds.");
        const auto& a = vertices[indices[i]];
        const auto& b = vertices[indices[i+1]];
        const auto& c = vertices[indices[i+2]];
        if (a.materialIndex != b.materialIndex || a.materialIndex != c.materialIndex)
            throw std::invalid_argument("SimplePaint triangles must have one material.");
        const double sx = double(a.normalX)+b.normalX+c.normalX;
        const double sy = double(a.normalY)+b.normalY+c.normalY;
        const double sz = double(a.normalZ)+b.normalZ+c.normalZ;
        const double length = std::sqrt(sx*sx+sy*sy+sz*sz);
        for (const auto* v : {&a, &b, &c})
            if (length == 0.0 || (v->normalX*sx+v->normalY*sy+v->normalZ*sz) < 0.125*length)
                throw std::invalid_argument("SimplePaint triangle normals must share a cone with projection at least 0.125.");
    }
}
}
