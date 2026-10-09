#include "Editor.h"
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#ifdef _DEBUG
#include <crtdbg.h>
#endif

void Require(bool value,const char* text) { if(!value) throw std::runtime_error(text); }
template<class F> void Reject(F operation,const char* text) { try { operation(); } catch(const std::invalid_argument&) { return; } throw std::runtime_error(text); }
int main(int argc,char** argv)
{
    if(argc==2 && std::strcmp(argv[1],"--probe-headless-assert")==0)
    {
#ifdef _DEBUG
        _CrtDbgReport(_CRT_ASSERT,"TrackDeletionTests",0,nullptr,"Controlled headless assertion probe.");
        _invalid_parameter_noinfo_noreturn();
#else
        std::_Exit(1);
#endif
    }
    try
    {
        Editor e; e.Mode(EditorMode::TrackBuilder); e.NewTrack(true); e.outline=0;
        e.TrackEdit([&] { auto& c=e.track.track.outlines[0]; c.controls[0].weight=.81; for(auto& k:c.knots) k=20+2*k; }); e.BuildTrack();
        const auto original=SerializeTrack(e.track),model=SerializeScene(e.model.scene); const auto old=e.track.track.outlines[0];
        for(int selected:{0,5,11})
        {
            e.point=selected; e.gate=2; const auto surfaces=e.generated->surfaces[0].data(); Require(e.CanDeletePoint(),"Valid selected control could not be deleted.");
            e.DeletePoint(); Require(!e.generated && !e.CanDrive(),"Control deletion retained stale surfaces/Drive.");
            auto expected=e.track; expected.track.outlines[0]=old;
            Require(SerializeTrack(expected)==original && e.outline==0 && e.point==std::min(selected,10) && e.gate==2,"Control deletion changed unrelated data or left selection inconsistent.");
            auto curve=old; curve.controls.erase(curve.controls.begin()+selected); Racing2D::MakeUniformKnots(curve);
            Require(e.track.track.outlines[0].knots==curve.knots && e.track.track.outlines[0].controls.size()==11 && e.track.track.outlines[0].degree==old.degree,"Deletion did not preserve degree/reset only knots/remove one control.");
            for(size_t i=0;i<curve.controls.size();++i) Require(e.track.track.outlines[0].controls[i].position==curve.controls[i].position && e.track.track.outlines[0].controls[i].weight==curve.controls[i].weight,"Surviving control position/weight changed.");
            const auto deleted=SerializeTrack(e.track); Require(SerializeTrack(DeserializeTrack(deleted))==deleted,"Deleted curve did not persist exactly.");
            e.Undo(false); Require(SerializeTrack(e.track)==original && e.point==selected && e.gate==2 && !e.generated,"Control undo lost exact original knots/weights/selection or reused stale road.");
            e.Undo(true); Require(SerializeTrack(e.track)==deleted && e.point==std::min(selected,10),"Control redo failed.");
            e.BuildTrack(); Require(e.CanDrive(),"Valid deleted example did not rebuild for Drive.");
            e.Undo(false); Require(!e.generated && SerializeTrack(e.track)==original,"Undo of rebuilt shape kept stale generated surfaces."); e.BuildTrack(); Require(e.generated->surfaces[0].data()!=nullptr && surfaces!=nullptr,"Road rebuild failed.");
        }
        for(unsigned degree:{1u,2u,3u})
        {
            Editor minimum; minimum.Mode(EditorMode::TrackBuilder); minimum.NewTrack(false);
            Racing2D::NurbsOutline curve; curve.degree=degree; curve.name="Minimum"; const auto count=std::max(3u,degree+1);
            for(unsigned i=0;i<count+1;++i) { const auto a=static_cast<double>(i)*6.283185307179586/(count+1); curve.controls.push_back({{10*std::cos(a),10*std::sin(a)},1}); }
            Racing2D::MakeUniformKnots(curve); minimum.TrackEdit([&] { minimum.track.track.outlines.push_back(curve); }); minimum.outline=0; minimum.point=static_cast<int>(count);
            minimum.DeletePoint(); Require(!minimum.CanDeletePoint() && minimum.point==static_cast<int>(count)-1,"Minimum curve constraints/last-point selection failed.");
            const auto saved=SerializeTrack(minimum.track); Reject([&] { minimum.DeletePoint(); },"Deletion crossed V2 minimum control count."); Require(SerializeTrack(minimum.track)==saved,"Rejected minimum deletion mutated data.");
            minimum.Undo(false); Require(minimum.point==static_cast<int>(count) && minimum.CanDeletePoint(),"Minimum rejection changed history/selection.");
        }
        const auto surfaces=e.generated->surfaces[0].data();
        for(int selected:{1,2,3})
        {
            e.gate=selected; e.point=7; const auto gate=e.SelectedGate(); e.DeleteGate();
            auto expected=e.track; expected.track.checkpoints.insert(expected.track.checkpoints.begin()+(selected-1),gate);
            Require(SerializeTrack(expected)==original && e.gate==std::min(selected,2) && e.point==7 && e.generated->surfaces[0].data()==surfaces && e.CanDrive(),"Selected checkpoint deletion changed order/unrelated data, selection, cached road or valid Drive.");
            const auto deleted=SerializeTrack(e.track); Require(SerializeTrack(DeserializeTrack(deleted))==deleted,"Deleted checkpoint did not persist exactly.");
            e.Undo(false); Require(SerializeTrack(e.track)==original && e.gate==selected && e.point==7 && e.generated->surfaces[0].data()==surfaces,"Checkpoint undo did not restore exact endpoints/order/selection/road.");
            e.Undo(true); Require(SerializeTrack(e.track)==deleted && e.gate==std::min(selected,2),"Checkpoint redo failed."); e.Undo(false);
        }
        e.gate=0; const auto endpoints=e.SelectedGate(); e.DeleteGate(); Require(!e.track.track.hasFinish && e.track.track.finish.a==endpoints.a && e.track.track.finish.b==endpoints.b && e.gate==1 && !e.CanDrive() && e.generated->surfaces[0].data()==surfaces,"Finish deletion lost payload/cached road or enabled Drive.");
        auto expected=e.track; expected.track.hasFinish=true; Require(SerializeTrack(expected)==original,"Finish deletion changed other authored data.");
        Reject([&] { e.Mode(EditorMode::Drive); },"Drive accepted deleted finish."); e.Undo(false); Require(SerializeTrack(e.track)==original && e.gate==0 && e.CanDrive(),"Finish undo failed.");
        for(int i=3;i>=1;--i) { e.gate=i; e.DeleteGate(); }
        Require(e.gate==0 && !e.CanDrive() && e.generated->surfaces[0].data()==surfaces,"Last checkpoint deletion did not fall back to finish or disable Drive.");
        e.DeleteGate(); Require(e.gate==-1 && !e.CanDeleteGate(),"No-gate selection stayed editable."); const auto empty=SerializeTrack(e.track); Reject([&] { e.DeleteGate(); },"Missing gate deleted."); Require(SerializeTrack(e.track)==empty,"Rejected missing deletion changed authored state.");
        for(int i=0;i<4;++i) e.Undo(false); Require(SerializeTrack(e.track)==original && e.CanDrive(),"Gate deletion sequence did not undo exactly.");
        e.outline=0; e.point=11; e.DeletePoint(); e.Undo(false); const auto selection=e.point; const auto gateSelection=e.gate;
        Reject([&] { e.TrackEdit([&] { e.point=-999; e.gate=-1; e.track.track.outlines[0].controls[0].weight=-1; }); },"Invalid transaction accepted.");
        Require(SerializeTrack(e.track)==original && e.point==selection && e.gate==gateSelection && e.CanUndo(true),"Failed transaction changed selection/project/redo history.");
        e.point=-1; Reject([&] { e.DeletePoint(); },"Missing control deleted."); e.point=999; Reject([&] { e.DeletePoint(); },"Invalid control index deleted."); e.point=0;
        e.BeginOutlineColor(); Require(!e.CanDeletePoint() && !e.CanDeleteGate(),"Color gesture allowed deletion."); Reject([&] { e.DeletePoint(); },"Held color gesture permitted deletion."); e.EndOutlineColor(true);
        e.draft.push_back({{0,0},1}); Require(!e.CanDeletePoint() && !e.CanDeleteGate(),"Outline draft allowed deletion."); e.draft.clear(); e.gateStart=Racing2D::Point{0,0}; Require(!e.CanDeletePoint() && !e.CanDeleteGate(),"Gate gesture allowed deletion."); e.gateStart.reset();
        e.BuildTrack(); e.Mode(EditorMode::Drive); const auto driving=SerializeTrack(e.track); Reject([&] { e.DeletePoint(); },"Drive allowed point deletion."); Reject([&] { e.DeleteGate(); },"Drive allowed gate deletion."); e.Mode(EditorMode::TrackBuilder);
        Require(SerializeTrack(e.track)==driving && SerializeScene(e.model.scene)==model,"Rejected Drive deletion or transition changed authoring/assets.");
        Editor color; color.Mode(EditorMode::TrackBuilder); color.NewTrack(true); color.outline=0; color.point=5; color.gate=2; const auto colorRoad=color.generated->surfaces[0].data();
        color.PreviewOutlineColor(0,.2); color.EndOutlineColor(); color.outline=2; color.point=0; color.gate=0;
        Require(!color.Undo(false) && color.outline==0 && color.point==5 && color.gate==2 && color.generated->surfaces[0].data()==colorRoad,"Material-only history lost recorded selection or regenerated boundaries.");
        std::cout<<"Selected control/gate deletion, V2 minima/uniform reset, selection/history rollback, exact persistence, cached/stale roads and Drive constraints passed.\n"; return 0;
    }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
