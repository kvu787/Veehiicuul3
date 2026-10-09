#include "RelativeSliderPointer.h"
#include <algorithm>
#include <stdexcept>

void RelativeSliderPointer::Begin(HWND target,HWND slider,POINT anchor)
{
    if(Active()) throw std::logic_error("A relative slider drag is already active.");
    previousClip_=platform_.ReadClip();
    slider_=slider;
    try
    {
        platform_.RegisterMouse(target); registered_=true;
        captured_=true; platform_.Capture(slider);
        int visibility=0;
        do
        {
            visibility=platform_.Show(false); ++hiddenAdjustments_;
            if(hiddenAdjustments_==128 && visibility>=0) throw std::runtime_error("Could not hide the slider cursor safely.");
        } while(visibility>=0);
        platform_.HideImage();
        clipped_=true; platform_.Clip({anchor.x,anchor.y,anchor.x+1,anchor.y+1});
    }
    catch(...) { End({},false); throw; }
}
void RelativeSliderPointer::End(POINT restore,bool reposition) noexcept
{
    const HWND slider=slider_; slider_=nullptr; // Capture release can reenter.
    if(registered_) { registered_=false; platform_.UnregisterMouse(); }
    if(clipped_) { clipped_=false; platform_.RestoreClip(previousClip_); }
    if(slider && reposition) platform_.Position(restore);
    const bool hid=hiddenAdjustments_!=0;
    while(hiddenAdjustments_) { --hiddenAdjustments_; platform_.Show(true); }
    if(hid) platform_.RestoreImage();
    if(captured_) { captured_=false; platform_.Release(slider); }
}
POINT SliderCursorPoint(RECT thumb,RECT visible)
{
    if(visible.right<=visible.left || visible.bottom<=visible.top) throw std::invalid_argument("Slider has no visible cursor restore area.");
    return {std::clamp(thumb.left+(thumb.right-thumb.left)/2,visible.left,visible.right-1),
            std::clamp(thumb.top+(thumb.bottom-thumb.top)/2,visible.top,visible.bottom-1)};
}
RECT Win32SliderPointerPlatform::ReadClip()
{
    if(simulated_) return {-100000,-100000,100000,100000};
    RECT rectangle{};
    if(!GetClipCursor(&rectangle)) throw std::runtime_error("Could not save the cursor clipping rectangle.");
    return rectangle;
}
void Win32SliderPointerPlatform::RegisterMouse(HWND target)
{
    if(simulated_) { simulatedRegistered_=true; return; }
    // Foreground only; retain normal mouse messages for buttons and numeric
    // controls. The slider ignores their movement to avoid counting twice.
    RAWINPUTDEVICE mouse{0x01,0x02,0,target};
    if(!RegisterRawInputDevices(&mouse,1,sizeof(mouse))) throw std::runtime_error("Could not register relative mouse input. Numeric and keyboard paint editing remain available.");
}
void Win32SliderPointerPlatform::Capture(HWND slider)
{
    if(simulated_) { simulatedCapture_=slider; return; }
    SetCapture(slider);
    if(GetCapture()!=slider) throw std::runtime_error("Could not capture the slider mouse.");
}
void Win32SliderPointerPlatform::Clip(RECT rectangle)
{
    if(simulated_) { simulatedClipped_=true; return; }
    if(!ClipCursor(&rectangle)) throw std::runtime_error("Could not anchor the hidden slider cursor.");
}
int Win32SliderPointerPlatform::Show(bool visible) noexcept
{
    if(simulated_) return simulatedVisibility_+=visible ? 1 : -1;
    return ShowCursor(visible ? TRUE : FALSE);
}
void Win32SliderPointerPlatform::HideImage() noexcept { if(!simulated_) SetCursor(nullptr); }
void Win32SliderPointerPlatform::UnregisterMouse() noexcept
{
    if(simulated_) { simulatedRegistered_=false; return; }
    RAWINPUTDEVICE mouse{0x01,0x02,RIDEV_REMOVE,nullptr};
    RegisterRawInputDevices(&mouse,1,sizeof(mouse));
}
void Win32SliderPointerPlatform::RestoreClip(RECT rectangle) noexcept
{
    if(simulated_) { simulatedClipped_=false; return; }
    if(!ClipCursor(&rectangle)) ClipCursor(nullptr); // At least release our confinement if the saved desktop changed.
}
void Win32SliderPointerPlatform::Position(POINT point) noexcept { if(!simulated_) SetCursorPos(point.x,point.y); }
void Win32SliderPointerPlatform::RestoreImage() noexcept { if(!simulated_) SetCursor(LoadCursorW(nullptr,IDC_ARROW)); }
void Win32SliderPointerPlatform::Release(HWND slider) noexcept
{
    if(simulated_) { if(simulatedCapture_==slider) simulatedCapture_=nullptr; return; }
    if(GetCapture()==slider) ReleaseCapture();
}
