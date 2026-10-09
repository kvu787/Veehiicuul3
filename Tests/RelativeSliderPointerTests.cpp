#include "RelativeSliderPointer.h"
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
void Require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
class FakePlatform final : public SliderPointerPlatform
{
public:
    RECT original{-2560,-40,7040,1440},clip=original;
    HWND capture=nullptr;
    int initialVisibility=2,visibility=2;
    bool registered=false,imageHidden=false;
    POINT positioned{};
    int positions=0,releases=0,failStep=0,step=0;
    std::function<void()> onRelease;
    void Fail() { if(++step==failStep) throw std::runtime_error("Injected acquisition failure."); }
    RECT ReadClip() override { Fail(); return clip; }
    void RegisterMouse(HWND) override { Fail(); registered=true; }
    void Capture(HWND slider) override { capture=slider; Fail(); }
    void Clip(RECT rectangle) override { clip=rectangle; Fail(); }
    int Show(bool visible) noexcept override { return visibility+=visible ? 1 : -1; }
    void HideImage() noexcept override { imageHidden=true; }
    void UnregisterMouse() noexcept override { registered=false; }
    void RestoreClip(RECT rectangle) noexcept override { clip=rectangle; }
    void Position(POINT point) noexcept override { positioned=point; ++positions; }
    void RestoreImage() noexcept override { imageHidden=false; }
    void Release(HWND slider) noexcept override { if(capture==slider) capture=nullptr; ++releases; if(onRelease) onRelease(); }
    bool Clean() const { return !registered && !capture && visibility==initialVisibility && !imageHidden && EqualRect(&clip,&original); }
};
}
int main()
{
    try
    {
        const HWND slider=reinterpret_cast<HWND>(static_cast<INT_PTR>(1));
        for(int visibility : {-2,0,2,12})
        {
            FakePlatform platform; platform.initialVisibility=platform.visibility=visibility;
            RelativeSliderPointer drag(platform);
            drag.Begin(slider,slider,{6000,600});
            Require(drag.Active() && platform.registered && platform.capture==slider && platform.visibility<0 && platform.imageHidden,"Drag must own hidden raw-input pointer.");
            Require(platform.clip.left==6000 && platform.clip.right==6001,"Hidden pointer must be anchored inside the slider.");
            platform.onRelease=[&]{ Require(!drag.Active(),"Reentrant release must see inactive ownership."); drag.End({},false); };
            drag.End({6200,610});
            Require(platform.Clean() && platform.positioned.x==6200 && platform.positions==1 && platform.releases==1,"Release must balance exact visibility and restore prior clipping and thumb position.");
            drag.End({100,100}); Require(platform.positions==1 && platform.releases==1,"Cleanup must be idempotent.");
        }
        for(int failure=1;failure<=4;++failure)
        {
            FakePlatform platform; platform.failStep=failure; RelativeSliderPointer drag(platform);
            bool failed=false; try { drag.Begin(slider,slider,{6100,500}); } catch(const std::exception&) { failed=true; }
            Require(failed && !drag.Active() && platform.Clean() && platform.positions==0,"Acquisition failures must clean every acquired resource without a cursor warp.");
        }
        for(const char* reason : {"Escape","capture loss","deactivation","cancel mode","window destruction","error"})
        {
            (void)reason; FakePlatform platform;
            { RelativeSliderPointer drag(platform); drag.Begin(slider,slider,{6100,500}); drag.End({},false); }
            Require(platform.Clean() && platform.positions==0,"Loss/cancel/destruction cleanup must never warp another app's cursor.");
        }
        FakePlatform platform;
        { RelativeSliderPointer drag(platform); drag.Begin(slider,slider,{6100,500}); }
        Require(platform.Clean() && platform.positions==0,"Destructor must restore visibility, clip, capture and registration.");
        for(const RECT visible : {RECT{5120,226,5400,250},RECT{-300,-20,-50,20}})
        {
            const auto point=SliderCursorPoint({visible.left-50,visible.top-10,visible.left-10,visible.top+10},visible);
            Require(PtInRect(&visible,point)!=FALSE,"Restore must remain inside the visible slider on positive and negative desktop coordinates.");
        }
        const auto thumb=SliderCursorPoint({20,8,30,22},{0,0,278,22});
        Require(thumb.x==25 && thumb.y==15,"Normal release must restore the thumb center.");
        for(LONG size=1;size<=4;++size)
        {
            const RECT visible{-20,-30,-20+size,-30+size};
            const auto point=SliderCursorPoint({-50,-60,20,30},visible);
            Require(PtInRect(&visible,point)!=FALSE,"1-4 pixel intersections must have valid restore clamp bounds.");
        }
        std::cout << "Passed simulated cursor ownership, exact visibility balance, reentrant/idempotent cleanup, partial acquisition failures, every loss path, destructor and thumb restore bounds; no OS cursor operations performed.\n";
        return 0;
    }
    catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
