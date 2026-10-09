#include "Editor.h"
#include <iostream>
#include <stdexcept>
void Check(bool c,const char* text) { if(!c) throw std::runtime_error(text); }
int main()
{
    try
    {
        Editor e;Check(e.model.scene.objects.size()==5,"Actual source SlopeCar missing");const auto seed=SerializeScene(e.model.scene);
        e.PreviewPaint(PaintParameter::Red,.3);e.EndPaint(true);Check(SerializeScene(e.model.scene)==seed,"Paint cancel lost authored state");e.PreviewPaint(PaintParameter::Red,.3);e.EndPaint();e.Undo(false);Check(SerializeScene(e.model.scene)==seed,"Paint undo wrong");e.Undo(true);Check(e.Material().paint.baseColorSrgb[0]==.3,"Paint redo wrong");
        e.ModelEdit([&]{e.model.scene=Scene{};e.model.selection={SelectionMode::Face,0,0,{},0};});e.CopyMaterial();e.AssignMaterial();e.Extrude();e.Refine();Check(MaterialCount(e.model.scene)==2,"Face material slots lost");const auto authored=SerializeScene(e.model.scene);
        e.Mode(EditorMode::TrackBuilder);e.NewTrack(true);const auto track=SerializeTrack(e.track);e.Mode(EditorMode::Drive);for(int i=0;i<120;++i)e.race.Step({{1,0},0,false},1./120);Check(e.race.State().velocity.Length()>0,"Planar race did not run");e.Mode(EditorMode::ModelBuilder);Check(SerializeScene(e.model.scene)==authored && SerializeTrack(e.track)==track,"Mode change lost authored state");Check(!e.race.Active(),"Drive not stopped on mode change");
        e.Mode(EditorMode::TrackBuilder);e.tool=Editor::Tool::Checkpoint;e.Place({20,0});e.Place({20,5});Check(e.track.track.checkpoints.size()==4,"Gate placement failed");e.Undo(false);Check(SerializeTrack(e.track)==track,"Independent track undo wrong");
        e.NewTrack(false);e.tool=Editor::Tool::Outline;for(auto p:std::vector<Racing2D::Point>{{-50,-50},{50,-50},{50,50},{-50,50}})e.Place(p);e.FinishOutline();Check(e.track.track.outlines.size()==1 && !e.track.track.outlines[0].knots.empty(),"Periodic NURBS authoring failed");
        std::cout<<"Shared model/material/undo, track/NURBS placement, planar drive and mode state passed.\n";return 0;
    }
    catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}
}
