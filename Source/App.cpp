#include "App.h"
#include "SurfaceMaterials.h"
#include <commdlg.h>
#include <windowsx.h>
#include <wtsapi32.h>
#include <dwmapi.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <numbers>

namespace
{
enum Id { ModeMenu=1,FileMenu,UndoAction,RedoAction,FrameAction,CageAction,QuitAction,
    ModelMode=20,TrackMode,DriveMode,NewCar=30,NewPrimitive,OpenProject,SaveFile,SaveAs,ExportFile,
    AddBox=40,AddPlane,AddCylinder,ObjectTarget=50,VertexTarget,FaceTarget,RefineAction,ExtrudeAction,CopyPaint,AssignPaint,ResetPaint,
    NewTrack=100,ExampleTrack,BuildTrack,SelectTool,OutlineTool,FinishOutline,CancelDraft,SpawnTool,FinishTool,CheckpointTool,DecorationTool,UseVehicle,UseDecoration,
    RemoveOutline,RemoveCheckpoint,RemoveDecoration,
    Speed=2300,PreviewLevel,EditStep,SpawnX=3000,SpawnY,SpawnHeading,VehicleScale,PointX,PointY,PointWeight,DecorX,DecorY,DecorHeading,DecorHeight,DecorScale,DeadzoneX,DeadzoneY };
constexpr int ObjectFirst=1000,MaterialFirst=1100,OutlineFirst=1200,PointFirst=1300,DecorFirst=1400,SliderFirst=2000,PaintFirst=2100,TransformFirst=2200;
std::wstring Wide(const std::string& s)
{
    const auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    if(n<=0) return L"Invalid UTF-8";
    std::wstring result(static_cast<size_t>(n),L' '); MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),result.data(),n); return result;
}
void Line(std::vector<OverlayVertex>& output,Vector3 a,Vector3 b,Vector3 c)
{ output.push_back({a.x,a.y,a.z,c.x,c.y,c.z,1}); output.push_back({b.x,b.y,b.z,c.x,c.y,c.z,1}); }
Vector3 World(Racing2D::Point p,float height=.06f) { return {static_cast<float>(p.x),height,static_cast<float>(-p.y)}; }
bool RayTriangle(Vector3 origin,Vector3 direction,Vector3 a,Vector3 b,Vector3 c,float& distance)
{
    const auto e1=b-a,e2=c-a,p=Cross(direction,e2); const auto determinant=Dot(e1,p);
    if(std::abs(determinant)<1e-8f) return false;
    const auto t=origin-a; const auto u=Dot(t,p)/determinant; if(u<0 || u>1) return false;
    const auto q=Cross(t,e1); const auto v=Dot(direction,q)/determinant; if(v<0 || u+v>1) return false;
    distance=Dot(e2,q)/determinant; return distance>=0;
}
void Require(bool condition,const char* text) { if(!condition) throw std::runtime_error(text); }
}
App::App(HINSTANCE instance,Display display,Presentation p,RunOptions options) : instance_(instance),display_(std::move(display)),presentation_(p),options_(std::move(options)),pointerPlatform_(options_.hidden || options_.smoke),relative_(pointerPlatform_)
{
    WNDCLASSEXW c{sizeof(c)}; c.hInstance=instance_; c.lpfnWndProc=Procedure; c.lpszClassName=L"Veehiicuul3GpuWindow"; c.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if(!RegisterClassExW(&c) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error("Register application window failed.");
    const auto extra=(options_.hidden || options_.smoke) ? WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW : 0;
    window_=CreateWindowExW(extra,c.lpszClassName,L"Veehiicuul3",WS_POPUP, p.originX,p.originY,p.width,p.height,nullptr,nullptr,instance_,this);
    if(!window_) throw std::runtime_error("Create borderless application window failed.");
    renderer_.Initialize(window_,options_.software,true); text_.Initialize();
    wchar_t exe[32768]{}; GetModuleFileNameW(nullptr,exe,32768); gamepad_.Initialize(std::filesystem::path(exe).parent_path());
    sessionRegistered_=WTSRegisterSessionNotification(window_,NOTIFY_FOR_THIS_SESSION)!=FALSE;
    if(!sessionRegistered_) throw std::runtime_error("Register session visibility notifications failed.");
    displayPowerNotification_=RegisterPowerSettingNotification(window_,&GUID_CONSOLE_DISPLAY_STATE,DEVICE_NOTIFY_WINDOW_HANDLE);
    if(!displayPowerNotification_) throw std::runtime_error("Register display power notifications failed.");
    cloakHook_=SetWinEventHook(EVENT_OBJECT_CLOAKED,EVENT_OBJECT_UNCLOAKED,nullptr,CloakChanged,GetCurrentProcessId(),0,WINEVENT_OUTOFCONTEXT);
    if(!cloakHook_) throw std::runtime_error("Register window cloaking notifications failed.");
    RefreshVisibility();
    editor_.Frame(); editor_.trackCamera.height=100; editor_.trackCamera.pitch=1.1f; editor_.trackCamera.yaw=0;
}
App::~App()
{
    Abort(); if(tracker_) tracker_->Stop(); gamepad_.Stop();
    renderer_.StopVisibilityNotifications();
    if(cloakHook_) UnhookWinEvent(cloakHook_);
    if(displayPowerNotification_) UnregisterPowerSettingNotification(displayPowerNotification_);
    if(sessionRegistered_ && window_) WTSUnRegisterSessionNotification(window_);
    if(window_) DestroyWindow(window_);
}
LRESULT CALLBACK App::Procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam)
{
    auto* app=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) { app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams); app->window_=window; SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app)); }
    if(!app) return DefWindowProcW(window,message,wparam,lparam);
    try { return app->Message(message,wparam,lparam); }
    catch(const std::exception& e) { app->Abort(); app->status_=Wide(e.what()); app->schedule_.dirty=true; return 0; }
}
void App::Change(bool mesh)
{
    meshDirty_=meshDirty_ || mesh; overlayDirty_=true; schedule_.dirty=true;
}
void CALLBACK App::CloakChanged(HWINEVENTHOOK,DWORD,HWND window,LONG object,LONG,DWORD,DWORD)
{
    if(window && object==OBJID_WINDOW) PostMessageW(window,Renderer::VisibilityMessage,0,0);
}
void App::VisibilityChanged()
{
    const bool wasBlocked=schedule_.occluded; schedule_.occluded=visibility_.Blocked();
    if(schedule_.occluded && !wasBlocked) { Abort(); accumulator_=0; }
    if(!schedule_.occluded && wasBlocked) schedule_.dirty=true;
    gamepad_.SetForeground(schedule_.WantsTimer());
}
void App::RefreshVisibility()
{
    if(!syntheticVisibility_)
    {
        visibility_.shown=IsWindowVisible(window_)!=FALSE; schedule_.minimized=IsIconic(window_)!=FALSE;
        DWORD cloaked=0; visibility_.cloaked=SUCCEEDED(DwmGetWindowAttribute(window_,DWMWA_CLOAKED,&cloaked,sizeof(cloaked))) && cloaked!=0;
        visibility_.desktopAvailable=InputDesktopAvailable();
    }
    VisibilityChanged();
}
void App::Interface()
{
    ui_.Begin(); float y=8;
    const Ui::Rect all{0,0,2560,1440};
    auto top=[&](int id,const wchar_t* text,float x,float w,bool enabled=true) { Ui::Control c; c.id=id; c.kind=Ui::Kind::Button; c.rect={x,12,w,38}; c.clip=all; c.text=text; c.enabled=enabled; ui_.Add(c,false); };
    top(ModeMenu,L"Mode",12,115); top(FileMenu,L"File",137,100); top(UndoAction,L"Undo",255,100,editor_.CanUndo(false)); top(RedoAction,L"Redo",365,100,editor_.CanUndo(true)); top(FrameAction,L"Frame all",483,145);
    top(CageAction,editor_.cage ? L"Cage: on" : L"Cage: off",640,145,editor_.mode==EditorMode::ModelBuilder); top(QuitAction,L"Exit",2440,108);
    Ui::Control title; title.text=L"Veehiicuul3   |   "+std::wstring(editor_.mode==EditorMode::ModelBuilder ? L"Model builder" : editor_.mode==EditorMode::TrackBuilder ? L"Track builder" : L"Drive"); title.rect={820,12,1000,38}; title.clip=all; ui_.Add(title,false);
    auto label=[&](const std::wstring& text) { Ui::Control c; c.rect={12,y,404,34}; c.text=text; ui_.Add(c); y+=36; };
    auto button=[&](int id,const std::wstring& text,bool enabled=true,bool selected=false) { Ui::Control c; c.id=id; c.kind=Ui::Kind::Button; c.rect={12,y,404,36}; c.text=text; c.enabled=enabled; c.selected=selected; ui_.Add(c); y+=42; };
    auto number=[&](int id,const wchar_t* name,double value,double minimum,double maximum,bool enabled=true) { Ui::Control l; l.rect={12,y,158,38}; l.text=name; l.enabled=enabled; ui_.Add(l); Ui::Control c; c.id=id; c.kind=Ui::Kind::Number; c.rect={175,y,241,38}; c.value=value; c.minimum=minimum; c.maximum=maximum; c.enabled=enabled; ui_.Add(c); y+=44; };
    if(editor_.mode==EditorMode::ModelBuilder)
    {
        label(L"Parts");
        for(size_t i=0;i<editor_.model.scene.objects.size();++i) button(ObjectFirst+static_cast<int>(i),Wide(editor_.model.scene.objects[i].name),true,editor_.model.selection.object==static_cast<int>(i));
        button(AddBox,L"+ Box"); button(AddPlane,L"+ Plane"); button(AddCylinder,L"+ Cylinder");
        const bool has=editor_.model.selection.object>=0,editable=editor_.Editable();
        if(has)
        {
            label(L"Edit target"); button(ObjectTarget,L"Whole part",true,editor_.model.selection.mode==SelectionMode::Object); button(VertexTarget,L"Control vertices",editable,editor_.model.selection.mode==SelectionMode::Vertex); button(FaceTarget,L"Control face",editable,editor_.model.selection.mode==SelectionMode::Face);
            auto& object=editor_.Object();
            number(PreviewLevel,L"Preview level",object.subdivisionLevel,0,4,editable);
            number(EditStep,L"Edit step",editor_.step,.0001,100);
            button(RefineAction,L"Refine cage",editable); button(ExtrudeAction,L"Extrude selected face",editable && editor_.model.selection.face>=0);
            label(L"Transform (local)"); const auto center=editor_.Center(); const bool whole=editor_.model.selection.mode==SelectionMode::Object;
            const auto rotation=whole ? object.rotationDegrees : Vector3{}; const float scale=whole ? object.scale : 1;
            number(TransformFirst,L"Position X",center.x,-10000,10000); number(TransformFirst+1,L"Position Y",center.y,-10000,10000); number(TransformFirst+2,L"Position Z",center.z,-10000,10000);
            number(TransformFirst+3,L"Rotation X",rotation.x,-3600,3600); number(TransformFirst+4,L"Rotation Y",rotation.y,-3600,3600); number(TransformFirst+5,L"Rotation Z",rotation.z,-3600,3600); number(TransformFirst+6,L"Scale",scale,1./1024,1024);
            label(L"Material slots");
            for(size_t i=0;i<MaterialCount(object);++i) button(MaterialFirst+static_cast<int>(i),L"Material "+std::to_wstring(i+1),true,editor_.model.selection.material==i);
            button(CopyPaint,L"+ Copy selected material",editable); button(AssignPaint,L"Assign material to selected face",editable && editor_.model.selection.face>=0);
            label(Wide(object.name)+L" | Paint");
            const wchar_t* names[]={L"Red",L"Green",L"Blue",L"Brightness",L"Shift",L"Angle",L"Darkpoint",L"Lightpoint"};
            const auto& paint=editor_.Material().paint; const bool painted=editor_.Material().materialKind==MaterialKind::SimplePaint;
            for(int i=0;i<8;++i)
            {
                const auto parameter=static_cast<PaintParameter>(i); const auto range=ParameterRange(parameter);
                number(PaintFirst+i,names[i],ParameterValue(paint,parameter),range.minimum,range.maximum,painted);
                Ui::Control c; c.id=SliderFirst+i; c.kind=Ui::Kind::Slider; c.rect={12,y,404,30}; c.value=ParameterValue(paint,parameter); c.minimum=range.minimum; c.maximum=range.maximum; c.enabled=painted; ui_.Add(c); y+=36;
            }
            number(Speed,L"Drag speed (x)",editor_.model.scene.sliderDragSpeed,.0001,10);
            button(ResetPaint,L"Reset selected part to source paint",object.evaluatedSurface.has_value());
        }
    }
    else if(editor_.mode==EditorMode::TrackBuilder)
    {
        label(L"NURBS track"); button(NewTrack,L"New empty track"); button(ExampleTrack,L"Example circuit"); button(BuildTrack,L"Build surfaces");
        label(L"Pointer tool");
        button(SelectTool,L"Select / move control point",true,editor_.tool==Editor::Tool::Select); button(OutlineTool,L"Draw outline (ground, road, islands)",true,editor_.tool==Editor::Tool::Outline);
        button(FinishOutline,L"Finish periodic cubic outline",editor_.draft.size()>=4); button(CancelDraft,L"Cancel outline / gate draft",!editor_.draft.empty() || editor_.gateStart.has_value());
        button(SpawnTool,L"Place vehicle start",true,editor_.tool==Editor::Tool::Spawn); button(FinishTool,L"Place checkered line (2 points)",true,editor_.tool==Editor::Tool::Finish); button(CheckpointTool,L"Add checkpoint gate (2 points)",true,editor_.tool==Editor::Tool::Checkpoint); button(DecorationTool,L"Place decoration",true,editor_.tool==Editor::Tool::Decoration);
        button(UseVehicle,L"Use model scene as vehicle"); button(UseDecoration,L"Use model scene as decoration");
        label(L"Outlines");
        for(size_t i=0;i<editor_.track.track.outlines.size();++i) button(OutlineFirst+static_cast<int>(i),Wide(editor_.track.track.outlines[i].name),true,editor_.outline==static_cast<int>(i));
        button(RemoveOutline,L"Remove selected outline",editor_.outline>=0 && static_cast<size_t>(editor_.outline)<editor_.track.track.outlines.size());
        if(editor_.outline>=0 && static_cast<size_t>(editor_.outline)<editor_.track.track.outlines.size())
        {
            const auto& curve=editor_.track.track.outlines[static_cast<size_t>(editor_.outline)];
            for(size_t i=0;i<curve.controls.size();++i) button(PointFirst+static_cast<int>(i),L"Control point "+std::to_wstring(i+1),true,editor_.point==static_cast<int>(i));
            if(editor_.point>=0 && static_cast<size_t>(editor_.point)<curve.controls.size()) { const auto p=curve.controls[static_cast<size_t>(editor_.point)]; number(PointX,L"Point X",p.position.x,-10000,10000); number(PointY,L"Point Y",p.position.y,-10000,10000); number(PointWeight,L"NURBS weight",p.weight,.001,1000); }
        }
        label(L"Vehicle start"); number(SpawnX,L"Start X",editor_.track.track.spawn.position.x,-10000,10000); number(SpawnY,L"Start Y",editor_.track.track.spawn.position.y,-10000,10000); number(SpawnHeading,L"Heading (rad)",editor_.track.track.spawn.heading,-10000,10000); number(VehicleScale,L"Vehicle scale",editor_.track.vehicleScale,.05,10);
        number(DeadzoneX,L"Stick deadzone X",editor_.track.deadzoneX,0,.94999); number(DeadzoneY,L"Stick deadzone Y",editor_.track.deadzoneY,0,.94999);
        label(L"Checkpoints: "+std::to_wstring(editor_.track.track.checkpoints.size())); button(RemoveCheckpoint,L"Remove last checkpoint",!editor_.track.track.checkpoints.empty());
        label(L"Decorations");
        for(size_t i=0;i<editor_.track.decorations.size();++i) button(DecorFirst+static_cast<int>(i),L"Decoration "+std::to_wstring(i+1),true,editor_.decoration==static_cast<int>(i));
        if(editor_.decoration>=0 && static_cast<size_t>(editor_.decoration)<editor_.track.decorations.size())
        {
            const auto& d=editor_.track.decorations[static_cast<size_t>(editor_.decoration)]; number(DecorX,L"Decor X",d.pose.position.x,-10000,10000); number(DecorY,L"Decor Y",d.pose.position.y,-10000,10000); number(DecorHeading,L"Heading (rad)",d.pose.heading,-10000,10000); number(DecorHeight,L"Height",d.height,-100,100); number(DecorScale,L"Scale",d.scale,.05,10); button(RemoveDecoration,L"Remove decoration");
        }
    }
    else
    {
        label(L"Drive"); const auto& car=editor_.race.State(); label(L"Laps: "+std::to_wstring(car.laps)); label(L"Next checkpoint: "+std::to_wstring(car.nextCheckpoint+1)); label(L"Speed: "+std::to_wstring(static_cast<int>(car.velocity.Length()))+L" m/s"); label(L"Boundary resets: "+std::to_wstring(car.collisions));
        label(L"WASD / arrows: acceleration"); label(L"Space: brake   R: reset"); label(L"Right stick / left trigger / X"); button(TrackMode,L"Return to Track builder"); button(ModelMode,L"Return to Model builder");
    }
    ui_.Finish(y+8);
    if(menu_)
    {
        const float x=menu_==1 ? 12.f : 137.f; float my=60;
        auto popup=[&](int id,const wchar_t* name,bool enabled=true,bool selected=false) { Ui::Control c; c.id=id; c.kind=Ui::Kind::Button; c.rect={x,my,340,42}; c.clip=all; c.text=name; c.enabled=enabled; c.selected=selected; ui_.Add(c,false); my+=44; };
        if(menu_==1) { popup(ModelMode,L"Model builder",true,editor_.mode==EditorMode::ModelBuilder); popup(TrackMode,L"Track builder",true,editor_.mode==EditorMode::TrackBuilder); popup(DriveMode,L"Drive",true,editor_.mode==EditorMode::Drive); }
        else { const bool model=editor_.mode==EditorMode::ModelBuilder,editing=editor_.mode!=EditorMode::Drive; popup(NewCar,L"New SlopeCar",model); popup(NewPrimitive,L"New primitive scene",model); popup(OpenProject,L"Open...",editing); popup(SaveFile,L"Save",editing); popup(SaveAs,L"Save as...",editing); popup(ExportFile,L"Export C++ mesh...",model); }
    }
}
void App::Actions(const std::vector<Ui::Action>& actions)
{
    for(const auto& a:actions)
    {
        if(a.kind==Ui::ActionKind::BeginSlider)
        {
            editor_.BeginPaint(); slider_=a.id; POINT anchor{}; GetCursorPos(&anchor);
            relative_.Begin(window_,window_,anchor);
        }
        else if(a.kind==Ui::ActionKind::EndSlider || a.kind==Ui::ActionKind::CancelSlider)
        {
            if(editor_.EndPaint(a.kind==Ui::ActionKind::CancelSlider)) modifiedModel_=true;
            POINT point{}; bool restore=!options_.hidden && !options_.smoke;
            try { point=SliderPoint(); } catch(const std::invalid_argument&) { restore=false; }
            slider_=0; relative_.End(point,restore); Change(false);
        }
        else Command(a.id,a.value);
    }
}
void App::Mode(EditorMode mode)
{
    Abort(); editor_.Mode(mode); ui_.scroll=0; ui_.focus=0; held_.fill(false); accumulator_=0;
    schedule_.active=mode==EditorMode::Drive && foreground_;
    gamepad_.SetForeground(schedule_.WantsTimer());
    if(mode==EditorMode::Drive && options_.traceSeconds)
    {
        tracker_=std::make_unique<DisplayTracker>(options_.session);
        tracker_->Start(options_.traceSeconds,[](void* p) { return static_cast<GamepadInput*>(p)->Now(); },&gamepad_,[](void* p) { return static_cast<GamepadInput*>(p)->InputLossDetected(); });
    }
    else if(tracker_) tracker_->RequestStop();
    displayInputs_.Clear(); Change();
    status_=mode==EditorMode::Drive ? L"Drive | WASD / arrows accelerate, Space brakes, R resets, Escape returns to Track builder." : mode==EditorMode::TrackBuilder ? L"Track builder | Draw NURBS outlines, build surfaces, place a start, checkered line and checkpoints." : L"Model builder | Select a part. Right drag orbits; middle drag pans; wheel zooms. Scroll to paint controls.";
}
void App::Command(int id,double value)
{
    menu_=(id==ModeMenu) ? (menu_==1 ? 0 : 1) : (id==FileMenu) ? (menu_==2 ? 0 : 2) : 0;
    if(id==ModeMenu || id==FileMenu) { Change(false); return; }
    if(id==ModelMode || id==TrackMode || id==DriveMode) { Mode(id==ModelMode ? EditorMode::ModelBuilder : id==TrackMode ? EditorMode::TrackBuilder : EditorMode::Drive); return; }
    if(id==QuitAction) { PostMessageW(window_,WM_CLOSE,0,0); return; }
    if(id==FrameAction) { editor_.Frame(); Change(false); return; }
    if(id==CageAction) { editor_.cage=!editor_.cage; Change(false); return; }
    if(id==UndoAction || id==RedoAction) editor_.Undo(id==RedoAction);
    else if(id==OpenProject || id==SaveFile || id==SaveAs || id==ExportFile) { File(id); return; }
    else if(id==NewCar || id==NewPrimitive) { editor_.ModelEdit([&] { editor_.model.scene=id==NewCar ? MakeSlopeCarScene() : Scene{}; editor_.model.selection={SelectionMode::Object,0,-1,{},0}; }); modelPath_.clear(); editor_.Frame(); }
    else if(id>=ObjectFirst && id<ObjectFirst+32) { editor_.model.selection={SelectionMode::Object,id-ObjectFirst,-1,{},0}; Change(false); return; }
    else if(id>=MaterialFirst && id<MaterialFirst+32) { editor_.model.selection.material=static_cast<uint32_t>(id-MaterialFirst); Change(false); return; }
    else if(id>=OutlineFirst && id<OutlineFirst+6) { editor_.outline=id-OutlineFirst; editor_.point=0; Change(false); return; }
    else if(id>=PointFirst && id<PointFirst+64) { editor_.point=id-PointFirst; Change(false); return; }
    else if(id>=DecorFirst && id<DecorFirst+16) { editor_.decoration=id-DecorFirst; Change(false); return; }
    else if(id==AddBox || id==AddPlane || id==AddCylinder) editor_.AddShape(id-AddBox);
    else if(id==ObjectTarget || id==VertexTarget || id==FaceTarget) { editor_.model.selection.mode=id==ObjectTarget ? SelectionMode::Object : id==VertexTarget ? SelectionMode::Vertex : SelectionMode::Face; editor_.model.selection.vertices.clear(); editor_.model.selection.face=-1; Change(false); return; }
    else if(id==RefineAction) editor_.Refine(); else if(id==ExtrudeAction) editor_.Extrude(); else if(id==CopyPaint) editor_.CopyMaterial(); else if(id==AssignPaint) editor_.AssignMaterial();
    else if(id==PreviewLevel) { if(value!=std::floor(value)) throw std::invalid_argument("Preview level must be an integer."); editor_.ModelEdit([&] { editor_.Object().subdivisionLevel=static_cast<unsigned>(value); }); }
    else if(id==EditStep) { editor_.step=static_cast<float>(value); Change(false); return; }
    else if(id==Speed) { ValidateDragSpeed(value); editor_.model.scene.sliderDragSpeed=value; modifiedModel_=true; Change(false); return; }
    else if(id>=PaintFirst && id<PaintFirst+8) { editor_.PreviewPaint(static_cast<PaintParameter>(id-PaintFirst),value); editor_.EndPaint(); Change(false); modifiedModel_=true; return; }
    else if(id>=TransformFirst && id<TransformFirst+7)
    {
        auto p=editor_.Center(); const auto& object=editor_.Object(); const bool whole=editor_.model.selection.mode==SelectionMode::Object; auto r=whole ? object.rotationDegrees : Vector3{}; auto scale=whole ? object.scale : 1.f;
        const auto v=static_cast<float>(value); switch(id-TransformFirst) { case 0:p.x=v;break;case 1:p.y=v;break;case 2:p.z=v;break;case 3:r.x=v;break;case 4:r.y=v;break;case 5:r.z=v;break;case 6:scale=v;break; } editor_.Transform(p,r,scale);
    }
    else if(id==ResetPaint)
    {
        const auto original=MakeSlopeCarScene(); const auto identity=editor_.Object().sourceMaterial;
        for(const auto& object:original.objects) if(object.sourceMaterial==identity) { editor_.ModelEdit([&] { editor_.Material().paint=object.paint; }); break; }
    }
    else if(id==NewTrack || id==ExampleTrack) { editor_.NewTrack(id==ExampleTrack); trackPath_.clear(); }
    else if(id==BuildTrack) editor_.BuildTrack();
    else if(id==SelectTool || id==OutlineTool || id==SpawnTool || id==FinishTool || id==CheckpointTool || id==DecorationTool)
    {
        editor_.tool=id==SelectTool ? Editor::Tool::Select : id==OutlineTool ? Editor::Tool::Outline : id==SpawnTool ? Editor::Tool::Spawn : id==FinishTool ? Editor::Tool::Finish : id==CheckpointTool ? Editor::Tool::Checkpoint : Editor::Tool::Decoration;
        status_=L"Click in the viewport. Ground plane coordinates are in meters; gate order is preserved."; Change(false); return;
    }
    else if(id==FinishOutline) editor_.FinishOutline();
    else if(id==CancelDraft) { editor_.draft.clear(); editor_.gateStart.reset(); Change(false); return; }
    else if(id==UseVehicle || id==UseDecoration) editor_.UseAsset(id==UseVehicle);
    else if(id==RemoveOutline) editor_.TrackEdit([&] { editor_.track.track.outlines.erase(editor_.track.track.outlines.begin()+editor_.outline); editor_.outline=std::min(editor_.outline,static_cast<int>(editor_.track.track.outlines.size())-1); editor_.point=0; });
    else if(id==RemoveCheckpoint) editor_.TrackEdit([&] { editor_.track.track.checkpoints.pop_back(); });
    else if(id==RemoveDecoration) editor_.TrackEdit([&] { editor_.track.decorations.erase(editor_.track.decorations.begin()+editor_.decoration); editor_.decoration=0; });
    else if(id>=SpawnX && id<=DeadzoneY)
    {
        editor_.TrackEdit([&] {
            auto& track=editor_.track;
            switch(id) { case SpawnX:track.track.spawn.position.x=value;track.track.hasSpawn=true;break;case SpawnY:track.track.spawn.position.y=value;track.track.hasSpawn=true;break;case SpawnHeading:track.track.spawn.heading=value;break;case VehicleScale:track.vehicleScale=value;break;case DeadzoneX:track.deadzoneX=value;break;case DeadzoneY:track.deadzoneY=value;break;
            case PointX:case PointY:case PointWeight: { auto& p=track.track.outlines.at(static_cast<size_t>(editor_.outline)).controls.at(static_cast<size_t>(editor_.point)); if(id==PointX) p.position.x=value; else if(id==PointY) p.position.y=value; else p.weight=value; break; }
            default: { auto& d=track.decorations.at(static_cast<size_t>(editor_.decoration)); if(id==DecorX) d.pose.position.x=value; else if(id==DecorY) d.pose.position.y=value; else if(id==DecorHeading) d.pose.heading=value; else if(id==DecorHeight) d.height=value; else if(id==DecorScale) d.scale=value; break; } }
        });
    }
    if(editor_.mode==EditorMode::ModelBuilder) modifiedModel_=true; else modifiedTrack_=true; Change();
}
void App::Geometry()
{
    if(meshDirty_)
    {
        if(editor_.mode==EditorMode::ModelBuilder) { shown_=editor_.model.scene; geometry_.vehicleIndexCount=0; }
        else
        {
            shown_=MakeTrackScene(editor_.track,editor_.generated ? &*editor_.generated : nullptr); BuildSurface(shown_,geometry_.vertices,geometry_.indices); geometry_.vehicleFirstIndex=static_cast<uint32_t>(geometry_.indices.size());
            const auto vehicle=NormalizedVehicle(editor_.track); shown_.objects.insert(shown_.objects.end(),vehicle.objects.begin(),vehicle.objects.end());
        }
        BuildSurface(shown_,geometry_.vertices,geometry_.indices);
        if(editor_.mode!=EditorMode::ModelBuilder) geometry_.vehicleIndexCount=editor_.track.track.hasSpawn ? static_cast<uint32_t>(geometry_.indices.size())-geometry_.vehicleFirstIndex : 0;
        CompileSceneMaterials(shown_,paints_,surfaces_); meshDirty_=false; overlayDirty_=true;
    }
    if(overlayDirty_) { Overlay(); overlayDirty_=false; ++geometry_.revision; }
    if(editor_.mode==EditorMode::ModelBuilder) CompileSceneMaterials(editor_.model.scene,paints_,surfaces_);
}
void App::Overlay()
{
    geometry_.grid.clear(); geometry_.cage.clear(); geometry_.markers.clear();
    const auto& camera=ViewCamera(); const float spacing=std::pow(10.f,std::floor(std::log10(camera.height/4)));
    if(editor_.mode==EditorMode::ModelBuilder || !editor_.generated) for(int i=-20;i<=20;++i)
    {
        const float v=static_cast<float>(i)*spacing; Line(geometry_.grid,{-20*spacing,0,v},{20*spacing,0,v},i==0 ? Vector3{.25f,.07f,.07f} : Vector3{.07f,.085f,.105f}); Line(geometry_.grid,{v,0,-20*spacing},{v,0,20*spacing},i==0 ? Vector3{.06f,.12f,.28f} : Vector3{.07f,.085f,.105f});
    }
    if(editor_.mode==EditorMode::ModelBuilder)
    {
        if(!editor_.cage || editor_.model.selection.object<0 || !editor_.Editable()) return;
        const auto& object=editor_.Object(); const auto selected=editor_.SelectedVertices();
        const auto chosen=[&](uint32_t v) { return std::find(selected.begin(),selected.end(),v)!=selected.end(); };
        for(const auto& edge:ValidateControlMesh(object.cage)) Line(geometry_.cage,WorldPosition(object,object.cage.positions[edge.a]),WorldPosition(object,object.cage.positions[edge.b]),chosen(edge.a) && chosen(edge.b) ? Vector3{1,.32f,.035f} : Vector3{.14f,.7f,.52f});
        if(editor_.model.selection.mode==SelectionMode::Vertex)
        {
            const auto box=MakeBox();
            for(uint32_t v=0;v<object.cage.positions.size();++v)
            {
                const auto p=WorldPosition(object,object.cage.positions[v]); const float radius=camera.height*(chosen(v) ? 4.5f : 3.f)/view_.h; const auto color=chosen(v) ? Vector3{1,.36f,.055f} : Vector3{.22f,.9f,.66f};
                for(const auto& face:box.faces) for(size_t i=1;i+1<face.size();++i) for(auto index:{face[0],face[i],face[i+1]}) { const auto point=p+box.positions[index]*radius; geometry_.markers.push_back({point.x,point.y,point.z,color.x,color.y,color.z,1}); }
            }
        }
    }
    else
    {
        if(editor_.mode==EditorMode::TrackBuilder) for(size_t i=0;i<editor_.track.track.outlines.size();++i)
        {
            const auto& curve=editor_.track.track.outlines[i]; const auto loop=Racing2D::Tessellate(curve);
            for(size_t j=0;j<loop.size();++j) Line(geometry_.cage,World(loop[j]),World(loop[(j+1)%loop.size()]),i==1 ? Vector3{.02f,.1f,.3f} : Vector3{.02f,.25f,.04f});
            if(static_cast<int>(i)==editor_.outline) for(size_t j=0;j<curve.controls.size();++j)
            {
                const auto p=World(curve.controls[j].position,.1f),next=World(curve.controls[(j+1)%curve.controls.size()].position,.1f); const auto r=camera.height*.006f; Line(geometry_.cage,p,next,{.3f,.08f,.3f});
                const auto color=static_cast<int>(j)==editor_.point ? Vector3{.7f,.06f,.01f} : Vector3{.3f,.08f,.3f}; Line(geometry_.cage,p-Vector3{r,0,0},p+Vector3{r,0,0},color); Line(geometry_.cage,p-Vector3{0,0,r},p+Vector3{0,0,r},color);
            }
        }
        for(size_t i=1;i<editor_.draft.size();++i) Line(geometry_.cage,World(editor_.draft[i-1].position),World(editor_.draft[i].position),{.4f,.03f,.3f});
        auto gate=[&](Racing2D::Gate g,Vector3 color) { Line(geometry_.cage,World(g.a),World(g.b),color); const auto e=g.b-g.a; const auto n=Racing2D::Point{-e.y,e.x}*(1/e.Length()); const auto center=(g.a+g.b)*.5; Line(geometry_.cage,World(center),World(center+n*2),color); };
        if(editor_.track.track.hasFinish) gate(editor_.track.track.finish,{.03f,.35f,.4f});
        for(size_t i=0;i<editor_.track.track.checkpoints.size();++i) gate(editor_.track.track.checkpoints[i],editor_.mode==EditorMode::Drive && i==editor_.race.State().nextCheckpoint ? Vector3{.75f,.3f,.005f} : Vector3{.02f,.4f,.08f});
    }
}
void App::Draw(bool present)
{
    Interface(); text_.Paint(ui_,status_); Geometry(); const auto& camera=ViewCamera();
    const auto transforms=camera.Transforms(2122,1316);
    Orthographic::ObjectTransforms vehicle{}; const Orthographic::ObjectTransforms* vehiclePointer=nullptr;
    if(editor_.mode!=EditorMode::ModelBuilder)
    {
        const auto pose=editor_.mode==EditorMode::Drive ? Racing2D::Pose{editor_.race.State().position,editor_.race.State().heading} : editor_.track.track.spawn;
        const auto world=DirectX::XMMatrixRotationY(static_cast<float>(pose.heading+std::numbers::pi*.5))*DirectX::XMMatrixTranslation(static_cast<float>(pose.position.x),0,static_cast<float>(-pose.position.y));
        vehicle=camera.Transforms(2122,1316,world); vehiclePointer=&vehicle;
    }
    DisplayFrameSubmission frame{}; DisplayPresentProbe probe{}; const DisplayPresentProbe* probePointer=nullptr;
    if(present && editor_.mode==EditorMode::Drive && ((tracker_ && tracker_->Active()) || testPresentProbe_))
    {
        frame=displayInputs_.Take(++displayFrame_); probe={&frame,[](void* p) { return static_cast<GamepadInput*>(p)->Now(); },&gamepad_}; probePointer=&probe;
    }
    renderer_.Render(geometry_,transforms,std::span(paints_.data(),MaterialCount(shown_)),std::span(surfaces_.data(),MaterialCount(shown_)),vehiclePointer,probePointer,&text_.Data(),view_,present);
    if(probePointer && tracker_) tracker_->Submit(frame);
    if(probePointer && testPresentProbe_) { Require(frame.accepted,"Traced present was rejected."); ++testTracedPresents_; }
    schedule_.dirty=false;
}
void App::Tick(double seconds,bool synthetic)
{
    if(editor_.mode!=EditorMode::Drive) return;
    const auto before=ticks_;
    accumulator_+=std::clamp(seconds,0.,.0666666667);
    for(unsigned step=0;accumulator_>=1./120 && step<8;++step)
    {
        Racing2D::Commands command;
        if(synthetic) command.acceleration={1,0};
        else
        {
            const auto sample=gamepad_.Sample(); auto acceleration=InputActions::Acceleration(sample.analog.x,sample.analog.y,editor_.track.deadzoneX,editor_.track.deadzoneY);
            const bool keys=ui_.focus==0 && foreground_; const auto h=[&](int key) { return keys && held_[static_cast<size_t>(key)]; };
            acceleration=acceleration+Racing2D::Point{double(h('D')||h(VK_RIGHT))-double(h('A')||h(VK_LEFT)),double(h('W')||h(VK_UP))-double(h('S')||h(VK_DOWN))};
            const auto& camera=ViewCamera(); const auto right=camera.Right(); const auto forward=Vector3{-std::sin(camera.yaw),0,-std::cos(camera.yaw)};
            command.acceleration={right.x*acceleration.x+forward.x*acceleration.y,-right.z*acceleration.x-forward.z*acceleration.y}; command.brake=std::max(sample.brake,h(VK_SPACE) ? 1. : 0.); command.reset=h('R'); held_['R']=false;
            for(const auto& edge:sample.edges) { if(edge.pressed&NativeInput::GameInputGamepadX) command.reset=true; if(edge.pressed&NativeInput::GameInputGamepadMenu) { Mode(EditorMode::TrackBuilder); return; } }
            if(tracker_ && tracker_->Active())
            {
                if(sample.connected) displayInputs_.ConsumeStep({sample.device,sample.generation,sample.reading,sample.serial,sample.sampled,0},!sample.baseline && !command.reset,sample.edges,NativeInput::GameInputGamepadX);
                tracker_->ReportInputLoss(gamepad_.InputLossDetected() || sample.droppedEdges || sample.callbackErrors || displayInputs_.HasLoss());
            }
        }
        editor_.race.Step(command,1./120); ++ticks_; accumulator_-=1./120;
    }
    statusClock_+=seconds;
    const bool statusChanged=statusClock_>=.25;
    if(statusChanged) { status_=L"Drive | "+Wide(gamepad_.Status()); statusClock_=0; }
    if(ticks_!=before || statusChanged) Change(false);
}
Racing2D::Point App::Ground(float x,float y) const
{
    const auto& camera=editor_.trackCamera; const auto toward=camera.TowardCamera(); const auto p=camera.PlanePoint(x,y,2122,1316);
    if(std::abs(toward.y)<.05f) throw std::invalid_argument("Orbit above the ground to place track objects.");
    const auto hit=p-toward*(p.y/toward.y); return {hit.x,-hit.z};
}
void App::Pick(float x,float y,bool extend)
{
    auto& s=editor_.model.selection; auto& camera=editor_.modelCamera;
    if(s.mode==SelectionMode::Vertex && editor_.Editable())
    {
        const auto& object=editor_.Object(); int nearest=-1; float best=196;
        for(uint32_t v=0;v<object.cage.positions.size();++v) { const auto p=camera.Screen(WorldPosition(object,object.cage.positions[v]),2122,1316); const auto d=(p.x-x)*(p.x-x)+(p.y-y)*(p.y-y); if(d<best) { best=d; nearest=static_cast<int>(v); } }
        if(!extend && (nearest<0 || std::find(s.vertices.begin(),s.vertices.end(),static_cast<uint32_t>(nearest))==s.vertices.end())) s.vertices.clear();
        if(nearest>=0) { const auto v=static_cast<uint32_t>(nearest); const auto it=std::find(s.vertices.begin(),s.vertices.end(),v); if(it==s.vertices.end()) s.vertices.push_back(v); else if(extend) s.vertices.erase(it); }
    }
    else
    {
        const auto direction=camera.TowardCamera()*-1; const auto origin=camera.PlanePoint(x,y,2122,1316)-direction*(camera.height*4+10); float closest=1e20f; int chosen=-1;
        if(s.mode==SelectionMode::Face && editor_.Editable())
        {
            const auto& object=editor_.Object();
            for(size_t f=0;f<object.cage.faces.size();++f) { const auto& face=object.cage.faces[f]; for(size_t i=1;i+1<face.size();++i) { float d=0; if(RayTriangle(origin,direction,WorldPosition(object,object.cage.positions[face[0]]),WorldPosition(object,object.cage.positions[face[i]]),WorldPosition(object,object.cage.positions[face[i+1]]),d) && d<closest) { closest=d; chosen=static_cast<int>(f); } } }
            s.face=chosen; if(chosen>=0) s.material=FaceMaterial(object.cage,static_cast<size_t>(chosen));
        }
        else
        {
            Geometry(); const auto p=[](const PaintVertex& v) { return Vector3{v.positionX,v.positionY,v.positionZ}; };
            for(size_t i=0;i<geometry_.indices.size();i+=3) { const auto& a=geometry_.vertices[geometry_.indices[i]]; const auto& b=geometry_.vertices[geometry_.indices[i+1]]; const auto& c=geometry_.vertices[geometry_.indices[i+2]]; float d=0; if(RayTriangle(origin,direction,p(a),p(b),p(c),d) && d<closest) { closest=d; const auto material=LocateMaterial(editor_.model.scene,a.materialIndex); chosen=static_cast<int>(material.object); s.material=material.slot; } }
            s.object=chosen; s.face=-1; s.vertices.clear();
        }
    }
    Change(false);
}
POINT App::SliderPoint() const
{
    const auto* c=ui_.Find(slider_); if(!c) throw std::invalid_argument("Slider has no visible cursor restore area.");
    const auto range=ParameterRange(static_cast<PaintParameter>(slider_-SliderFirst)); const auto t=(ParameterValue(editor_.Material().paint,static_cast<PaintParameter>(slider_-SliderFirst))-range.minimum)/(range.maximum-range.minimum);
    const auto visible=Ui::Intersect(c->rect,c->clip),image=presentation_.Image();
    const LONG x=static_cast<LONG>(std::floor(c->rect.x+8+static_cast<float>(t)*(c->rect.w-16))),y=static_cast<LONG>(std::floor(c->rect.y+c->rect.h*.5f));
    auto point=SliderCursorPoint({x,y,x+1,y+1},{static_cast<LONG>(std::ceil(visible.x)),static_cast<LONG>(std::ceil(visible.y)),static_cast<LONG>(std::ceil(visible.x+visible.w)),static_cast<LONG>(std::ceil(visible.y+visible.h))});
    point.x+=static_cast<LONG>(image.x); point.y+=static_cast<LONG>(image.y);
    ClientToScreen(window_,&point); return point;
}
void App::Abort(bool restore)
{
    restore=restore && GetForegroundWindow()==window_ && GetCapture()==window_;
    POINT point{}; if(restore && slider_) { try { point=SliderPoint(); } catch(...) { restore=false; } }
    relative_.End(point,restore); editor_.EndPaint(true); slider_=0; ui_.Cancel();
    if(modelDrag_) editor_.model=dragStart_; if(trackDrag_) editor_.track=trackStart_;
    modelDrag_=trackDrag_=orbit_=pan_=false; held_.fill(false);
    if(!options_.hidden && !options_.smoke && GetCapture()==window_) ReleaseCapture();
    Change();
}
void App::RawMouse(HRAWINPUT input)
{
    UINT size=0; if(GetRawInputData(input,RID_INPUT,nullptr,&size,sizeof(RAWINPUTHEADER))!=0 || size>4096) return;
    std::vector<std::byte> bytes(size); if(GetRawInputData(input,RID_INPUT,bytes.data(),&size,sizeof(RAWINPUTHEADER))!=size) return;
    const auto* raw=reinterpret_cast<const RAWINPUT*>(bytes.data());
    if(slider_ && raw->header.dwType==RIM_TYPEMOUSE && !(raw->data.mouse.usFlags&MOUSE_MOVE_ABSOLUTE))
    {
        const auto p=static_cast<PaintParameter>(slider_-SliderFirst); const auto* c=ui_.Find(slider_);
        editor_.PreviewPaint(p,DraggedValue(ParameterValue(editor_.Material().paint,p),raw->data.mouse.lLastX,c->rect.w-16,editor_.model.scene.sliderDragSpeed,p)); Change(false);
    }
}
void App::Pointer(UINT message,float x,float y,WPARAM buttons)
{
    if(slider_ && message!=WM_LBUTTONUP) return;
    if(message==WM_LBUTTONDOWN)
    {
        Actions(ui_.Down(x,y));
        if(ui_.capture || ui_.Editing()) { if(ui_.capture && !relative_.Active() && !options_.smoke && !options_.hidden) SetCapture(window_); Change(false); return; }
        menu_=0;
        if(!view_.Contains(x,y)) return;
        x-=view_.x; y-=view_.y;
        if(editor_.mode==EditorMode::ModelBuilder)
        {
            Pick(x,y,(buttons&MK_CONTROL)!=0);
            if(editor_.model.selection.mode==SelectionMode::Vertex && !editor_.SelectedVertices().empty()) { modelDrag_=true; dragStart_=editor_.model; dragPlane_=ViewCamera().PlanePoint(x,y,2122,1316); }
        }
        else if(editor_.mode==EditorMode::TrackBuilder)
        {
            if(editor_.tool!=Editor::Tool::Select) { editor_.Place(Ground(x,y)); modifiedTrack_=true; Change(); }
            else if(editor_.outline>=0 && static_cast<size_t>(editor_.outline)<editor_.track.track.outlines.size())
            {
                const auto& curve=editor_.track.track.outlines[static_cast<size_t>(editor_.outline)]; float best=196; int selected=-1;
                for(size_t i=0;i<curve.controls.size();++i) { const auto p=ViewCamera().Screen(World(curve.controls[i].position),2122,1316); const auto d=(p.x-x)*(p.x-x)+(p.y-y)*(p.y-y); if(d<best) { best=d; selected=static_cast<int>(i); } }
                if(selected>=0) { editor_.point=selected; trackStart_=editor_.track; trackDrag_=true; }
            }
        }
        if((modelDrag_ || trackDrag_) && !options_.smoke && !options_.hidden) SetCapture(window_);
    }
    else if(message==WM_LBUTTONUP)
    {
        Actions(ui_.Up(x,y));
        if(modelDrag_) { const auto result=editor_.model; editor_.model=dragStart_; modelDrag_=false; editor_.ModelEdit([&] { editor_.model=result; }); modifiedModel_=true; }
        if(trackDrag_) { const auto result=editor_.track; editor_.track=trackStart_; trackDrag_=false; editor_.TrackEdit([&] { editor_.track=result; }); modifiedTrack_=true; }
        orbit_=pan_=false; if(!options_.smoke && !options_.hidden && GetCapture()==window_) ReleaseCapture(); Change();
    }
    else if(message==WM_RBUTTONDOWN || message==WM_MBUTTONDOWN)
    {
        Actions(ui_.Commit()); if(!view_.Contains(x,y)) return; orbit_=message==WM_RBUTTONDOWN; pan_=message==WM_MBUTTONDOWN;
        previousX_=x; previousY_=y; if(!options_.smoke && !options_.hidden) SetCapture(window_);
    }
    else if(message==WM_RBUTTONUP || message==WM_MBUTTONUP) { orbit_=pan_=false; if(!options_.smoke && !options_.hidden && GetCapture()==window_) ReleaseCapture(); }
    else if(message==WM_MOUSEMOVE)
    {
        if(slider_) return;
        if(orbit_) { ViewCamera().Orbit(x-previousX_,y-previousY_); Change(false); }
        else if(pan_) { ViewCamera().Pan(x-previousX_,y-previousY_,1316); Change(false); }
        else if(modelDrag_)
        {
            editor_.model=dragStart_; auto& object=editor_.Object(); const auto delta=ViewCamera().PlanePoint(x-view_.x,y-view_.y,2122,1316)-dragPlane_;
            const auto rotation=DirectX::XMMatrixRotationRollPitchYaw(DirectX::XMConvertToRadians(object.rotationDegrees.x),DirectX::XMConvertToRadians(object.rotationDegrees.y),DirectX::XMConvertToRadians(object.rotationDegrees.z));
            DirectX::XMFLOAT3 local{}; DirectX::XMStoreFloat3(&local,DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(delta.x,delta.y,delta.z,0),DirectX::XMMatrixTranspose(rotation)));
            for(auto v:editor_.SelectedVertices()) object.cage.positions.at(v)+=Vector3{local.x,local.y,local.z}/object.scale;
            try { ValidateScene(editor_.model.scene); } catch(...) { editor_.model=dragStart_; } Change();
        }
        else if(trackDrag_) { auto& point=editor_.track.track.outlines.at(static_cast<size_t>(editor_.outline)).controls.at(static_cast<size_t>(editor_.point)); point.position=Ground(x-view_.x,y-view_.y); editor_.generated.reset(); Change(); }
        else if(ui_.Move(x,y)) Change(false);
        previousX_=x; previousY_=y;
    }
}
void App::Key(UINT key)
{
    const bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
    if(key==VK_ESCAPE) { if(slider_ || ui_.Editing() || modelDrag_ || trackDrag_) Abort(true); else if(editor_.mode==EditorMode::Drive) Mode(EditorMode::TrackBuilder); else { menu_=0; editor_.draft.clear(); editor_.gateStart.reset(); Change(false); } return; }
    if(slider_) return;
    if(ui_.Editing())
    {
        if(key==VK_RETURN) Actions(ui_.Commit()); else if(key==VK_BACK || key==VK_DELETE) ui_.Erase(key==VK_BACK); else if(key==VK_LEFT || key==VK_RIGHT) ui_.Left(key==VK_RIGHT,shift); else if(key==VK_HOME || key==VK_END) ui_.Home(key==VK_END,shift); else if(ctrl && key=='A') ui_.SelectAll();
        else if(ctrl && (key=='C' || key=='X'))
        {
            const auto selection=ui_.Selected(); if(!selection.empty() && OpenClipboard(window_)) { const auto bytes=(selection.size()+1)*sizeof(wchar_t); auto memory=GlobalAlloc(GMEM_MOVEABLE,bytes); if(memory) { if(auto* data=GlobalLock(memory)) { std::memcpy(data,selection.c_str(),bytes); GlobalUnlock(memory); EmptyClipboard(); if(!SetClipboardData(CF_UNICODETEXT,memory)) GlobalFree(memory); } else GlobalFree(memory); } CloseClipboard(); if(key=='X') ui_.Erase(true); }
        }
        else if(ctrl && key=='V' && OpenClipboard(window_)) { auto handle=GetClipboardData(CF_UNICODETEXT); if(handle) { const auto* data=static_cast<const wchar_t*>(GlobalLock(handle)); if(data) { const auto bound=GlobalSize(handle)/sizeof(wchar_t); size_t length=0; while(length<bound && length<128 && data[length]) ++length; ui_.Replace(std::wstring(data,length)); GlobalUnlock(handle); } } CloseClipboard(); }
        Change(false); return;
    }
    if(ctrl && (key=='Z' || key=='Y')) Command(key=='Y' ? RedoAction : UndoAction);
    else if(ctrl && key=='S') File(SaveFile);
    else if(key==VK_F5) Mode(editor_.mode==EditorMode::Drive ? EditorMode::TrackBuilder : EditorMode::Drive);
    else if(editor_.mode==EditorMode::ModelBuilder)
    {
        if(key=='F') { editor_.Frame(); Change(false); }
        else if(key=='E') Command(ExtrudeAction);
        else if(key=='R' || key=='S') TransformKey(key,shift);
        else { const auto step=editor_.step*(shift ? .1f : 1.f); Vector3 delta{}; if(key==VK_LEFT) delta.x=-step; else if(key==VK_RIGHT) delta.x=step; else if(key==VK_UP) delta.y=step; else if(key==VK_DOWN) delta.y=-step; else if(key==VK_PRIOR) delta.z=step; else if(key==VK_NEXT) delta.z=-step; if(delta.Length()>0) { editor_.Nudge(delta); modifiedModel_=true; Change(); } }
    }
}
void App::TransformKey(UINT key,bool shift)
{
    const bool whole=editor_.model.selection.mode==SelectionMode::Object; const auto& object=editor_.Object();
    auto rotation=whole ? object.rotationDegrees : Vector3{}; auto scale=whole ? object.scale : 1.f;
    if(key=='R') rotation.y+=shift ? -5.f : 5.f; else scale*=shift ? 1/1.1f : 1.1f;
    editor_.Transform(editor_.Center(),rotation,scale); modifiedModel_=true; Change();
}
void App::SaveModel(const std::filesystem::path& path)
{
    SaveScene(editor_.model.scene,path); modelPath_=path; modifiedModel_=false;
}
void App::OpenModel(const std::filesystem::path& path)
{
    auto scene=LoadScene(path); editor_.ModelEdit([&] { editor_.model.scene=scene; editor_.model.selection={SelectionMode::Object,0,-1,{},0}; });
    modelPath_=path; modifiedModel_=false; editor_.Frame(); Change();
}
void App::File(int action)
{
    if(options_.hidden || options_.smoke || editor_.mode==EditorMode::Drive) return;
    const bool model=editor_.mode==EditorMode::ModelBuilder;
    if(!model && (!editor_.draft.empty() || editor_.gateStart)) throw std::invalid_argument("Finish or cancel the draft before saving/opening a track.");
    auto path=model ? modelPath_ : trackPath_; const bool save=action!=OpenProject;
    if(action==ExportFile) path.clear();
    if(action!=SaveFile || path.empty())
    {
        wchar_t filename[32768]{}; if(!path.empty()) wcsncpy_s(filename,path.c_str(),_TRUNCATE);
        OPENFILENAMEW dialog{}; dialog.lStructSize=sizeof(dialog); dialog.hwndOwner=window_; dialog.lpstrFile=filename; dialog.nMaxFile=32768;
        dialog.lpstrFilter=action==ExportFile ? L"C++ header\0*.h\0\0" : model ? L"Model project\0*.modeler\0\0" : L"Track project\0*.track\0\0";
        dialog.lpstrDefExt=action==ExportFile ? L"h" : model ? L"modeler" : L"track"; dialog.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        if(!(save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))) return; path=filename;
    }
    if(action==ExportFile) ExportMesh(editor_.model.scene,path);
    else if(save) { if(model) SaveModel(path); else { SaveTrack(editor_.track,path); trackPath_=path; modifiedTrack_=false; } }
    else if(model) OpenModel(path);
    else { auto track=LoadTrack(path); editor_.TrackEdit([&] { editor_.track=track; }); trackPath_=path; modifiedTrack_=false; editor_.outline=0; editor_.point=0; editor_.BuildTrack(); editor_.Frame(); }
    status_=L"Saved/opened: "+path.filename().wstring(); Change();
}
LRESULT App::Message(UINT message,WPARAM wparam,LPARAM lparam)
{
    switch(message)
    {
    case WM_ERASEBKGND:return 1;
    case WM_PAINT: { PAINTSTRUCT paint{}; BeginPaint(window_,&paint); EndPaint(window_,&paint); schedule_.dirty=true; return 0; }
    case WM_SIZE:schedule_.minimized=wparam==SIZE_MINIMIZED; if(schedule_.minimized) { Abort(); accumulator_=0; } gamepad_.SetForeground(schedule_.WantsTimer()); schedule_.dirty=true; return 0;
    case WM_SHOWWINDOW:visibility_.shown=wparam!=0; RefreshVisibility(); return 0;
    case Renderer::VisibilityMessage:RefreshVisibility(); return 0;
    case WM_WTSSESSION_CHANGE:
        if(wparam==WTS_SESSION_LOCK || wparam==WTS_CONSOLE_DISCONNECT || wparam==WTS_REMOTE_DISCONNECT) visibility_.sessionAvailable=false;
        else if(wparam==WTS_SESSION_UNLOCK || wparam==WTS_CONSOLE_CONNECT || wparam==WTS_REMOTE_CONNECT) visibility_.sessionAvailable=true;
        VisibilityChanged(); return 0;
    case WM_POWERBROADCAST:
        if(wparam==PBT_POWERSETTINGCHANGE && lparam)
        {
            const auto* setting=reinterpret_cast<const POWERBROADCAST_SETTING*>(lparam);
            if(setting->PowerSetting==GUID_CONSOLE_DISPLAY_STATE && setting->DataLength==sizeof(DWORD)) { DWORD state=0; std::memcpy(&state,setting->Data,sizeof(state)); visibility_.displayOn=state!=0; VisibilityChanged(); }
        }
        else if(wparam==PBT_APMSUSPEND) { visibility_.suspended=true; VisibilityChanged(); }
        else if(wparam==PBT_APMRESUMEAUTOMATIC || wparam==PBT_APMRESUMESUSPEND) { visibility_.suspended=false; RefreshVisibility(); }
        return TRUE;
    case WM_ACTIVATE: foreground_=LOWORD(wparam)!=WA_INACTIVE; schedule_.active=foreground_ && editor_.mode==EditorMode::Drive; RefreshVisibility(); if(!foreground_) Abort(); return 0;
    case WM_KILLFOCUS:foreground_=false; schedule_.active=false; gamepad_.SetForeground(false); Abort(); return 0;
    case WM_CAPTURECHANGED:if(reinterpret_cast<HWND>(lparam)!=window_ && (ui_.capture || slider_ || modelDrag_ || trackDrag_ || orbit_ || pan_)) Abort(); return 0;
    case WM_CANCELMODE:Abort();return 0;
    case WM_INPUT:RawMouse(reinterpret_cast<HRAWINPUT>(lparam));return DefWindowProcW(window_,message,wparam,lparam);
    case WM_KEYDOWN:if(wparam<256) held_[wparam]=true; Key(static_cast<UINT>(wparam)); return 0;
    case WM_KEYUP:if(wparam<256) held_[wparam]=false;return 0;
    case WM_CHAR:if(ui_.Editing() && !(GetKeyState(VK_CONTROL)&0x8000)) { ui_.Text(static_cast<wchar_t>(wparam)); Change(false); } return 0;
    case WM_MOUSEWHEEL:
    {
        if(slider_) return 0;
        POINT p{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)}; ScreenToClient(window_,&p); const auto point=presentation_.ClientPoint(static_cast<float>(p.x),static_cast<float>(p.y));
        if(point) { const auto x=(*point)[0],y=(*point)[1]; if(ui_.panel.Contains(x,y)) { if(ui_.Wheel(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)))) Change(false); } else if(view_.Contains(x,y)) { ViewCamera().Zoom(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam))); Change(false); } } return 0;
    }
    case WM_MOUSEMOVE:case WM_LBUTTONDOWN:case WM_LBUTTONUP:case WM_RBUTTONDOWN:case WM_RBUTTONUP:case WM_MBUTTONDOWN:case WM_MBUTTONUP:
    {
        const float x=static_cast<float>(GET_X_LPARAM(lparam)),y=static_cast<float>(GET_Y_LPARAM(lparam)); auto point=presentation_.ClientPoint(x,y);
        if(point) Pointer(message,(*point)[0],(*point)[1],wparam);
        else if(GetCapture()==window_ || ui_.capture) { const auto image=presentation_.Image(); Pointer(message,x-image.x,y-image.y,wparam); }
        else if(ui_.hover) { ui_.hover=0; Change(false); }
        return 0;
    }
    case WM_DPICHANGED:case WM_DISPLAYCHANGE:
    {
        const auto displays=EnumerateDisplays(); const auto selected=ResolveDisplay(displays,display_.identity); const auto p=DisplayPresentation(displays[selected]);
        try { ValidateDisplay(displays[selected],p); if(p.width!=presentation_.width || p.height!=presentation_.height || p.originX!=presentation_.originX || p.originY!=presentation_.originY) throw std::runtime_error("Monitor configuration changed. Restart Veehiicuul3 to recenter its fixed render surface."); }
        catch(const std::exception& e) { Abort(); if(!options_.hidden && !options_.smoke) MessageBoxA(window_,e.what(),"Veehiicuul3 display error",MB_OK|MB_ICONERROR); running_=false; }
        return 0;
    }
    case WM_CLOSE:
        if(!options_.hidden && !options_.smoke && (modifiedModel_ || modifiedTrack_) && MessageBoxW(window_,L"Close and discard unsaved model or track changes?",L"Veehiicuul3",MB_OKCANCEL|MB_ICONQUESTION)!=IDOK) return 0;
        running_=false; return 0;
    case WM_DESTROY:window_=nullptr;running_=false;return 0;
    }
    return DefWindowProcW(window_,message,wparam,lparam);
}
int App::Run()
{
    if(options_.hidden || options_.smoke) return Tests();
    ShowWindow(window_,SW_SHOW); SetForegroundWindow(window_); SetFocus(window_); foreground_=true;
    RefreshVisibility(); Pump(); return 0;
}
void App::Pump(std::optional<Clock::time_point> deadline)
{
    auto previous=Clock::now(); bool ticking=false;
    while(running_)
    {
        MSG message{}; while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { if(message.message==WM_QUIT) running_=false; TranslateMessage(&message); DispatchMessageW(&message); }
        if(!running_) break;
        const auto now=Clock::now(); if(deadline && now>=*deadline) break;
        const auto remaining=deadline ? static_cast<DWORD>(std::max(1.,std::ceil(std::chrono::duration<double,std::milli>(*deadline-now).count()))) : INFINITE;
        const bool active=schedule_.WantsTimer();
        if(active) Tick(ticking ? std::chrono::duration<double>(now-previous).count() : 0.);
        previous=now; ticking=active;
        if(schedule_.WantsFrame())
        {
            HANDLE ready=renderer_.FrameReady(); const auto result=MsgWaitForMultipleObjectsEx(1,&ready,remaining,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
            if(result==WAIT_OBJECT_0 && !PeekMessageW(&message,nullptr,0,0,PM_NOREMOVE)) Draw();
            else if(result==WAIT_FAILED) throw std::runtime_error("DXGI/message wait failed.");
        }
        else
        {
            const DWORD timeout=std::min(remaining,schedule_.WantsTimer() ? DWORD{8} : INFINITE);
            if(MsgWaitForMultipleObjectsEx(0,nullptr,timeout,QS_ALLINPUT,MWMO_INPUTAVAILABLE)==WAIT_FAILED) throw std::runtime_error("Editor idle wait failed.");
        }
        ++wakes_;
    }
}
void App::TestWorkflow()
{
    TestUiTransactions();
    Command(NewPrimitive); Command(CopyPaint); editor_.model.selection.mode=SelectionMode::Face; editor_.model.selection.face=0; Command(AssignPaint); Command(PaintFirst,.7); Command(ExtrudeAction); Command(RefineAction);
    SaveScene(editor_.model.scene,options_.session/"TwoPaint.modeler"); ExportMesh(editor_.model.scene,options_.session/"TwoPaint.h");
    const auto authored=SerializeScene(editor_.model.scene); Draw(!options_.hidden); renderer_.Capture(options_.session/"Model2560x1440.bmp");
    Mode(EditorMode::TrackBuilder); Command(ExampleTrack); Draw(!options_.hidden); renderer_.Capture(options_.session/"Track2560x1440.bmp");
    SaveTrack(editor_.track,options_.session/"ExampleCircuit.track");
    Mode(EditorMode::Drive); for(int i=0;i<120;++i) { Tick(1./120,true); if(i%30==0) Draw(!options_.hidden); }
    Draw(!options_.hidden); renderer_.Capture(options_.session/"Drive2560x1440.bmp");
    Mode(EditorMode::ModelBuilder); Require(SerializeScene(editor_.model.scene)==authored,"Mode changes lost model authoring."); Command(NewCar); Draw(!options_.hidden); renderer_.Capture(options_.session/"SlopeCar2560x1440.bmp");
    ui_.scroll=1000; Draw(!options_.hidden); renderer_.Capture(options_.session/"ScrolledPaint2560x1440.bmp");
    const auto source=SerializeScene(editor_.model.scene);
    const auto* slider=ui_.Find(SliderFirst); Require(slider && Ui::Intersect(slider->rect,slider->clip).h>0,"Test slider not visible after scroll.");
    const float sx=slider->rect.x+20,sy=slider->rect.y+12;
    Actions(ui_.Down(sx,sy)); Require(relative_.Active(),"Shared UI failed to start relative pointer session.");
    const auto value=ParameterValue(editor_.Material().paint,PaintParameter::Red);
    editor_.PreviewPaint(PaintParameter::Red,DraggedValue(value,100000,388,.01,PaintParameter::Red));
    Actions(ui_.Cancel()); Require(SerializeScene(editor_.model.scene)==source && !relative_.Active(),"Relative slider cancellation failed.");
    ui_.scroll=0; Draw(!options_.hidden);
    if(options_.smoke)
    {
        renderer_.Capture(options_.session/"CenteredPresentation.bmp",true); renderer_.Capture(options_.session/"PresentationSource.bmp");
        const auto read=[](const std::filesystem::path& p) { std::ifstream f(p,std::ios::binary);return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),{}); };
        const auto presented=read(options_.session/"CenteredPresentation.bmp"),internal=read(options_.session/"PresentationSource.bmp"); const auto image=presentation_.Image();
        Require(presented.size()==54+static_cast<size_t>(presentation_.width)*presentation_.height*4,"Presentation capture dimensions wrong.");
        for(int y=0;y<presentation_.height;++y) for(int x=0;x<presentation_.width;++x)
        {
            const size_t output=54+(static_cast<size_t>(presentation_.height-1-y)*presentation_.width+x)*4;
            if(image.Contains(static_cast<float>(x),static_cast<float>(y)))
            {
                const auto ix=x-static_cast<int>(image.x),iy=y-static_cast<int>(image.y);const size_t input=54+(static_cast<size_t>(1439-iy)*2560+ix)*4;
                for(int c=0;c<3;++c) Require(std::abs(int(presented[output+c])-int(internal[input+c]))<=1,"Presentation rescaled or shifted the internal image.");
            }
            else for(int c=0;c<3;++c) Require(presented[output+c]==0,"Presentation margin is not black.");
        }
    }
}
void App::TestUiTransactions()
{
    Command(NewCar); const auto path=options_.session/"Clean.modeler"; SaveModel(path);
    for(bool opened:{false,true})
    {
        if(opened) OpenModel(path); else SaveModel(path);
        ui_.scroll=1000; Interface(); const auto clean=SerializeScene(editor_.model.scene);
        Actions({{Ui::ActionKind::BeginSlider,SliderFirst}}); editor_.PreviewPaint(PaintParameter::Red,opened ? .415 : .314);
        Actions({{Ui::ActionKind::EndSlider,SliderFirst}}); Require(modifiedModel_,"Committed slider did not mark a clean saved/opened model unsaved.");
        SaveModel(path); Actions({{Ui::ActionKind::BeginSlider,SliderFirst}}); Actions({{Ui::ActionKind::EndSlider,SliderFirst}});
        Require(!modifiedModel_,"Unchanged slider marked a clean model unsaved.");
        OpenModel(path); const auto beforeCancel=SerializeScene(editor_.model.scene);
        Actions({{Ui::ActionKind::BeginSlider,SliderFirst}}); editor_.PreviewPaint(PaintParameter::Red,.618);
        Actions({{Ui::ActionKind::CancelSlider,SliderFirst}}); Require(!modifiedModel_ && SerializeScene(editor_.model.scene)==beforeCancel,"Canceled slider changed a clean model or its unsaved state.");
        TransformKey('R',false); Require(modifiedModel_,"Keyboard rotation bypassed unsaved warning."); SaveModel(path);
        TransformKey('S',false); Require(modifiedModel_,"Keyboard scale bypassed unsaved warning.");
        Actions({{Ui::ActionKind::BeginSlider,SliderFirst}}); editor_.PreviewPaint(PaintParameter::Red,.618); Actions({{Ui::ActionKind::CancelSlider,SliderFirst}});
        Require(modifiedModel_,"Cancel cleared previously committed unsaved changes.");
        Require(clean!=SerializeScene(editor_.model.scene),"Authored regression operations were no-ops.");
    }
    Interface(); menu_=0; Pointer(WM_LBUTTONDOWN,20,20,0); Require(ui_.capture==ModeMenu,"Button capture test did not start.");
    Message(WM_CAPTURECHANGED,0,0); Pointer(WM_LBUTTONUP,20,20,0); Require(!ui_.capture && !menu_,"Button activated after capture loss without focus loss.");
    ui_.scroll=1000; Interface(); const auto thumb=ui_.Thumb(); Pointer(WM_LBUTTONDOWN,thumb.x+2,thumb.y+2,0); Require(ui_.capture==-1,"Scrollbar capture test did not start.");
    Message(WM_CAPTURECHANGED,0,0); const auto scroll=ui_.scroll; Pointer(WM_MOUSEMOVE,thumb.x+2,thumb.y+102,0); Pointer(WM_LBUTTONUP,thumb.x+2,thumb.y+102,0);
    Require(!ui_.capture && ui_.scroll==scroll,"Scrollbar remained active after capture loss.");
    for(int height=1;height<=4;++height) for(bool escape:{false,true})
    {
        Interface(); auto& slider=*std::find_if(ui_.controls.begin(),ui_.controls.end(),[](const Ui::Control& c){return c.id==SliderFirst;});
        slider.rect={12,32.f+height,404,30}; slider.clip={12,62,404,static_cast<float>(height)};
        Actions(ui_.Down(32,62.5f)); Require(relative_.Active(),"Partially clipped slider did not capture.");
        const auto point=SliderPoint(); const auto image=presentation_.Image(); const auto y=point.y-presentation_.originY-static_cast<LONG>(image.y);
        Require(y>=62 && y<62+height,"Clipped slider restore was outside its 1-4 pixel intersection.");
        if(escape) Key(VK_ESCAPE); else Actions(ui_.Up(32,62.5f));
        Require(!relative_.Active() && !ui_.capture && pointerPlatform_.SimulatedClean(),"Clipped slider release/Escape leaked capture.");
    }
    ui_.scroll=0; modelPath_.clear(); Change();
}
int App::Tests()
{
    if(options_.smoke && !InputDesktopAvailable()) { std::ofstream(options_.session/"Skipped.txt")<<"Input desktop unavailable before displayed checks."; return 77; }
    const auto foreground=GetForegroundWindow(),focus=GetFocus(),capture=GetCapture(); POINT cursorBefore{}; GetCursorPos(&cursorBefore);
    if(options_.smoke) { ShowWindow(window_,SW_SHOWNOACTIVATE); SetWindowPos(window_,HWND_TOP,presentation_.originX,presentation_.originY,presentation_.width,presentation_.height,SWP_NOACTIVATE); }
    TestWorkflow(); TestProductionLoop();
    // The same Pump used by Run is observed with a finite test deadline.
    MSG message{}; while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
    if(schedule_.dirty) Draw(!options_.hidden); renderer_.WaitIdle();
    const auto startPresents=renderer_.Presents(); const auto start=std::chrono::steady_clock::now();
    FILETIME created{},exit{},kernelBefore{},userBefore{},kernelAfter{},userAfter{}; GetProcessTimes(GetCurrentProcess(),&created,&exit,&kernelBefore,&userBefore);
    const auto beforeWakes=wakes_; Pump(start+std::chrono::seconds(3)); const auto idleWakes=wakes_-beforeWakes;
    GetProcessTimes(GetCurrentProcess(),&created,&exit,&kernelAfter,&userAfter);
    const auto time=[](FILETIME value) { return (static_cast<uint64_t>(value.dwHighDateTime)<<32)|value.dwLowDateTime; };
    POINT cursorAfter{}; GetCursorPos(&cursorAfter);
    // The user may move their cursor or change applications during a test.
    // Never mistake that activity for an operation performed by this test.
    Require(GetForegroundWindow()!=window_ && GetFocus()!=window_ && GetCapture()!=window_,"Test acquired foreground/focus/capture.");
    const bool foregroundStable=GetForegroundWindow()==foreground && GetFocus()==focus && GetCapture()==capture;
    const bool cursorStable=cursorBefore.x==cursorAfter.x && cursorBefore.y==cursorAfter.y;
    std::ofstream(options_.session/"GpuDebug.txt")<<renderer_.DebugMessages();
    Require(pointerPlatform_.SimulatedClean(),"Simulated pointer cleanup failed."); Require(renderer_.DebugErrors()==0,"DX12 debug layer reported warnings/errors.");
    Require(renderer_.Presents()==startPresents,"Static UI presented frames while idle.");
    Require(idleWakes<=8,"Static production scheduler woke continuously while idle.");
    char commit[128]{}; WideCharToMultiByte(CP_UTF8,0,VEEHIICUUL3_COMMIT,-1,commit,sizeof(commit),nullptr,nullptr);
    std::ofstream out(options_.session/"Verification.txt"); out<<"Veehiicuul3 "<<commit<<"\nAdapter: "<<renderer_.AdapterName()<<"\nHidden: "<<options_.hidden<<"\nPresents: "<<renderer_.Presents()<<"\nIdle seconds: 3\nIdle new presents: "<<renderer_.Presents()-startPresents<<"\nIdle wakes: "<<idleWakes<<"\nIdle CPU ms: "<<double(time(kernelAfter)+time(userAfter)-time(kernelBefore)-time(userBefore))/10000<<"\nDX12 warnings/errors: "<<renderer_.DebugErrors()<<"\nDebug layer enabled: "<<renderer_.HasDebugLayer()<<"\nTest acquired foreground/focus/capture: false\nObserved foreground/focus/capture stable: "<<foregroundStable<<"\nObserved user cursor stable: "<<cursorStable<<"\nCursor operations: simulated only\nProduction Run/Pump path: idle and driving suspension/resume passed\nVisibility signals: synthetic show/minimize/session/display-power/cloak/suspend; OS registrations exercised, no OS state changed\nTraced Present probe branch: "<<testTracedPresents_<<" accepted submissions (no physical ETW association claim)\nPhysical input and actual ETW display association: pending\n";
    if(options_.smoke && !InputDesktopAvailable()) { std::ofstream(options_.session/"Skipped.txt")<<"Input desktop became unavailable during displayed checks. Visual verification pending."; return 77; }
    return 0;
}
void App::TestProductionLoop()
{
    syntheticVisibility_=true; visibility_=Ui::Visibility{}; visibility_.shown=true; schedule_.minimized=false; VisibilityChanged();
    foreground_=true; Mode(EditorMode::TrackBuilder); Command(ExampleTrack); Mode(EditorMode::Drive);
    struct PowerSignal { GUID setting; DWORD length,value; } power{GUID_CONSOLE_DISPLAY_STATE,sizeof(DWORD),0};
    for(bool traced:{false,true})
    {
        testPresentProbe_=traced;
        const auto initialTicks=ticks_,initialPresents=renderer_.Presents();
        held_['D']=true; Pump(Clock::now()+std::chrono::milliseconds(150));
        Require(ticks_>initialTicks && renderer_.Presents()>initialPresents,"Production driving loop did not step/present.");
        for(int reason=0;reason<6;++reason)
        {
            if(reason==0) Message(WM_SIZE,SIZE_MINIMIZED,0);
            else if(reason==1) Message(WM_WTSSESSION_CHANGE,WTS_SESSION_LOCK,0);
            else if(reason==2) { power.value=0; Message(WM_POWERBROADCAST,PBT_POWERSETTINGCHANGE,reinterpret_cast<LPARAM>(&power)); }
            else if(reason==3) { visibility_.cloaked=true; Message(Renderer::VisibilityMessage,0,0); }
            else if(reason==4) Message(WM_SHOWWINDOW,FALSE,0);
            else Message(WM_POWERBROADCAST,PBT_APMSUSPEND,0);
            const auto ticks=ticks_,presents=renderer_.Presents(),wakes=wakes_; Message(WM_PAINT,0,0);
            Pump(Clock::now()+std::chrono::milliseconds(40));
            Require(ticks_==ticks && renderer_.Presents()==presents,"Paint/dirty state resumed invisible driving or traced rendering.");
            Require(wakes_-wakes<=4,"Suspended production scheduler polled continuously.");
            if(reason==0) Message(WM_SIZE,SIZE_RESTORED,0);
            else if(reason==1) Message(WM_WTSSESSION_CHANGE,WTS_SESSION_UNLOCK,0);
            else if(reason==2) { power.value=1; Message(WM_POWERBROADCAST,PBT_POWERSETTINGCHANGE,reinterpret_cast<LPARAM>(&power)); }
            else if(reason==3) { visibility_.cloaked=false; Message(Renderer::VisibilityMessage,0,0); }
            else if(reason==4) Message(WM_SHOWWINDOW,TRUE,0);
            else Message(WM_POWERBROADCAST,PBT_APMRESUMEAUTOMATIC,0);
            held_['D']=true; Pump(Clock::now()+std::chrono::milliseconds(150));
            Require(ticks_>ticks && renderer_.Presents()>presents,"Production driving failed to resume after visibility notification.");
        }
    }
    Require(testTracedPresents_>0,"Production scheduler did not exercise traced Present branch.");
    testPresentProbe_=false; Mode(EditorMode::ModelBuilder); foreground_=false; schedule_.active=false;
    syntheticVisibility_=false; RefreshVisibility(); Change();
}
