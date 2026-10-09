#include "SimplePaint/Material.h"
#include "SimplePaint/Geometry.h"
#include "SimplePaint/OrthographicTransforms.h"

#include <array>
#include <iostream>

// A host-owned vertex layout proves the copied validator has no car-mesh or
// application dependency. Exercise every public C++ header in the package.
struct Vertex
{
    float positionX, positionY, positionZ;
    float normalX, normalY, normalZ;
    std::uint32_t materialIndex;
};

int main()
{
    const auto material = SimplePaint::Material::Compile({});
    const std::array<Vertex,3> vertices = {{
        {.positionX = 0, .positionY = 0, .positionZ = 0,
            .normalX = 0, .normalY = 0, .normalZ = 1, .materialIndex = 0},
        {.positionX = 1, .positionY = 0, .positionZ = 0,
            .normalX = 0, .normalY = 0, .normalZ = 1, .materialIndex = 0},
        {.positionX = 0, .positionY = 1, .positionZ = 0,
            .normalX = 0, .normalY = 0, .normalZ = 1, .materialIndex = 0},
    }};
    const std::array<std::uint32_t,3> indices{0,1,2};
    SimplePaint::ValidateMesh<Vertex>(vertices,indices,1);
    const auto projection = Orthographic::MakeProjection(2,2,0,1);
    const auto object = Orthographic::BuildObjectTransforms(DirectX::XMMatrixIdentity(),projection);
    if (material.Constants().warp[3] != 0 || object.worldToClip[0].x != 1)
        return 1;
    std::cout << "Copied SimplePaint compiles and runs without application sources.\n";
}
