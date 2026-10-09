#pragma once

#include "SimplePaint/Material.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <random>
#include <stdexcept>
#include <vector>

namespace PaintTest
{
using Normal = std::array<float, 3>;
struct Case { SimplePaint::Parameters parameters; Normal normal; };

inline void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

inline double Linear(double srgb)
{
    return srgb <= 0.04045 ? srgb / 12.92 : std::pow((srgb + 0.055) / 1.055, 2.4);
}
inline double Srgb(double linear)
{
    return linear <= 0.0031308 ? 12.92*linear : 1.055*std::pow(linear, 1.0/2.4)-0.055;
}
inline double Curve(double color, double brightness, double tone)
{
    const double a = Linear(color)*brightness;
    const double b = (1.0-Linear(color))*(1.0-brightness);
    return a*tone / (a*tone+b*(1.0-tone));
}

// Independent binary64 reference: direct rational K12 reduction, normalized
// normal, explicit tone interpolation, and uncomposed Schlick color function.
// Only roundoff beyond the mathematical [0,1] facing range is removed here.
inline std::array<double, 3> Reference(const Case& sample)
{
    const auto& p = sample.parameters;
    const double angle = -p.rotationDegrees*std::numbers::pi/180.0;
    const double x = sample.normal[0]*std::cos(angle)-sample.normal[1]*std::sin(angle);
    const double y = sample.normal[0]*std::sin(angle)+sample.normal[1]*std::cos(angle);
    const double z = sample.normal[2];
    const double length = std::sqrt(x*x+y*y+z*z);
    double f = 0.0;
    if (z > 0.0)
    {
        const double r = std::hypot(x,z);
        f = r*z*std::sqrt((1.0-p.shift)*(1.0+p.shift)) / (length*(r-p.shift*x));
        f = std::clamp(f, 0.0, 1.0);
    }
    const double tone = p.darkPoint*(1.0-f)+p.lightPoint*f;
    return {Curve(p.baseColorSrgb[0],p.brightness,tone),
        Curve(p.baseColorSrgb[1],p.brightness,tone), Curve(p.baseColorSrgb[2],p.brightness,tone)};
}

inline std::vector<Case> Cases()
{
    using namespace SimplePaint;
    std::vector<Case> cases;
    // Cartesian extremes, full/inverted/constant tone ranges, exact axis normals,
    // both slice poles, former cutoff, and tiny positive slices (underflow risk).
    for (double color : {Margin, 0.04045, 0.5, InteriorMaximum})
    for (double brightness : {Margin, 0.126, 0.5, InteriorMaximum})
    for (double shift : {0.0, 1.0e-12, 0.5, InteriorMaximum})
    for (double rotation : {0.0, 90.0, 187.0, 359.999999})
    for (const auto range : {std::array{0.0,1.0}, std::array{InteriorMaximum,Margin}, std::array{0.5,0.5}})
    {
        const Parameters p{
            .baseColorSrgb = {color, 0.223, InteriorMaximum},
            .brightness = brightness,
            .shift = shift,
            .rotationDegrees = rotation,
            .darkPoint = range[0],
            .lightPoint = range[1],
        };
        for (Normal n : {Normal{0,0,1}, {0,0,-1}, {1,0,0}, {0,1,0}, {0,-1,0},
            {0,1,1.0e-30f}, {1.0e-30f,1,1.0e-30f}, {1,0,0.009999f}, {1,0,0.010001f}})
            cases.push_back({.parameters = p, .normal = n});
        // Dense rings around the lobe maximum: these expose loss of 1-facing
        // much more effectively than uniformly random normals.
        for (double offset : {-0.01,-0.001,-0.0001,-0.00001,0.0,0.00001,0.0001,0.001,0.01})
        {
            const double a = std::asin(shift)+offset;
            const double rotationAngle = rotation*std::numbers::pi/180.0;
            cases.push_back({.parameters = p, .normal = {float(std::sin(a)*std::cos(rotationAngle)),
                float(std::sin(a)*std::sin(rotationAngle)),float(std::cos(a))}});
        }
    }
    std::mt19937 random(0x5041494e);
    std::uniform_real_distribution<double> unit(0.0,1.0), signedUnit(-1.0,1.0);
    for (unsigned i = 0; i < 16384; ++i)
    {
        const auto interior = [&]{return Margin + (1.0-2.0*Margin)*unit(random);};
        const Parameters p{
            .baseColorSrgb = {interior(),interior(),interior()},
            .brightness = interior(),
            .shift = InteriorMaximum*unit(random),
            .rotationDegrees = 360.0*unit(random),
            .darkPoint = InteriorMaximum*unit(random),
            .lightPoint = Margin+InteriorMaximum*unit(random),
        };
        Normal n{float(signedUnit(random)),float(signedUnit(random)),float(signedUnit(random))};
        const float length = std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
        const float scale = std::exp2(float(-10.0+20.0*unit(random)));
        for (auto& component : n) component *= scale/length;
        cases.push_back({.parameters = p, .normal = n});
    }
    return cases;
}
}
