#include "Editor.h"
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

void Require(bool value,const char* text) { if(!value) throw std::runtime_error(text); }
template<class F> void Reject(F operation,const char* text) { try { operation(); } catch(const std::invalid_argument&) { return; } throw std::runtime_error(text); }
int main()
{
    try
    {
        Editor e; e.Mode(EditorMode::TrackBuilder); e.NewTrack(true); e.outline=1; e.point=5;
        e.TrackEdit([&] { auto& curve=e.track.track.outlines[1]; curve.controls[0].weight=.81; for(auto& knot:curve.knots) knot=20+2*knot; }); e.BuildTrack();
        const auto original=SerializeTrack(e.track),model=SerializeScene(e.model.scene); const auto curve=e.track.track.outlines[1];
        const auto geometry=e.generated->surfaces[0].data();
        Require(!e.SetOutlineDegree(3) && SerializeTrack(e.track)==original && e.generated->surfaces[0].data()==geometry,"Equal degree reset authored knots or invalidated geometry.");
        Require(e.SetOutlineDegree(2) && !e.generated && e.outline==1 && e.point==5,"Degree edit failed or changed selection/kept stale surfaces.");
        auto restored=e.track; restored.track.outlines[1].degree=curve.degree; restored.track.outlines[1].knots=curve.knots;
        Require(SerializeTrack(restored)==original && SerializeScene(e.model.scene)==model,"Degree edit changed controls/weights/color, another outline, assets or placements.");
        const auto& edited=e.track.track.outlines[1];
        Require(edited.knots.size()==edited.controls.size()+2*edited.degree+1 && (Racing2D::Evaluate(edited,0)-Racing2D::Evaluate(edited,1)).Length()<1e-10,"Regenerated periodic knot count/seam failed.");
        Require((Racing2D::Evaluate(edited,.25)-Racing2D::Evaluate(curve,.25)).Length()>.01,"Explicit degree edit did not exercise its documented shape change.");
        const auto changed=SerializeTrack(e.track); Require(SerializeTrack(DeserializeTrack(changed))==changed,"Degree/knots did not round trip exactly.");
        for(double invalid:{-1.,0.,1.5,4.,1e20,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()})
        {
            Reject([&] { e.SetOutlineDegree(invalid); },"Invalid degree accepted.");
            Require(SerializeTrack(e.track)==changed && !e.generated,"Rejected degree changed authoring or rebuilt surfaces.");
        }
        Require(e.Undo(false) && SerializeTrack(e.track)==original && !e.generated,"Degree undo did not restore original nonuniform knots or classify geometry change.");
        Require(e.Undo(true) && SerializeTrack(e.track)==changed && !e.generated,"Degree redo failed.");
        Reject([&] { e.Mode(EditorMode::Drive); },"Drive accepted stale/unbuilt degree geometry.");
        e.BuildTrack(); e.Mode(EditorMode::Drive); e.race.Step({{1,0},0,false},.02); e.Mode(EditorMode::TrackBuilder);
        Require(SerializeTrack(e.track)==changed,"Rebuilt degree/Drive transition mutated authoring.");
        e.outline=-1; Reject([&] { e.SetOutlineDegree(1); },"Missing outline selection accepted degree edit.");
        Editor rational; rational.Mode(EditorMode::TrackBuilder); rational.NewTrack(false); rational.outline=0;
        rational.TrackEdit([&] {
            Racing2D::NurbsOutline circle; circle.degree=2; const auto weight=std::sqrt(.5);
            circle.controls={{{0,-1},1},{{-1,-1},weight},{{-1,0},1},{{-1,1},weight},{{0,1},1},{{1,1},weight},{{1,0},1},{{1,-1},weight}};
            circle.knots={-1,0,0,1,1,2,2,3,3,4,4,5,5}; rational.track.track.outlines={circle};
        });
        const auto circleSaved=SerializeTrack(rational.track);
        Require(!rational.SetOutlineDegree(2) && SerializeTrack(rational.track)==circleSaved,"Equal degree rewrote a nonuniform rational circle's authored knots.");
        rational.SetOutlineDegree(3); rational.Undo(false);
        Require(SerializeTrack(rational.track)==circleSaved && std::abs(Racing2D::Evaluate(rational.track.track.outlines[0],.25).Length()-1)<1e-12,"Degree undo failed to restore exact rational circle knots/weights/shape.");
        for(unsigned count=2;count<=4;++count)
        {
            Editor small; small.Mode(EditorMode::TrackBuilder); small.NewTrack(false); small.outline=0;
            small.TrackEdit([&] {
                Racing2D::NurbsOutline next; next.degree=1;
                for(unsigned i=0;i<count;++i) { const auto angle=2*std::numbers::pi*i/count; next.controls.push_back({{std::cos(angle),std::sin(angle)},.7+.1*i}); }
                Racing2D::MakeUniformKnots(next); small.track.track.outlines={next};
            });
            for(unsigned degree=1;degree<=Racing2D::MaximumNurbsDegree;++degree)
            {
                const auto before=SerializeTrack(small.track);
                if(degree>=count) { Reject([&] { small.SetOutlineDegree(degree); },"Degree at/above control count accepted."); Require(SerializeTrack(small.track)==before,"Point-count rejection was not atomic."); }
                else { small.SetOutlineDegree(degree); const auto& next=small.track.track.outlines[0]; Require((Racing2D::Evaluate(next,0)-Racing2D::Evaluate(next,1)).Length()<1e-10,"Small weighted periodic curve did not close."); }
            }
        }
        std::cout<<"Degree/count bounds, atomic rejection, preserved controls/weights/authored data, periodic knots, explicit shape change, undo/redo, persistence and rebuilt Drive passed.\n";
        return 0;
    }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
