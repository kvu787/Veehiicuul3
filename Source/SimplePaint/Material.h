#pragma once

#include <array>
#include <cstddef>
#include <type_traits>

// SimplePaint's portable CPU interface. No DirectX types or renderer state.
namespace SimplePaint
{
// Practical binary32 domain, distinct from the open real-number domain.
// See this folder's README.md for the input contract and integration steps.
// These limits reject inputs; they are never shader clamps. Exact powers of two
// make boundary tests unambiguous.
inline constexpr double Margin = 1.0 / 1024.0;
inline constexpr double InteriorMaximum = 1.0 - Margin;

struct Parameters
{
    // User-facing sRGB channels, each in [Margin, InteriorMaximum].
    std::array<double, 3> baseColorSrgb{0.107, 0.223, 0.578};
    // Base color anchors at tone 1-brightness; same interior limits as RGB.
    double brightness = 0.5;
    // Shift in [0, InteriorMaximum]; zero disables directional warping.
    double shift = 0.0;
    // Canonical counterclockwise screen angle in [0,360); zero points right.
    double rotationDegrees = 0.0;
    // Tones at facing 0 and 1. Dark is [0,InteriorMaximum], light is [Margin,1].
    // Either ordering is valid, including equality (a constant painted color).
    double darkPoint = 0.0;
    double lightPoint = 1.0;
};

// Validate the portable input without computing GPU constants.
void ValidateParameters(const Parameters& parameters);

// Five HLSL float4 registers. The fourth color lanes are reserved and zero.
// Copy verbatim into a CBV (whose start address must be 256-byte aligned).
// Instances come only from Material::Compile; consumers must not edit them.
struct alignas(16) GpuMaterial
{
    // cos(-rotation), sin(-rotation), sqrt((1-shift)/(1+shift)),
    // and a zero/one flag indicating whether the requested shift is nonzero.
    std::array<float, 4> warp;
    std::array<float, 4> numeratorDark;
    std::array<float, 4> numeratorLight;
    std::array<float, 4> complementDark;
    std::array<float, 4> complementLight;
};
static_assert(std::is_trivially_copyable_v<GpuMaterial>);
static_assert(sizeof(GpuMaterial) == 80 && alignof(GpuMaterial) == 16);
static_assert(offsetof(GpuMaterial, numeratorDark) == 16);
static_assert(offsetof(GpuMaterial, numeratorLight) == 32);
static_assert(offsetof(GpuMaterial, complementDark) == 48);
static_assert(offsetof(GpuMaterial, complementLight) == 64);

class Material final
{
public:
    // Validates ALL parameters in binary64 before narrowing or precomputation.
    // Throws std::invalid_argument with the field and accepted range. It never
    // clamps, wraps rotation, or substitutes a different requested material.
    [[nodiscard]] static Material Compile(const Parameters& parameters);
    [[nodiscard]] const GpuMaterial& Constants() const noexcept { return m_gpu; }

private:
    explicit Material(const GpuMaterial& gpu) noexcept : m_gpu(gpu) {}
    GpuMaterial m_gpu;
};
}
