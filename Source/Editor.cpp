#include "Editor.h"
#include <algorithm>
#include <numeric>

namespace
{
bool SameTrackGeometry(const Racing2D::Track& a,const Racing2D::Track& b)
{
    if(a.outlines.size()!=b.outlines.size()) return false;
    for(size_t i=0;i<a.outlines.size();++i)
    {
        const auto& x=a.outlines[i]; const auto& y=b.outlines[i];
        if(x.degree!=y.degree || x.knots!=y.knots || x.controls.size()!=y.controls.size()) return false;
        for(size_t p=0;p<x.controls.size();++p)
            if(x.controls[p].position!=y.controls[p].position || x.controls[p].weight!=y.controls[p].weight) return false;
    }
    return true;
}
}

ModelObject& Editor::Object()
{
    if(model.selection.object<0 || static_cast<size_t>(model.selection.object)>=model.scene.objects.size()) throw std::invalid_argument("Select a model part first.");
    return model.scene.objects[static_cast<size_t>(model.selection.object)];
}
MaterialSlot& Editor::Material() { return ObjectMaterial(Object(),model.selection.material); }
const MaterialSlot& Editor::Material() const { return ObjectMaterial(model.scene.objects.at(static_cast<size_t>(model.selection.object)),model.selection.material); }
bool Editor::Editable() const { return model.selection.object>=0 && !model.scene.objects.at(static_cast<size_t>(model.selection.object)).evaluatedSurface; }
std::vector<uint32_t> Editor::SelectedVertices() const
{
    if(model.selection.object<0) return {};
    const auto& object=model.scene.objects.at(static_cast<size_t>(model.selection.object));
    if(model.selection.mode==SelectionMode::Vertex) return model.selection.vertices;
    if(model.selection.mode==SelectionMode::Face) return model.selection.face>=0 ? object.cage.faces.at(static_cast<size_t>(model.selection.face)) : std::vector<uint32_t>{};
    std::vector<uint32_t> all(object.cage.positions.size()); std::iota(all.begin(),all.end(),0); return all;
}
Vector3 Editor::Center() const
{
    if(model.selection.object<0) return {};
    const auto& object=model.scene.objects.at(static_cast<size_t>(model.selection.object));
    if(model.selection.mode==SelectionMode::Object || object.evaluatedSurface) return object.position;
    const auto selected=SelectedVertices(); Vector3 center{};
    for(auto v:selected) center+=object.cage.positions.at(v);
    return selected.empty() ? center : center/static_cast<float>(selected.size());
}
void Editor::ModelEdit(const std::function<void()>& operation)
{
    auto previous=model;
    try { operation(); ValidateScene(model.scene); }
    catch(...) { model=std::move(previous); throw; }
    if(modelUndo_.size()>=128) modelUndo_.erase(modelUndo_.begin());
    modelUndo_.push_back(std::move(previous)); modelRedo_.clear();
}
void Editor::TrackEdit(const std::function<void()>& operation)
{
    auto previous=track;
    try { operation(); ValidateTrackProject(track,false); }
    catch(...) { track=std::move(previous); throw; }
    if(trackUndo_.size()>=128) trackUndo_.erase(trackUndo_.begin());
    trackUndo_.push_back({std::move(previous),false}); trackRedo_.clear(); generated.reset();
}
bool Editor::CanUndo(bool redo) const { return mode==EditorMode::ModelBuilder ? !(redo ? modelRedo_ : modelUndo_).empty() : mode==EditorMode::TrackBuilder && !(redo ? trackRedo_ : trackUndo_).empty(); }
bool Editor::Undo(bool redo)
{
    if(!CanUndo(redo)) return false;
    if(mode==EditorMode::ModelBuilder)
    {
        auto& from=redo ? modelRedo_ : modelUndo_; auto& to=redo ? modelUndo_ : modelRedo_;
        to.push_back(model); model=std::move(from.back()); from.pop_back(); return true;
    }
    else
    {
        auto& from=redo ? trackRedo_ : trackUndo_; auto& to=redo ? trackUndo_ : trackRedo_;
        const bool sameGeometry=SameTrackGeometry(track.track,from.back().project.track),materialOnly=from.back().materialOnly;
        to.push_back({track,materialOnly}); track=std::move(from.back().project); from.pop_back(); if(!sameGeometry) generated.reset();
        return !materialOnly;
    }
}
void Editor::AddShape(int kind)
{
    ModelEdit([&] { ModelObject object; object.cage=kind==1 ? MakePlane() : kind==2 ? MakeCylinder() : MakeBox(); object.name=kind==1 ? "Plane" : kind==2 ? "Cylinder" : "Box"; model.scene.objects.push_back(std::move(object)); model.selection={SelectionMode::Vertex,static_cast<int>(model.scene.objects.size()-1),-1,{},0}; });
}
void Editor::Refine() { if(!Editable()) throw std::invalid_argument("Source SlopeCar is evaluated geometry, not an editable cage."); ModelEdit([&] { Object().cage=Subdivide(Object().cage,1); model.selection.face=-1; model.selection.vertices.clear(); }); }
void Editor::Extrude() { if(!Editable() || model.selection.face<0) throw std::invalid_argument("Select an editable control face first."); ModelEdit([&] { ExtrudeFace(Object().cage,static_cast<size_t>(model.selection.face),step); }); }
void Editor::Transform(Vector3 position,Vector3 rotation,float scale)
{
    ModelEdit([&] {
        auto& object=Object();
        if(model.selection.mode==SelectionMode::Object || object.evaluatedSurface) { object.position=position; object.rotationDegrees=rotation; object.scale=scale; return; }
        const auto selected=SelectedVertices(); if(selected.empty()) throw std::invalid_argument("Select cage vertices or a face first.");
        const auto center=Center();
        const auto matrix=DirectX::XMMatrixScaling(scale,scale,scale)*DirectX::XMMatrixRotationRollPitchYaw(DirectX::XMConvertToRadians(rotation.x),DirectX::XMConvertToRadians(rotation.y),DirectX::XMConvertToRadians(rotation.z));
        for(auto v:selected) { const auto p=object.cage.positions.at(v)-center; DirectX::XMFLOAT3 result{}; DirectX::XMStoreFloat3(&result,DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(p.x,p.y,p.z,1),matrix)); object.cage.positions[v]=position+Vector3{result.x,result.y,result.z}; }
    });
}
void Editor::Nudge(Vector3 delta)
{
    const auto& object=Object(); Transform(Center()+delta,model.selection.mode==SelectionMode::Object ? object.rotationDegrees : Vector3{},model.selection.mode==SelectionMode::Object ? object.scale : 1);
}
void Editor::CopyMaterial() { ModelEdit([&] { model.selection.material=AddMaterialSlot(model.scene,static_cast<size_t>(model.selection.object),model.selection.material); }); }
void Editor::AssignMaterial() { if(model.selection.face<0) throw std::invalid_argument("Select a face."); ModelEdit([&] { AssignFaceMaterial(model.scene,static_cast<size_t>(model.selection.object),static_cast<size_t>(model.selection.face),model.selection.material); }); }
void Editor::BeginPaint() { if(!paintStart_) paintStart_=model; }
void Editor::PreviewPaint(PaintParameter p,double value)
{
    const auto range=ParameterRange(p); if(!std::isfinite(value) || value<range.minimum || value>range.maximum) throw std::invalid_argument("Paint outside valid range.");
    BeginPaint(); auto paint=Material().paint; SetParameter(paint,p,value); (void)SimplePaint::Material::Compile(paint); Material().paint=paint;
}
bool Editor::EndPaint(bool cancel)
{
    if(!paintStart_) return false;
    const bool changed=!cancel && SerializeScene(model.scene)!=SerializeScene(paintStart_->scene);
    if(cancel) model=*paintStart_;
    else if(changed) { if(modelUndo_.size()>=128) modelUndo_.erase(modelUndo_.begin()); modelUndo_.push_back(*paintStart_); modelRedo_.clear(); }
    paintStart_.reset(); return changed;
}
void Editor::BeginOutlineColor()
{
    if(mode!=EditorMode::TrackBuilder || outline<0 || static_cast<size_t>(outline)>=track.track.outlines.size()) throw std::invalid_argument("Select a track outline to edit its color.");
    if(!colorStart_) { colorStart_=track; colorOutline_=outline; }
    if(colorOutline_!=outline) throw std::invalid_argument("Finish the current outline color gesture first.");
}
void Editor::PreviewOutlineColor(unsigned channel,double value)
{
    if(channel>=3 || !std::isfinite(value) || value<0 || value>1) throw std::invalid_argument("Outline RGB values must be in [0,1].");
    BeginOutlineColor(); track.track.outlines.at(static_cast<size_t>(colorOutline_)).colorSrgb[channel]=value;
}
bool Editor::EndOutlineColor(bool cancel)
{
    if(!colorStart_) return false;
    const bool changed=!cancel && track.track.outlines.at(static_cast<size_t>(colorOutline_)).colorSrgb!=colorStart_->track.outlines.at(static_cast<size_t>(colorOutline_)).colorSrgb;
    if(cancel) track=std::move(*colorStart_);
    else if(changed) { if(trackUndo_.size()>=128) trackUndo_.erase(trackUndo_.begin()); trackUndo_.push_back({std::move(*colorStart_),true}); trackRedo_.clear(); }
    colorStart_.reset(); colorOutline_=-1; return changed;
}
bool Editor::SetOutlineDegree(double value)
{
    if(colorStart_) throw std::invalid_argument("Finish or cancel the outline color gesture before changing degree.");
    if(mode!=EditorMode::TrackBuilder || outline<0 || static_cast<size_t>(outline)>=track.track.outlines.size()) throw std::invalid_argument("Select a track outline to edit its degree.");
    const auto& original=track.track.outlines[static_cast<size_t>(outline)];
    if(!std::isfinite(value) || value!=std::floor(value) || value<1 || value>Racing2D::MaximumNurbsDegree || value>=static_cast<double>(original.controls.size())) throw std::invalid_argument("Degree must be an integer from 1 to 3 and below the control-point count.");
    const auto degree=static_cast<unsigned>(value); if(degree==original.degree) return false;
    auto next=original; next.degree=degree; Racing2D::MakeUniformKnots(next);
    (void)Racing2D::Tessellate(next); // Validate the periodic seam before touching authored state/history.
    TrackEdit([&] { track.track.outlines[static_cast<size_t>(outline)]=std::move(next); });
    return true;
}
void Editor::Mode(EditorMode next)
{
    if(mode==next) return; EndPaint(); EndOutlineColor();
    if(next==EditorMode::Drive)
    {
        if(!draft.empty() || gateStart) throw std::invalid_argument("Finish or cancel the current outline/gate first.");
        if(!generated) throw std::invalid_argument("Build track surfaces before Drive.");
        ValidateTrackProject(track,false); Racing2D::ValidateRace(track.track,*generated,VehicleRadius(track));
        race.Begin(track.track,*generated,VehicleRadius(track));
    }
    else race.End();
    mode=next;
}
void Editor::BuildTrack() { auto next=Racing2D::Generate(track.track); ValidateTrackProject(track,false); generated=std::move(next); }
void Editor::NewTrack(bool example)
{
    TrackEdit([&] { track=TrackProject{}; if(!example) { track.track=Racing2D::Track{}; track.track.outlines.clear(); track.track.hasFinish=track.track.hasSpawn=false; track.track.checkpoints.clear(); } });
    outline=0; point=0; draft.clear(); gateStart.reset(); tool=Tool::Select;
    if(example) BuildTrack(); Frame();
}
void Editor::FinishOutline()
{
    if(draft.size()<4) throw std::invalid_argument("A periodic cubic outline needs at least four points.");
    Racing2D::NurbsOutline curve; curve.controls=draft; curve.name=track.track.outlines.empty() ? "Ground" : track.track.outlines.size()==1 ? "Road" : "Island";
    curve.colorSrgb=track.track.outlines.size()==1 ? std::array<double,3>{.28,.28,.30} : std::array<double,3>{.35,.45,.25};
    Racing2D::MakeUniformKnots(curve); Racing2D::ValidateCurve(curve);
    TrackEdit([&] { track.track.outlines.push_back(curve); }); draft.clear(); outline=static_cast<int>(track.track.outlines.size()-1); point=0; tool=Tool::Select;
}
void Editor::Place(Racing2D::Point p)
{
    if(tool==Tool::Outline) { if(draft.size()>=64) throw std::invalid_argument("Outline draft budget reached."); draft.push_back({p,1}); return; }
    if(tool==Tool::Spawn) TrackEdit([&] { track.track.spawn.position=p; track.track.hasSpawn=true; });
    else if(tool==Tool::Decoration) TrackEdit([&] { track.decorations.push_back({{p,0},0,1}); decoration=static_cast<int>(track.decorations.size()-1); });
    else if(tool==Tool::Finish || tool==Tool::Checkpoint)
    {
        if(!gateStart) { gateStart=p; return; }
        const Racing2D::Gate gate{*gateStart,p};
        if((gate.b-gate.a).Length()<.01) throw std::invalid_argument("Gate endpoints must differ.");
        TrackEdit([&] { if(tool==Tool::Finish) { track.track.finish=gate; track.track.hasFinish=true; } else track.track.checkpoints.push_back(gate); }); gateStart.reset();
    }
}
void Editor::UseAsset(bool vehicle) { TrackEdit([&] { if(vehicle) track.vehicle=model.scene; else track.decoration=model.scene; }); }
void Editor::Frame(bool selected)
{
    if(selected && (mode!=EditorMode::ModelBuilder || model.selection.object<0 || static_cast<size_t>(model.selection.object)>=model.scene.objects.size())) throw std::invalid_argument("Select a model part to frame.");
    if(mode!=EditorMode::ModelBuilder) { trackCamera.target={}; trackCamera.height=100; trackCamera.pitch=1.1f; trackCamera.yaw=0; return; }
    std::vector<PaintVertex> vertices; std::vector<uint32_t> indices;
    if(selected) { Scene part; part.objects={Object()}; BuildSurface(part,vertices,indices); }
    else BuildSurface(model.scene,vertices,indices);
    Vector3 minimum{1e20f,1e20f,1e20f},maximum{-1e20f,-1e20f,-1e20f};
    for(const auto& v:vertices) { minimum={std::min(minimum.x,v.positionX),std::min(minimum.y,v.positionY),std::min(minimum.z,v.positionZ)}; maximum={std::max(maximum.x,v.positionX),std::max(maximum.y,v.positionY),std::max(maximum.z,v.positionZ)}; }
    modelCamera.target=(minimum+maximum)*.5f; modelCamera.height=std::max(1.f,(maximum-minimum).Length()*1.25f);
}
