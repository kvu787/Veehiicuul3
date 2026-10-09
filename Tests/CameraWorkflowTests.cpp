#include "Editor.h"
#include <array>
#include <iostream>
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
auto CameraState(const Camera& camera) { return std::array{camera.target.x,camera.target.y,camera.target.z,camera.yaw,camera.pitch,camera.height}; }
void Centered(const Editor& editor)
{
    const auto position=editor.race.State().position;
    Require(editor.driveCamera.target.x==static_cast<float>(position.x) && editor.driveCamera.target.y==0 && editor.driveCamera.target.z==static_cast<float>(-position.y),"Follow target is not the planar car's separate 3D presentation.");
}
int main()
{
    try
    {
        Editor editor; const auto model=SerializeScene(editor.model.scene); const auto modelCamera=CameraState(editor.modelCamera);
        Require(!editor.following,"Follow must default off like V2.");
        editor.Mode(EditorMode::TrackBuilder); editor.NewTrack(true); const auto before=SerializeTrack(editor.track);
        editor.TrackEdit([&] { editor.track.track.outlines[0].controls[0].weight=.8; }); editor.BuildTrack(); const auto track=SerializeTrack(editor.track); const auto road=editor.generated->surfaces[0].data();
        editor.trackCamera.target={4,3,-7}; editor.trackCamera.yaw=.64f; editor.trackCamera.pitch=1.23f; editor.trackCamera.height=87;
        const auto editing=CameraState(editor.trackCamera);
        editor.SetFollowing(true); Require(CameraState(editor.trackCamera)==editing,"Arming follow moved the authoring view."); editor.SetFollowing(false);
        editor.Mode(EditorMode::Drive); Require(editor.driveCamera.height==87 && editor.driveCamera.pitch==.95f && editor.driveCamera.yaw==0 && editor.driveCamera.target.x==4,"Drive did not seed its separate view with V2 orbit convention.");
        editor.driveCamera.Orbit(17,-9); editor.driveCamera.Zoom(120); editor.driveCamera.Pan(12,-8,1316); const auto free=CameraState(editor.driveCamera);
        for(int i=0;i<30;++i) { editor.race.Step({{1,0},0,false},1./120); editor.UpdateDriveCamera(); }
        Require(CameraState(editor.driveCamera)==free,"Free camera followed the moving car.");
        const auto position=editor.race.State().position; editor.Mode(EditorMode::Drive); Require(editor.race.State().position==position && CameraState(editor.driveCamera)==free,"Active Drive selection restarted race/view.");
        editor.SetFollowing(true); Centered(editor); Require(editor.driveCamera.yaw==free[3] && editor.driveCamera.pitch==free[4] && editor.driveCamera.height==free[5],"Follow changed orbit/zoom.");
        for(int i=0;i<30;++i) { editor.race.Step({{1,0},0,false},1./120); editor.UpdateDriveCamera(); Centered(editor); }
        const auto following=CameraState(editor.driveCamera); editor.SetFollowing(false);
        editor.race.Step({{1,0},0,false},1./120); editor.UpdateDriveCamera(); Require(CameraState(editor.driveCamera)==following,"Disabling follow jumped the camera.");
        editor.SetFollowing(true); editor.race.Reset(); editor.UpdateDriveCamera(); Centered(editor); Require(editor.race.State().position==editor.track.track.spawn.position,"Reset did not recenter on authored spawn.");
        editor.Frame(); Centered(editor); Require(CameraState(editor.trackCamera)==editing,"Drive framing changed the stored editing view.");
        editor.Mode(EditorMode::ModelBuilder); Require(CameraState(editor.modelCamera)==modelCamera && CameraState(editor.trackCamera)==editing,"Drive exit failed to restore independent editor cameras.");
        bool rejected=false; try { editor.SetFollowing(false); } catch(const std::invalid_argument&) { rejected=true; } Require(rejected && editor.following,"Hidden model follow command changed view preference.");
        editor.Mode(EditorMode::TrackBuilder); editor.Mode(EditorMode::Drive); Centered(editor); Require(editor.driveCamera.height==87 && editor.driveCamera.pitch==.95f && editor.driveCamera.yaw==0,"Reentry reused a mutated previous Drive view.");
        editor.Mode(EditorMode::TrackBuilder); Require(CameraState(editor.ViewCamera())==editing && editor.following,"Track return lost view/preference.");
        Require(SerializeTrack(editor.track)==track && SerializeScene(editor.model.scene)==model && editor.generated->surfaces[0].data()==road,"View changes dirtied authored data/assets or rebuilt surfaces.");
        editor.Undo(false); Require(SerializeTrack(editor.track)==before,"View-only settings added an authored history entry."); editor.Undo(true); Require(SerializeTrack(editor.track)==track,"View changes damaged redo.");
        std::cout<<"Camera-follow/free-view, exact editor restoration, Drive/reset/reentry and authored/history isolation passed.\n"; return 0;
    }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
