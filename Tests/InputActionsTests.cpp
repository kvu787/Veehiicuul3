#include "InputActions.h"
#include <iostream>
#include <stdexcept>
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
int main()
{
    try
    {
        InputActions::Buttons buttons; buttons.Observe(0,1); buttons.Observe(16,2); buttons.Observe(0,3); buttons.Observe(16,4); buttons.Observe(16,5);
        const auto edges=buttons.Take(); Require(edges.size()==3 && edges[0].pressed==16 && edges[1].released==16 && edges[2].pressed==16,"Press/release transitions between updates must survive; held buttons must not repeat.");
        Require(buttons.Take().empty(),"Transitions must be consumed exactly once.");
        buttons.Reset(); buttons.Observe(16,6); Require(buttons.Take().empty(),"Focus/reconnect held state must establish a fresh baseline.");
        Require(InputActions::Axis(.01,.1)==0 && InputActions::Axis(1,.1)==1 && InputActions::Axis(-1,.1)==-1,"Deadzone endpoints must preserve sign and bounds.");
        Require(std::abs(InputActions::Axis(.5,.1)-(.4/.85))<1e-12,"Axis must have no extra smoothing.");
        Require(std::abs(InputActions::Acceleration(1,1,.1,.05).Length()-1)<1e-12,"Diagonal analog acceleration must be normalized.");
        for(unsigned i=0;i<300;++i) buttons.Observe(i%2,100+i); Require(buttons.Dropped()>0,"Overflow must be counted visibly.");
        std::cout<<"PASS: buffered button edges, baseline resets, one consumption, documented deadzones, no smoothing, diagonal clamp and bounded overflow.\n"; return 0;
    }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
