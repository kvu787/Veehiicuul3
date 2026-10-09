// Keep Veehiicuul's vendored shader unchanged. Ground semantics take an explicit
// unlit path and return a single linear color, independent of the view normal.
#define PSMain SimplePaintPixel
#include "SimplePaint/SimplePaint.hlsl"
#undef PSMain
cbuffer SurfaceConstants : register(b2)
{
    float4 surfaceMaterials[SIMPLE_PAINT_MATERIAL_COUNT];
};
float4 PSMain(PixelInput input) : SV_TARGET
{
    const float4 material=surfaceMaterials[input.materialIndex];
    if(material.w!=0) return float4(material.xyz,1);
    return SimplePaintPixel(input);
}
