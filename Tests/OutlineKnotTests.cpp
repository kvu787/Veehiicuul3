#include "Editor.h"
#include <iostream>
#include <limits>
#include <stdexcept>

void Require(bool value,const char* text) { if(!value) throw std::runtime_error(text); }
template<class F> void Reject(F operation,const char* text) { try { operation(); } catch(const std::invalid_argument&) { return; } throw std::runtime_error(text); }
int main()
{
    try
    {
        using namespace Racing2D;
        Editor e; e.Mode(EditorMode::TrackBuilder); e.NewTrack(true); e.outline=0; e.point=5;
        e.TrackEdit([&] { e.track.track.outlines[0].controls[0].weight=.81; }); e.BuildTrack();
        const auto original=SerializeTrack(e.track),model=SerializeScene(e.model.scene); const auto oldKnots=e.track.track.outlines[0].knots;
        const auto geometry=e.generated->surfaces[0].data();
        auto formatted=FormatKnots(oldKnots); for(auto& c:formatted) if(c==' ') c=',';
        Require(e.SetOutlineKnots(" \n"+formatted+"\t")==Editor::KnotEdit::Unchanged && SerializeTrack(e.track)==original && e.generated->surfaces[0].data()==geometry,"Equivalent numeric vector dirtied authoring/history or invalidated geometry.");
        auto scaled=oldKnots; for(auto& k:scaled) k*=2;
        Require(e.SetOutlineKnots(FormatKnots(scaled))==Editor::KnotEdit::SameBoundary && e.generated->surfaces[0].data()==geometry,"Exactly equal sampled boundaries invalidated built geometry.");
        Require(!e.Undo(false) && SerializeTrack(e.track)==original && e.generated->surfaces[0].data()==geometry,"Parameter-only knot undo rebuilt unchanged boundaries.");
        Require(!e.Undo(true) && e.track.track.outlines[0].knots==scaled && e.generated->surfaces[0].data()==geometry,"Parameter-only knot redo rebuilt unchanged boundaries."); e.Undo(false);
        auto changed=oldKnots; const auto n=e.track.track.outlines[0].controls.size();
        for(size_t i=1;i<changed.size();++i) changed[i]=changed[i-1]+(i%n==4 ? .5 : 1.);
        Require(e.SetOutlineKnots(FormatKnots(changed))==Editor::KnotEdit::ChangedBoundary && !e.generated && e.outline==0 && e.point==5,"Nonuniform knot edit failed, changed selection or kept stale geometry.");
        auto restored=e.track; restored.track.outlines[0].knots=oldKnots;
        Require(SerializeTrack(restored)==original && SerializeScene(e.model.scene)==model,"Knot edit changed degree/points/weights/colors, another outline, assets or placements.");
        const auto saved=SerializeTrack(e.track); Require(SerializeTrack(DeserializeTrack(saved))==saved,"Explicit knots did not persist exactly.");
        auto invalid=[&](const std::string& input) { Reject([&] { e.SetOutlineKnots(input); },"Invalid knot vector accepted."); Require(SerializeTrack(e.track)==saved && !e.generated,"Rejected knot edit changed authoring/geometry."); };
        for(const char* input:{"", " ", "1,,2", "1,", "+-1 2", "0x1 2", "1e 2", "nan", "inf", "1e309", "1e-400"}) invalid(input);
        invalid(std::string(MaximumKnotTextLength+1,'0'));
        auto wrongLength=changed; wrongLength.pop_back(); invalid(FormatKnots(wrongLength));
        auto descending=changed; descending[5]=descending[4]-1; invalid(FormatKnots(descending));
        auto inconsistent=changed; inconsistent.back()+=.25; auto seam=e.track.track.outlines[0]; seam.knots=inconsistent;
        (void)Tessellate(seam); invalid(FormatKnots(inconsistent)); // A closing seam alone misses unused extension corruption.
        invalid(FormatKnots(std::vector<double>(changed.size(),0)));
        auto nonfinite=changed; nonfinite[0]=std::numeric_limits<double>::infinity(); invalid(FormatKnots(nonfinite));
        auto unbounded=changed; unbounded.back()=100001; invalid(FormatKnots(unbounded));
        Require(e.Undo(false) && SerializeTrack(e.track)==original,"Rejected edits mutated history or undo failed to restore the whole original track.");
        Require(e.Undo(true) && SerializeTrack(e.track)==saved,"Explicit knot redo failed.");
        Reject([&] { e.Mode(EditorMode::Drive); },"Drive accepted unbuilt knot geometry."); e.BuildTrack(); e.Mode(EditorMode::Drive); e.race.Step({{1,0},0,false},.02); e.Mode(EditorMode::TrackBuilder);
        Require(SerializeTrack(e.track)==saved,"Built knot Drive transition changed authoring.");
        e.BeginOutlineColor(); Reject([&] { e.SetOutlineKnots(FormatKnots(changed)); },"Knots replaced an active RGB transaction."); e.EndOutlineColor(true);
        e.outline=-1; Reject([&] { e.SetOutlineKnots(FormatKnots(changed)); },"Missing selection accepted knot editing.");
        NurbsOutline circle; circle.degree=2; const auto w=std::sqrt(.5);
        circle.controls={{{0,-1},1},{{-1,-1},w},{{-1,0},1},{{-1,1},w},{{0,1},1},{{1,1},w},{{1,0},1},{{1,-1},w}};
        circle.knots={-1,0,0,1,1,2,2,3,3,4,4,5,5}; ValidatePeriodicKnots(circle);
        Require(ParseKnots("-1, +0,0 1 1 2 2 3 3 4 4 5e0 5")==circle.knots,"Mixed decimal/scientific/whitespace/comma forms changed repeated knots.");
        for(unsigned i=0;i<=100;++i) Require(std::abs(Evaluate(circle,i/100.).Length()-1)<1e-12,"Rational repeated-knot circle changed.");
        auto maximum=circle; maximum.degree=3; maximum.controls.resize(64,{{1,0},1}); MakeUniformKnots(maximum); ValidatePeriodicKnots(maximum);
        Require(maximum.knots.size()==MaximumKnotCount && ParseKnots(FormatKnots(maximum.knots))==maximum.knots,"Maximum supported vector did not round trip.");
        std::cout<<"Bounded knot parsing, periodic extensions/seam, atomic rejection, preserved authoring, exact sampled-geometry reuse, history, rational repeats, persistence and rebuilt Drive passed.\n";
        return 0;
    }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
