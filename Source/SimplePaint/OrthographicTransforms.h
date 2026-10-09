#pragma once

#include <DirectXMath.h>
#include <array>
#include <cmath>
#include <stdexcept>

namespace Orthographic
{
// Projection is always orthographic, and object/view transforms are affine.
// Only clip XYZ is stored; the vertex shader supplies the invariant clip W = 1.
struct ObjectTransforms
{
    std::array<DirectX::XMFLOAT4, 3> worldToClip;
    std::array<DirectX::XMFLOAT4, 3> normalToView;
};
static_assert(sizeof(ObjectTransforms) == 96);

struct Projection
{
    DirectX::XMFLOAT3 scale;
    float depthOffset;
};

inline Projection MakeProjection(float width, float height, float nearZ, float farZ)
{
    if (!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(nearZ) ||
        !std::isfinite(farZ) || width < 1.0e-4f || height < 1.0e-4f ||
        width > 1.0e6f || height > 1.0e6f || nearZ < 0.0f || farZ > 1.0e6f || farZ-nearZ < 1.0e-4f)
        throw std::invalid_argument("Orthographic projection requires bounded positive dimensions and 0 <= near < far.");
    const float depthScale = 1.0f / (nearZ - farZ);
    return {
        .scale = {2.0f / width, 2.0f / height, depthScale},
        .depthOffset = depthScale * nearZ,
    };
}

inline ObjectTransforms BuildObjectTransforms(
    DirectX::FXMMATRIX worldView, const Projection& projection)
{
    if (!std::isfinite(projection.scale.x) || !std::isfinite(projection.scale.y) ||
        !std::isfinite(projection.scale.z) || !std::isfinite(projection.depthOffset) ||
        projection.scale.x <= 0.0f || projection.scale.y <= 0.0f || projection.scale.z >= 0.0f ||
        projection.scale.x > 2.0e4f || projection.scale.y > 2.0e4f ||
        projection.scale.z < -1.0e4f || std::abs(projection.depthOffset) > 1.0e10f)
        throw std::invalid_argument("Invalid packed orthographic projection.");
    DirectX::XMFLOAT4X4 matrix;
    DirectX::XMStoreFloat4x4(&matrix, worldView);
    for (const auto& row : matrix.m)
        for (float value : row)
            if (!std::isfinite(value) || std::abs(value) > 1.0e6f)
                throw std::invalid_argument("SimplePaint transforms must be finite and bounded.");
    if (matrix._14 != 0.0f || matrix._24 != 0.0f || matrix._34 != 0.0f || matrix._44 != 1.0f)
        throw std::invalid_argument("SimplePaint supports affine object/view transforms and orthographic projection only.");
    // The adapter deliberately supports only rotations/reflections and uniform
    // scale. Reject shear and nonuniform scale instead of shading wrong normals.
    double lengths[3]{};
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned k = 0; k < 3; ++k)
            lengths[i] += double(matrix.m[i][k])*matrix.m[i][k];
    for (unsigned i = 0; i < 3; ++i)
    {
        if (lengths[i] < 1.0/(1024.0*1024.0) || lengths[i] > 1024.0*1024.0 ||
            std::abs(lengths[i]-lengths[0]) > 1.0e-5*lengths[0])
            throw std::invalid_argument("SimplePaint requires uniform scale in [1/1024,1024].");
        for (unsigned j = 0; j < i; ++j)
        {
            double dot = 0.0;
            for (unsigned k = 0; k < 3; ++k) dot += double(matrix.m[i][k])*matrix.m[j][k];
            if (std::abs(dot) > 1.0e-5*lengths[0])
                throw std::invalid_argument("SimplePaint does not support sheared object/view transforms.");
        }
    }
    const DirectX::XMMATRIX columns = DirectX::XMMatrixTranspose(worldView);
    ObjectTransforms result;
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        // Normals use XYZ only. As before, object scales must be uniform.
        DirectX::XMStoreFloat4(&result.normalToView[axis], columns.r[axis]);
    }
    DirectX::XMStoreFloat4(&result.worldToClip[0],
        DirectX::XMVectorScale(columns.r[0], projection.scale.x));
    DirectX::XMStoreFloat4(&result.worldToClip[1],
        DirectX::XMVectorScale(columns.r[1], projection.scale.y));
    DirectX::XMStoreFloat4(&result.worldToClip[2],
        DirectX::XMVectorAdd(
            DirectX::XMVectorScale(columns.r[2], projection.scale.z),
            DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, projection.depthOffset)));
    return result;
}
}
