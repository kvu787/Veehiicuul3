#pragma once
#include "Editor.h"
#include "Renderer.h"
#include "Display.h"
#include "GamepadInput.h"
#include "RelativeSliderPointer.h"
#include "DisplayTracker.h"
#include "ConsumedDisplayInputs.h"
#include <chrono>

struct RunOptions { bool software=false,hidden=false,smoke=false; unsigned traceSeconds=0; std::filesystem::path session; };
class App
{
public:
    App(HINSTANCE instance,Display display,Presentation presentation,RunOptions options);
    ~App();
    int Run();
private:
    using Clock=std::chrono::steady_clock;
    void Pump(std::optional<Clock::time_point> deadline=std::nullopt);
    void RefreshVisibility();
    void VisibilityChanged();
    static void CALLBACK CloakChanged(HWINEVENTHOOK,DWORD,HWND,LONG,LONG,DWORD,DWORD);
    static LRESULT CALLBACK Procedure(HWND,UINT,WPARAM,LPARAM);
    LRESULT Message(UINT,WPARAM,LPARAM);
    void Interface();
    void Actions(const std::vector<Ui::Action>& actions);
    void Command(int id,double value=0);
    void Change(bool mesh=true);
    void Mode(EditorMode mode);
    void Geometry();
    void Overlay();
    void Draw(bool present=true);
    void Tick(double seconds,bool synthetic=false);
    void Pick(float x,float y,bool extend);
    Racing2D::Point Ground(float x,float y) const;
    void Pointer(UINT message,float x,float y,WPARAM buttons);
    void Key(UINT key);
    void TransformKey(UINT key,bool shift);
    void Abort(bool restore=false);
    POINT SliderPoint() const;
    void RawMouse(HRAWINPUT input);
    void File(int action);
    void SaveModel(const std::filesystem::path& path);
    void OpenModel(const std::filesystem::path& path);
    void TestUiTransactions();
    void TestProductionLoop();
    void TestWorkflow();
    int Tests();
    Camera& ViewCamera() { return editor_.mode==EditorMode::ModelBuilder ? editor_.modelCamera : editor_.trackCamera; }
    HINSTANCE instance_;
    HWND window_=nullptr;
    Display display_;
    Presentation presentation_;
    RunOptions options_;
    Editor editor_;
    Ui::State ui_;
    Ui::Schedule schedule_;
    Ui::Visibility visibility_;
    TextAtlas text_;
    Renderer renderer_;
    Win32SliderPointerPlatform pointerPlatform_;
    RelativeSliderPointer relative_;
    GamepadInput gamepad_;
    std::unique_ptr<DisplayTracker> tracker_;
    ConsumedDisplayInputs displayInputs_;
    uint64_t displayFrame_=0,wakes_=0,ticks_=0,testTracedPresents_=0;
    Scene shown_;
    ViewGeometry geometry_;
    std::array<SimplePaint::GpuMaterial,32> paints_{};
    std::array<std::array<float,4>,32> surfaces_{};
    std::wstring status_=L"Model builder | Select a part to edit its paint. Right drag orbits; middle drag pans; wheel zooms.";
    std::filesystem::path modelPath_,trackPath_;
    bool running_=true,meshDirty_=true,overlayDirty_=true,orbit_=false,pan_=false,modelDrag_=false,trackDrag_=false;
    bool modifiedModel_=false,modifiedTrack_=false,foreground_=false;
    bool sessionRegistered_=false,syntheticVisibility_=false,testPresentProbe_=false;
    bool resetClock_=true,testDesktopAvailable_=true,continuousProbe_=false,checkResumeStep_=false;
    uint64_t pausedTicks_=0,pausedPresents_=0,resumeChecks_=0;
    HPOWERNOTIFY displayPowerNotification_=nullptr;
    HWINEVENTHOOK cloakHook_=nullptr;
    int menu_=0,slider_=0;
    float previousX_=0,previousY_=0;
    ModelState dragStart_;
    TrackProject trackStart_;
    Vector3 dragPlane_;
    double accumulator_=0,statusClock_=0;
    std::array<bool,256> held_{};
    static constexpr Ui::Rect view_{438,62,2122,1316};
};
