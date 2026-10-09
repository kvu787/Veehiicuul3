#include "Editor.h"
#include <iostream>
#include <iomanip>
#include <limits>
#include <stdexcept>

void Require(bool value,const char* text) { if(!value) throw std::runtime_error(text); }
template<class F> void Reject(F operation,const char* text) { try { operation(); } catch(const std::invalid_argument&) { return; } throw std::runtime_error(text); }
bool Near(double a,double b,double tolerance=1e-9) { return std::abs(a-b)<=tolerance; }
int main()
{
    try
    {
        using namespace Racing2D;
        Editor e; e.Mode(EditorMode::TrackBuilder); e.NewTrack(true); Require(e.CanDrive(),"Built example cannot Drive.");
        const auto original=SerializeTrack(e.track),model=SerializeScene(e.model.scene); const auto geometry=e.generated->surfaces[0].data();
        const auto initial=e.SelectedGate(); const auto pose=DescribeGate(initial);
        Require(!e.SetGatePlacement(pose) && SerializeTrack(e.track)==original && e.generated->surfaces[0].data()==geometry,"Derived gate no-op rewrote endpoints/history or lost built road.");
        auto equivalent=pose; equivalent.headingDegrees+=360;
        Require(!e.SetGatePlacement(equivalent) && SerializeTrack(e.track)==original,"Equivalent wrapped heading changed authored endpoints.");
        auto nonCardinal=pose; nonCardinal.headingDegrees=30.1; e.SetGatePlacement(nonCardinal); const auto angled=SerializeTrack(e.track);
        nonCardinal=DescribeGate(e.SelectedGate());
        for(double turns:{-2.,-1.,1.,2.}) { auto wrapped=nonCardinal; wrapped.headingDegrees+=360*turns; Require(!e.SetGatePlacement(wrapped) && SerializeTrack(e.track)==angled,"Non-cardinal full turns rewrote exact endpoints/history."); }
        Require(e.Undo(false) && SerializeTrack(e.track)==original,"Non-cardinal full-turn no-op added history."); e.Undo(true);
        auto tiny=nonCardinal; tiny.headingDegrees+=1e-9; Require(e.SetGatePlacement(tiny) && SerializeTrack(e.track)!=angled,"A meaningful tiny heading edit was swallowed."); e.Undo(false);
        tiny=nonCardinal; tiny.headingDegrees=std::nextafter(nonCardinal.headingDegrees+360.,std::numeric_limits<double>::infinity()); Require(e.SetGatePlacement(tiny),"A distinct adjacent wrapped double was swallowed."); e.Undo(false);
        e.Undo(false); Require(SerializeTrack(e.track)==original,"Wrapped/tiny-angle history did not restore original.");
        auto changed=pose; changed.center.x+=.25; changed.center.y+=.25; changed.width=8; changed.headingDegrees=180;
        Require(e.SetGatePlacement(changed) && e.generated->surfaces[0].data()==geometry && e.CanDrive(),"Checkered placement failed, dropped built road or invalidated valid Drive.");
        const auto finish=SerializeTrack(e.track); auto restored=e.track; restored.track.finish=initial;
        Require(SerializeTrack(restored)==original && SerializeScene(e.model.scene)==model,"Gate edit changed outlines/knots/paints, another gate, spawn, assets or decorations.");
        const auto actual=DescribeGate(e.SelectedGate()); Require(Near(actual.center.x,changed.center.x) && Near(actual.center.y,changed.center.y) && Near(actual.width,8) && Near(std::abs(actual.headingDegrees),180),"Center/heading/width convention differs from V2.");
        Require(e.Undo(false) && SerializeTrack(e.track)==original && e.generated->surfaces[0].data()==geometry,"Checkered undo lost endpoints or cached road.");
        Require(e.Undo(true) && SerializeTrack(e.track)==finish && e.generated->surfaces[0].data()==geometry,"Checkered redo lost endpoints or cached road.");
        for(unsigned i=1;i<=e.track.track.checkpoints.size();++i)
        {
            e.gate=static_cast<int>(i); const auto before=SerializeTrack(e.track); const auto old=e.SelectedGate(); auto p=DescribeGate(old); p.width=8;
            e.SetGatePlacement(p); auto isolated=e.track; isolated.track.checkpoints[i-1]=old;
            Require(SerializeTrack(isolated)==before && e.generated->surfaces[0].data()==geometry,"Checkpoint editing changed another gate/order or road geometry.");
            const auto gate=e.SelectedGate(); const auto edge=gate.b-gate.a; const auto normal=Point{-edge.y,edge.x}*(1/edge.Length()); const auto center=(gate.a+gate.b)*.5;
            Require(Crosses(gate,center-normal,center+normal) && !Crosses(gate,center+normal,center-normal),"Gate forward-heading edit reversed directed crossing.");
        }
        const auto saved=SerializeTrack(e.track); Require(SerializeTrack(DeserializeTrack(saved))==saved,"Gate endpoints/order did not persist exactly.");
        auto invalid=[&](GatePlacement p) { Reject([&] { e.SetGatePlacement(p); },"Invalid gate numeric candidate accepted."); Require(SerializeTrack(e.track)==saved && e.generated->surfaces[0].data()==geometry,"Rejected gate edit changed authoring/generated storage."); };
        auto p=DescribeGate(e.SelectedGate());
        for(double width:{0.,.099,1000.01,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) { auto bad=p; bad.width=width; invalid(bad); }
        for(double value:{10001.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) { auto bad=p; bad.center.x=value; invalid(bad); }
        auto bad=p; bad.headingDegrees=std::numeric_limits<double>::infinity(); invalid(bad); bad=p; bad.center={10000,10000}; bad.headingDegrees=45; invalid(bad);
        e.gate=0; const auto onRoad=e.SelectedGate(); auto offRoad=DescribeGate(onRoad); offRoad.center={500,500}; e.SetGatePlacement(offRoad);
        const auto offRoadSaved=SerializeTrack(e.track); Require(!e.CanDrive() && e.generated->surfaces[0].data()==geometry,"Off-road placement discarded road or enabled Drive.");
        Reject([&] { e.Mode(EditorMode::Drive); },"Drive accepted an off-road gate."); Require(e.mode==EditorMode::TrackBuilder && SerializeTrack(e.track)==offRoadSaved && !e.race.Active(),"Drive rejection changed authoring/mode/session.");
        e.Undo(false); Require(e.CanDrive() && e.SelectedGate().a==onRoad.a && e.SelectedGate().b==onRoad.b,"Gate undo did not restore Drive readiness.");
        const auto clean=SerializeTrack(e.track); e.Mode(EditorMode::Drive); e.race.Step({{1,0},0,false},.02); Reject([&] { e.SetGatePlacement(p); },"Driving mode allowed gate authoring."); e.Mode(EditorMode::TrackBuilder);
        Require(SerializeTrack(e.track)==clean && SerializeScene(e.model.scene)==model,"Gate Drive transition changed authored project/assets.");
        e.gate=-1; Reject([&] { e.SetGatePlacement(p); },"Missing gate selection accepted editing."); e.gate=33; Reject([&] { e.SetGatePlacement(p); },"Invalid checkpoint selection accepted editing.");
        for(auto center:std::vector<Point>{{0,0},{28,18},{9000,-9000}}) for(double heading:{0.,90.,180.,270.,45.,1e308}) for(double width:{.1,1000.})
        {
            const auto gate=PlaceGate({center,heading,width}); const auto measured=DescribeGate(gate);
            if(!(measured.width>=.1 && measured.width<=1000 && Near(measured.center.x,center.x) && Near(measured.center.y,center.y)))
            {
                std::cerr<<std::setprecision(17)<<"Boundary case center "<<center.x<<','<<center.y<<" heading "<<heading<<" width "<<width<<" measured "<<measured.width<<" center "<<measured.center.x<<','<<measured.center.y<<'\n';
                throw std::runtime_error("Valid boundary width failed at finite center/heading.");
            }
        }
        const auto positiveX=PlaceGate({{0,0},0,2}),positiveY=PlaceGate({{0,0},90,2});
        Require(Crosses(positiveX,{-1,0},{1,0}) && Crosses(positiveY,{0,-1},{0,1}),"V2 degree convention 0:+X, 90:+Y was lost.");
        e.NewTrack(false); Require(!e.GateSelected() && !e.CanDrive(),"Unplaced gates were editable/driveable."); e.gate=0; Reject([&] { e.SetGatePlacement(p); },"Unplaced checkered line accepted editing.");
        e.NewTrack(true); const auto surfaces=e.generated->surfaces[0].data(); e.tool=Editor::Tool::Checkpoint; e.Place({25,0}); e.Place({32,0});
        Require(e.gate==4 && e.tool==Editor::Tool::Select && e.generated->surfaces[0].data()==surfaces,"Placed checkpoint was not selected or unnecessarily lost built surfaces.");
        std::cout<<"Selected gate isolation, V2 degree/direction/width conventions, atomic rejection, exact no-op, cached road, history, persistence and Drive readiness passed.\n";
        return 0;
    }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
