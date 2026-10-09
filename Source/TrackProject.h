#pragma once
#include "Racing2D.h"
#include "Scene.h"
struct Decoration
{
    Racing2D::Pose pose;
    double height=0,scale=1;
};
struct TrackProject
{
    Racing2D::Track track=Racing2D::MakeExampleTrack();
    Scene vehicle=MakeSlopeCarScene(),decoration;
    double vehicleScale=.6;
    double deadzoneX=.10,deadzoneY=.05;
    std::vector<Decoration> decorations;
};
void ValidateTrackProject(const TrackProject& project,bool build);
std::string SerializeTrack(const TrackProject& project);
TrackProject DeserializeTrack(std::string_view text);
void SaveTrack(const TrackProject& project,const std::filesystem::path& path);
TrackProject LoadTrack(const std::filesystem::path& path);
// Presentation boundary: derive a conservative planar footprint once from
// evaluated asset bounds. Racing only receives the resulting 2D radius.
double VehicleRadius(const TrackProject& project);
Scene MakeTrackScene(const TrackProject& project,const Racing2D::GeneratedTrack* generated);
Scene NormalizedVehicle(const TrackProject& project);
