#pragma once
#include "Racing2D.h"
#include <algorithm>
#include <array>
namespace InputActions
{
struct Edge { uint32_t pressed=0,released=0; uint64_t timestamp=0; };
// Bounded callback transition buffer. Held buttons and duplicate snapshots do
// not generate another action. Focus/device transitions establish a baseline.
class Buttons
{
public:
    void Reset() { count_=0; initialized_=false; previous_=0; }
    void Observe(uint32_t buttons,uint64_t timestamp)
    {
        if(!initialized_) { previous_=buttons; initialized_=true; return; }
        const auto changed=buttons^previous_; previous_=buttons;
        if(!changed) return;
        if(count_==edges_.size()) { ++dropped_; return; }
        edges_[count_++]={changed&buttons,changed&~buttons,timestamp};
    }
    std::vector<Edge> Take() { std::vector<Edge> result(edges_.begin(),edges_.begin()+count_); count_=0; return result; }
    uint64_t Dropped() const { return dropped_; }
private:
    std::array<Edge,128> edges_{};
    size_t count_=0;
    uint32_t previous_=0;
    bool initialized_=false;
    uint64_t dropped_=0;
};
inline double Axis(double value,double inner,double outer=.95)
{
    const auto amount=std::abs(value);
    if(amount<=inner) return 0;
    return std::copysign(std::clamp((amount-inner)/(outer-inner),0.0,1.0),value);
}
inline Racing2D::Point Acceleration(double x,double y,double innerX,double innerY)
{
    Racing2D::Point value{Axis(x,innerX),Axis(y,innerY)};
    const auto length=value.Length(); return length>1 ? value*(1/length) : value;
}
}
