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
    Camera modelCamera,trackCamera,driveCamera;
    bool following=false; // View preference, never project data/history.
    Camera& ViewCamera() { return mode==EditorMode::ModelBuilder ? modelCamera : mode==EditorMode::Drive ? driveCamera : trackCamera; }
    const Camera& ViewCamera() const { return mode==EditorMode::ModelBuilder ? modelCamera : mode==EditorMode::Drive ? driveCamera : trackCamera; }
    bool SetFollowing(bool value);
    void UpdateDriveCamera();
    std::optional<Racing2D::GeneratedTrack> generated;
    Racing2D::Session race;
    std::vector<Racing2D::WeightedPoint> draft;
    std::optional<Racing2D::Point> gateStart;
    int outline=1,point=0,decoration=0,gate=0; // Gate 0 is finish; 1..count are checkpoints in authored order.
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
    void TrackEdit(const std::function<void()>& operation,bool preservesGeometry=false,bool preservesBoundaries=false);
    bool Undo(bool redo); // True when rendering geometry must be rebuilt; colors/exact same-boundary history return false.
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
    enum class KnotEdit { Unchanged,SameBoundary,ChangedBoundary };
    KnotEdit SetOutlineKnots(std::string_view text);
    bool GateSelected() const;
    bool CanDeletePoint() const;
    void DeletePoint();
    bool CanDeleteGate() const;
    void DeleteGate();
    Racing2D::Gate SelectedGate() const;
    bool SetGatePlacement(Racing2D::GatePlacement placement);
    bool CanDrive() const;
    void Mode(EditorMode next);
    void BuildTrack();
    void NewTrack(bool example);
    void FinishOutline();
    void Place(Racing2D::Point value);
    void UseAsset(bool vehicle);
    void Frame(bool selected=false);
private:
    std::vector<ModelState> modelUndo_,modelRedo_;
    struct TrackSelection { int outline,point,decoration,gate; };
    TrackSelection TrackSelectionNow() const { return {outline,point,decoration,gate}; }
    void RestoreTrackSelection(TrackSelection value);
    void ClampTrackSelection();
    struct TrackHistory { TrackProject project; bool preservesGeometry=false; TrackSelection selection; };
    std::vector<TrackHistory> trackUndo_,trackRedo_;
    std::optional<ModelState> paintStart_;
    std::optional<TrackProject> colorStart_;
    TrackSelection colorSelection_{0,0,-1,-1};
    int colorOutline_=-1;
};
