#include "Editor.h"
#include "SurfaceMaterials.h"
#include <iostream>
#include <limits>
#include <stdexcept>

void Require(bool value,const char* text) { if(!value) throw std::runtime_error(text); }
int main()
{
    try
    {
        Editor e; e.Mode(EditorMode::TrackBuilder); e.NewTrack(true); e.outline=0;
        const PaintRange rgb{0,1,10000};
        Require(DraggedValue(.5,-100000,388,1,rgb)==0 && DraggedValue(.5,100000,388,1,rgb)==1,"Unlit RGB drag excluded exact black or white.");
        Require(DraggedValue(.5,-100000,388,1,PaintParameter::Red)>0 && DraggedValue(.5,100000,388,1,PaintParameter::Red)<1,"Unlit RGB drag changed SimplePaint's reserved margins.");
        Require(std::abs(DraggedValue(.5,1,388,.0001,rgb)-(.5+.0001/388))<1e-15,"Fine RGB dragging lost continuous precision.");
        const auto original=SerializeTrack(e.track),model=SerializeScene(e.model.scene),vehicle=SerializeScene(e.track.vehicle);
        const auto geometry=e.generated->surfaces[0].data(); const auto road=e.track.track.outlines[1].colorSrgb;
        e.PreviewOutlineColor(0,.91); e.PreviewOutlineColor(1,.18);
        Require(!e.EndOutlineColor(true) && SerializeTrack(e.track)==original,"Canceled color gesture changed authored track.");
        for(unsigned channel=0;channel<3;++channel) e.PreviewOutlineColor(channel,std::array<double,3>{.91,.18,.07}[channel]);
        Require(e.EndOutlineColor() && e.generated->surfaces[0].data()==geometry,"Committed RGB edit lost built geometry.");
        const auto colored=SerializeTrack(e.track); const auto roundTrip=DeserializeTrack(colored);
        Require(SerializeTrack(roundTrip)==colored && e.track.track.outlines[1].colorSrgb==road,"RGB round trip or selected-outline isolation failed.");
        const auto scene=MakeTrackScene(e.track,&*e.generated);
        Require(scene.objects[0].materialKind==MaterialKind::UnlitGround && scene.objects[0].unlitColorSrgb==std::array<double,3>{.91,.18,.07} && CompileSurfaceMaterial(scene.objects[0])[3]==1,"RGB changed the explicit unlit surface material contract.");
        Require(SerializeScene(e.model.scene)==model && SerializeScene(e.track.vehicle)==vehicle && e.track.vehicle.objects[0].materialKind==MaterialKind::SimplePaint,"Track RGB changed source model/vehicle paint.");
        e.BeginOutlineColor(); Require(!e.EndOutlineColor(),"Unchanged color gesture reported an edit.");
        e.PreviewOutlineColor(0,.91); Require(!e.EndOutlineColor(),"Equal RGB value reported an edit.");
        e.Undo(false); Require(SerializeTrack(e.track)==original && e.generated->surfaces[0].data()==geometry,"Single RGB undo failed or invalidated unchanged track geometry.");
        e.Undo(true); Require(SerializeTrack(e.track)==colored && e.generated->surfaces[0].data()==geometry,"RGB redo failed or invalidated unchanged track geometry.");
        for(double invalid:{-.01,1.01,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()})
        {
            bool rejected=false; try { e.PreviewOutlineColor(0,invalid); } catch(const std::invalid_argument&) { rejected=true; }
            Require(rejected && !e.EndOutlineColor() && SerializeTrack(e.track)==colored,"Invalid RGB edit mutated authored state/history.");
        }
        e.outline=-1; bool rejected=false; try { e.BeginOutlineColor(); } catch(const std::invalid_argument&) { rejected=true; }
        Require(rejected,"Missing outline selection accepted a color gesture."); e.outline=0;
        e.Mode(EditorMode::Drive); e.race.Step({{1,0},0,false},.02); e.Mode(EditorMode::TrackBuilder);
        Require(SerializeTrack(e.track)==colored && e.generated->surfaces[0].data()==geometry,"Drive transition lost RGB or built geometry.");
        e.TrackEdit([&] { e.track.track.outlines[0].controls[0].weight=.8; }); e.BuildTrack(); e.Undo(false);
        Require(!e.generated,"Undoing a shape change retained stale generated geometry.");
        std::cout<<"Selected outline RGB, cancel/no-op/undo/redo, unchanged geometry, unlit/SimplePaint distinction, persistence and Drive passed.\n";
        return 0;
    }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
