#pragma once
#include "SimplePaint/Material.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

enum class PaintParameter { Red, Green, Blue, Brightness, Shift, Angle, DarkPoint, LightPoint };
inline constexpr int PaintParameterCount=8;
struct PaintRange { double minimum,maximum; int steps; };
inline PaintRange ParameterRange(PaintParameter parameter)
{
    using namespace SimplePaint;
    switch(parameter)
    {
    case PaintParameter::Red: case PaintParameter::Green: case PaintParameter::Blue: case PaintParameter::Brightness:
        return {Margin,InteriorMaximum,10000};
    case PaintParameter::Shift: case PaintParameter::DarkPoint: return {0,InteriorMaximum,10000};
    case PaintParameter::Angle: return {0,std::nextafter(360.0,0.0),36000};
    case PaintParameter::LightPoint: return {Margin,1,10000};
    }
    throw std::invalid_argument("Unknown paint parameter.");
}
inline double ParameterValue(const SimplePaint::Parameters& paint,PaintParameter parameter)
{
    switch(parameter)
    {
    case PaintParameter::Red: return paint.baseColorSrgb[0];
    case PaintParameter::Green: return paint.baseColorSrgb[1];
    case PaintParameter::Blue: return paint.baseColorSrgb[2];
    case PaintParameter::Brightness: return paint.brightness;
    case PaintParameter::Shift: return paint.shift;
    case PaintParameter::Angle: return paint.rotationDegrees;
    case PaintParameter::DarkPoint: return paint.darkPoint;
    case PaintParameter::LightPoint: return paint.lightPoint;
    }
    throw std::invalid_argument("Unknown paint parameter.");
}
inline void SetParameter(SimplePaint::Parameters& paint,PaintParameter parameter,double value)
{
    switch(parameter)
    {
    case PaintParameter::Red: paint.baseColorSrgb[0]=value; break;
    case PaintParameter::Green: paint.baseColorSrgb[1]=value; break;
    case PaintParameter::Blue: paint.baseColorSrgb[2]=value; break;
    case PaintParameter::Brightness: paint.brightness=value; break;
    case PaintParameter::Shift: paint.shift=value; break;
    case PaintParameter::Angle: paint.rotationDegrees=value; break;
    case PaintParameter::DarkPoint: paint.darkPoint=value; break;
    case PaintParameter::LightPoint: paint.lightPoint=value; break;
    }
}
inline bool SamePaint(const SimplePaint::Parameters& a,const SimplePaint::Parameters& b)
{
    return a.baseColorSrgb==b.baseColorSrgb && a.brightness==b.brightness && a.shift==b.shift &&
        a.rotationDegrees==b.rotationDegrees && a.darkPoint==b.darkPoint && a.lightPoint==b.lightPoint;
}
inline int SliderPosition(double value,PaintParameter parameter)
{
    const auto range=ParameterRange(parameter);
    if(!std::isfinite(value) || value<range.minimum || value>range.maximum) throw std::invalid_argument("Paint value is outside its supported slider domain.");
    // Visual thumb approximation only. Never assign this rounded value back to
    // a loaded material, a numeric edit, or a continuous slow-drag value.
    return static_cast<int>(std::llround((value-range.minimum)/(range.maximum-range.minimum)*range.steps));
}
inline double SliderValue(int position,PaintParameter parameter)
{
    const auto range=ParameterRange(parameter);
    if(position<0 || position>range.steps) throw std::invalid_argument("Invalid slider position.");
    return range.minimum+(range.maximum-range.minimum)*position/range.steps;
}
inline void ValidateDragSpeed(double speed)
{
    if(!std::isfinite(speed) || speed<.0001 || speed>10) throw std::invalid_argument("Drag speed must be in [0.0001,10]. Use 1 for normal, 0.1 for fine, or 0.01 for very fine.");
}
inline double DraggedValue(double value,double pixels,double channelWidth,double speed,PaintParameter parameter)
{
    ValidateDragSpeed(speed);
    if(!std::isfinite(pixels) || !std::isfinite(channelWidth) || channelWidth<1) throw std::invalid_argument("Invalid slider drag coordinates.");
    const auto range=ParameterRange(parameter);
    // Constrain intentional pointer movement to the shader's real domain.
    // No conversion through the integer thumb position occurs here.
    return std::clamp(value+pixels/channelWidth*(range.maximum-range.minimum)*speed,range.minimum,range.maximum);
}
