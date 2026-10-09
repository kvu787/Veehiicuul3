#include "SimplePaintCore.hlsli"

// Reusable DX12 adapter. Define the count when compiling BOTH entry points;
// the host must upload the same number of 80-byte material records at b1.
#ifndef SIMPLE_PAINT_MATERIAL_COUNT
#define SIMPLE_PAINT_MATERIAL_COUNT 1
#endif
#if SIMPLE_PAINT_MATERIAL_COUNT < 1
#error SIMPLE_PAINT_MATERIAL_COUNT must be positive
#endif
static const uint MaterialCount = SIMPLE_PAINT_MATERIAL_COUNT;
cbuffer ObjectConstants : register(b0)
{
    // Three columns of affine transforms; clip W is always one.
    float4 worldToClip[3];
    float4 normalToView[3];
};
cbuffer MaterialConstants : register(b1)
{
    SimplePaintMaterial paintMaterials[MaterialCount];
};

struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    uint materialIndex : MATERIAL;
};
struct PixelInput
{
    float4 position : SV_POSITION;
    noperspective float3 paintNormal : NORMAL;
    nointerpolation uint materialIndex : MATERIAL;
};

PixelInput VSMain(VertexInput input)
{
    PixelInput output;
    const float4 p = float4(input.position, 1.0f);
    output.position = float4(dot(p, worldToClip[0]), dot(p, worldToClip[1]),
        dot(p, worldToClip[2]), 1.0f);
    const float3 normal = float3(dot(input.normal, normalToView[0].xyz),
        dot(input.normal, normalToView[1].xyz), dot(input.normal, normalToView[2].xyz));
    output.paintNormal = SimplePaintRotateNormal(normal, paintMaterials[input.materialIndex]);
    output.materialIndex = input.materialIndex;
    return output;
}

float4 PSMain(PixelInput input) : SV_TARGET
{
    // sRGB encoding is performed once by the application's sRGB RTV.
    return float4(SimplePaintShade(input.paintNormal, paintMaterials[input.materialIndex]), 1.0f);
}
