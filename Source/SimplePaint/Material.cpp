#include "Material.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace SimplePaint
{
namespace
{
void RequireRange(double value, double minimum, double maximum, const char* name)
{
    if (!std::isfinite(value) || value < minimum || value > maximum)
    {
        std::ostringstream message;
        message << std::setprecision(17) << "SimplePaint " << name <<
            " must be finite and in [" << minimum << ", " << maximum << "].";
        throw std::invalid_argument(message.str());
    }
}

double DecodeSrgb(double value)
{
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}
}

void ValidateParameters(const Parameters& p)
{
    RequireRange(p.baseColorSrgb[0], Margin, InteriorMaximum, "R");
    RequireRange(p.baseColorSrgb[1], Margin, InteriorMaximum, "G");
    RequireRange(p.baseColorSrgb[2], Margin, InteriorMaximum, "B");
    RequireRange(p.brightness, Margin, InteriorMaximum, "Brightness");
    RequireRange(p.shift, 0.0, InteriorMaximum, "Shift");
    RequireRange(p.darkPoint, 0.0, InteriorMaximum, "Dark Point");
    RequireRange(p.lightPoint, Margin, 1.0, "Light Point");
    if (!std::isfinite(p.rotationDegrees) || p.rotationDegrees < 0.0 || p.rotationDegrees >= 360.0)
        throw std::invalid_argument("SimplePaint Rotation must be finite and in [0, 360) degrees.");
}

Material Material::Compile(const Parameters& p)
{
    ValidateParameters(p);
    const double radians = -p.rotationDegrees * std::numbers::pi / 180.0;
    GpuMaterial gpu{
        .warp = {static_cast<float>(std::cos(radians)), static_cast<float>(std::sin(radians)),
            static_cast<float>(std::sqrt((1.0 - p.shift) / (1.0 + p.shift))),
            p.shift == 0.0 ? 0.0f : 1.0f},
    };
    for (std::size_t channel = 0; channel < 3; ++channel)
    {
        const double c = DecodeSrgb(p.baseColorSrgb[channel]);
        const double a = c * p.brightness;
        const double b = (1.0 - c) * (1.0 - p.brightness);
        // Compose the tone remap with Schlick on the CPU, retaining positive
        // endpoint contributions. A common scale cancels from the ratio and
        // ensures at least one coefficient per channel is exactly one.
        const double nd = a * p.darkPoint;
        const double nl = a * p.lightPoint;
        const double cd = b * (1.0 - p.darkPoint);
        const double cl = b * (1.0 - p.lightPoint);
        const double scale = std::max({nd, nl, cd, cl});
        gpu.numeratorDark[channel] = static_cast<float>(nd / scale);
        gpu.numeratorLight[channel] = static_cast<float>(nl / scale);
        gpu.complementDark[channel] = static_cast<float>(cd / scale);
        gpu.complementLight[channel] = static_cast<float>(cl / scale);
    }
    return Material(gpu);
}
}
