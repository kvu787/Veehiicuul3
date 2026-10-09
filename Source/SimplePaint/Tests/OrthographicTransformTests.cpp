#include "SimplePaint/OrthographicTransforms.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <iterator>
#include <random>
#include <stdexcept>

using namespace DirectX;

void RequireNear(float actual, float expected)
{
    if (!std::isfinite(actual) || std::abs(actual - expected) > 2.0e-5f * std::max(1.0f, std::abs(expected)))
        throw std::runtime_error("Packed orthographic transform differs from DirectXMath");
}

int main()
{
    try
    {
        std::mt19937 random(0x4f525448);
        std::uniform_real_distribution<float> position(-10.0f, 10.0f), angle(-3.14f, 3.14f);
        for (float aspect : {0.5625f, 1.0f, 16.0f / 9.0f, 32.0f / 9.0f})
        {
            const auto projection = Orthographic::MakeProjection(5.0f * aspect, 5.0f, 1.0f, 20.0f);
            const auto referenceProjection = XMMatrixOrthographicRH(5.0f * aspect, 5.0f, 1.0f, 20.0f);
            // Near/far mapping must remain correct for depth clipping and overlap.
            for (const auto [z, depth] : {std::pair{-1.0f, 0.0f}, {-20.0f, 1.0f}})
                RequireNear(z * projection.scale.z + projection.depthOffset, depth);

            for (unsigned sample = 0; sample < 1000; ++sample)
            {
                const auto world = XMMatrixScaling(0.4f, 0.4f, 0.4f) *
                    XMMatrixRotationRollPitchYaw(angle(random), angle(random), angle(random)) *
                    XMMatrixTranslation(position(random), position(random), position(random));
                const auto view = XMMatrixLookAtRH(
                    XMVectorSet(1.6889755f, 3.6863865f, 2.9253915f, 1.0f),
                    XMVectorZero(), XMVectorSet(-0.36863866f, 0.6755902f, -0.63850087f, 0.0f));
                const auto worldView = world * view;
                const auto compact = Orthographic::BuildObjectTransforms(worldView, projection);
                const auto vertex = XMVectorSet(position(random), position(random), position(random), 1.0f);
                const auto normal = XMVectorSet(position(random), position(random), position(random), 0.0f);
                const auto expectedPosition = XMVector4Transform(vertex, worldView * referenceProjection);
                const auto expectedNormal = XMVector3TransformNormal(normal, worldView);
                for (unsigned axis = 0; axis < 3; ++axis)
                {
                    const auto clip = XMVector4Dot(vertex, XMLoadFloat4(&compact.worldToClip[axis]));
                    const auto viewNormal = XMVector3Dot(normal, XMLoadFloat4(&compact.normalToView[axis]));
                    RequireNear(XMVectorGetX(clip), XMVectorGetByIndex(expectedPosition, axis));
                    RequireNear(XMVectorGetX(viewNormal), XMVectorGetByIndex(expectedNormal, axis));
                }
                RequireNear(XMVectorGetW(expectedPosition), 1.0f);
            }
        }
        std::cout << "4,000 packed transforms, depth endpoints passed.\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
