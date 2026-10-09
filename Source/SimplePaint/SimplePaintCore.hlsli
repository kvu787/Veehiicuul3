#ifndef SIMPLE_PAINT_CORE_HLSLI
#define SIMPLE_PAINT_CORE_HLSLI

// Reusable SimplePaint core. The application owns bindings and vertex layout.
// CPU contract: Material.h and Geometry.h in this directory; see README.md.
// All colors are linear RGB. No lighting, gamma encoding, or material repair.
// Reimplements Kevin Vu's K12 Simple Paint mathematics:
// https://github.com/kvu787/SimplePaintShaders/blob/793126205e028f06f635f23e87a9bac856bf669a/Godot/ShaderTest/Shaders/K12.gdshader
struct SimplePaintMaterial
{
    float4 warp;
    float4 numeratorDark;
    float4 numeratorLight;
    float4 complementDark;
    float4 complementLight;
};

// A constant rotation commutes with interpolation. Call once per vertex when
// each triangle has one material. +X right, +Y up, +Z toward the camera.
float3 SimplePaintRotateNormal(float3 normal, SimplePaintMaterial material)
{
    [branch]
    if (material.warp.w != 0.0f)
        normal.xy = float2(
            normal.x * material.warp.x - normal.y * material.warp.y,
            normal.x * material.warp.y + normal.y * material.warp.x);
    return normal;
}

// Return positive homogeneous weights (P,Q) with f=P/(P+Q).
// Keeping Q instead of computing 1-f preserves tiny highlight complements.
// Normal length cancels: per-pixel normalization is unnecessary.
float2 SimplePaintFacingWeights(float3 n, SimplePaintMaterial material)
{
    // This defines the back hemisphere, including both slice poles. There is
    // no positive cutoff; for every fixed valid shift the front limit is zero.
    [branch]
    if (n.z <= 0.0f)
        return float2(0.0f, 1.0f);

    const float lengthN = sqrt(dot(n, n));
    [branch]
    if (material.warp.w == 0.0f)
    {
        // L-z = (x*x+y*y)/(L+z), without cancellation at the lobe maximum.
        return float2(n.z, dot(n.xy, n.xy) / (lengthN + n.z));
    }

    // Scale the slice before squaring. Near the Y poles x,z can be tiny even
    // when the normal has healthy length; their squares must not erase it.
    const float sliceScale = max(abs(n.x), n.z);
    const float2 slice = n.xz / sliceScale;
    const float sliceLength = sqrt(dot(slice, slice));
    const float r = sliceScale * sliceLength;
    // Two equivalent half-angle charts avoid subtracting nearly equal r and
    // abs(x). The common sqrt(1+shift) factor has already been canceled.
    const float a = material.warp.z * (n.x >= 0.0f ? sliceLength + slice.x : slice.y);
    const float b = n.x >= 0.0f ? slice.y : sliceLength - slice.x;
    const float squareSum = a*a + b*b;
    const float difference = a-b;
    const float p = r * (2.0f*a*b);
    // L-r = y*y/(L+r); 1-2ab/(a*a+b*b) = (a-b)^2/(a*a+b*b).
    // Both dark contributions stay nonnegative, even at the shifted peak.
    const float q = (n.y*n.y / (lengthN+r))*squareSum + r*difference*difference;
    return float2(p, q);
}

float3 SimplePaintShade(float3 rotatedViewNormal, SimplePaintMaterial material)
{
    const float2 weights = SimplePaintFacingWeights(rotatedViewNormal, material);
    const float3 numerator = material.numeratorDark.xyz * weights.y +
        material.numeratorLight.xyz * weights.x;
    const float3 complement = material.complementDark.xyz * weights.y +
        material.complementLight.xyz * weights.x;
    // Positive denominator by construction. Hardware reciprocal multiplication
    // can overshoot 1 by one ULP; this output saturation only removes roundoff.
    // No denominator floor, pow, trig, or material-validation branches.
    return saturate(numerator / (numerator + complement));
}
#endif
