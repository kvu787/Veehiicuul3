#pragma once
#include <windows.h>

// Cursor ownership is separate from paint history. The same guard can run
// against a fake platform, so automated tests never touch the user's cursor.
class SliderPointerPlatform
{
public:
    virtual ~SliderPointerPlatform()=default;
    virtual RECT ReadClip()=0;
    virtual void RegisterMouse(HWND target)=0;
    virtual void Capture(HWND slider)=0;
    virtual void Clip(RECT rectangle)=0;
    virtual int Show(bool visible) noexcept=0;
    virtual void HideImage() noexcept=0;
    virtual void UnregisterMouse() noexcept=0;
    virtual void RestoreClip(RECT rectangle) noexcept=0;
    virtual void Position(POINT point) noexcept=0;
    virtual void RestoreImage() noexcept=0;
    virtual void Release(HWND slider) noexcept=0;
};
class RelativeSliderPointer
{
public:
    explicit RelativeSliderPointer(SliderPointerPlatform& platform) : platform_(platform) {}
    ~RelativeSliderPointer() { End({},false); }
    RelativeSliderPointer(const RelativeSliderPointer&)=delete;
    RelativeSliderPointer& operator=(const RelativeSliderPointer&)=delete;
    void Begin(HWND target,HWND slider,POINT anchor);
    void End(POINT restore,bool reposition=true) noexcept;
    bool Active() const noexcept { return slider_!=nullptr; }
private:
    SliderPointerPlatform& platform_;
    HWND slider_=nullptr;
    RECT previousClip_{};
    bool registered_=false,clipped_=false,captured_=false;
    unsigned hiddenAdjustments_=0;
};
// Clamp the thumb center into the actually visible portion of its control.
POINT SliderCursorPoint(RECT thumb,RECT visible);

class Win32SliderPointerPlatform final : public SliderPointerPlatform
{
public:
    explicit Win32SliderPointerPlatform(bool simulated) : simulated_(simulated) {}
    RECT ReadClip() override;
    void RegisterMouse(HWND target) override;
    void Capture(HWND slider) override;
    void Clip(RECT rectangle) override;
    int Show(bool visible) noexcept override;
    void HideImage() noexcept override;
    void UnregisterMouse() noexcept override;
    void RestoreClip(RECT rectangle) noexcept override;
    void Position(POINT point) noexcept override;
    void RestoreImage() noexcept override;
    void Release(HWND slider) noexcept override;
    bool SimulatedClean() const noexcept { return !simulatedCapture_ && !simulatedRegistered_ && simulatedVisibility_==0 && !simulatedClipped_; }
private:
    bool simulated_;
    HWND simulatedCapture_=nullptr;
    bool simulatedRegistered_=false,simulatedClipped_=false;
    int simulatedVisibility_=0;
};
