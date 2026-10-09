#pragma once
#include "Scene.h"
#include "TrackProject.h"
#include "EditorMode.h"
#include "PaintControls.h"
#include "Camera.h"
#include <functional>

struct ModelState { Scene scene=MakeSlopeCarScene(); Selection selection{SelectionMode::Object,0,-1,{},0}; };
class Editor
{
public:
    ModelState model;
    TrackProject track;
    EditorMode mode=EditorMode::ModelBuilder;
    Camera modelCamera,trackCamera;
    std::optional<Racing2D::GeneratedTrack> generated;
    Racing2D::Session race;
    std::vector<Racing2D::WeightedPoint> draft;
    std::optional<Racing2D::Point> gateStart;
    int outline=1,point=0,decoration=0;
    enum class Tool { Select,Outline,Spawn,Finish,Checkpoint,Decoration } tool=Tool::Select;
    bool cage=true;
    float step=.1f;
    ModelObject& Object();
    MaterialSlot& Material();
    const MaterialSlot& Material() const;
    bool Editable() const;
    std::vector<uint32_t> SelectedVertices() const;
    Vector3 Center() const;
    void ModelEdit(const std::function<void()>& operation);
    void TrackEdit(const std::function<void()>& operation);
    bool Undo(bool redo); // True when rendering geometry must be rebuilt; color history returns false.
    bool CanUndo(bool redo) const;
    void AddShape(int kind);
    void Refine();
    void Extrude();
    void Transform(Vector3 position,Vector3 rotation,float scale);
    void Nudge(Vector3 delta);
    void CopyMaterial();
    void AssignMaterial();
    void BeginPaint();
    void PreviewPaint(PaintParameter parameter,double value);
    bool EndPaint(bool cancel=false);
    void BeginOutlineColor();
    void PreviewOutlineColor(unsigned channel,double value);
    bool EndOutlineColor(bool cancel=false);
    bool SetOutlineDegree(double value);
    void Mode(EditorMode next);
    void BuildTrack();
    void NewTrack(bool example);
    void FinishOutline();
    void Place(Racing2D::Point value);
    void UseAsset(bool vehicle);
    void Frame(bool selected=false);
private:
    std::vector<ModelState> modelUndo_,modelRedo_;
    struct TrackHistory { TrackProject project; bool materialOnly=false; };
    std::vector<TrackHistory> trackUndo_,trackRedo_;
    std::optional<ModelState> paintStart_;
    std::optional<TrackProject> colorStart_;
    int colorOutline_=-1;
};
