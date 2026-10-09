#include "TrackProject.h"
#include <nlohmann/json.hpp>
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <limits>
#include <numbers>
#include <random>

namespace
{
using Json=nlohmann::json;
Json PointJson(Racing2D::Point p) { return Json::array({p.x,p.y}); }
Racing2D::Point ReadPoint(const Json& value)
{
    if(!value.is_array() || value.size()!=2) throw std::invalid_argument("Expected exactly two planar coordinates.");
    return {value[0].get<double>(),value[1].get<double>()};
}
Json GateJson(Racing2D::Gate gate) { return Json::array({PointJson(gate.a),PointJson(gate.b)}); }
Racing2D::Gate ReadGate(const Json& value) { if(!value.is_array() || value.size()!=2) throw std::invalid_argument("Expected two gate endpoints."); return {ReadPoint(value[0]),ReadPoint(value[1])}; }
struct Bounds { Vector3 minimum{1e9f,1e9f,1e9f},maximum{-1e9f,-1e9f,-1e9f}; };
Bounds AssetBounds(const Scene& scene)
{
    std::vector<PaintVertex> vertices; std::vector<uint32_t> indices; BuildSurface(scene,vertices,indices);
    if(vertices.empty()) throw std::invalid_argument("A vehicle/decorative asset needs geometry.");
    Bounds bounds;
    for(const auto& v:vertices)
    {
        bounds.minimum={std::min(bounds.minimum.x,v.positionX),std::min(bounds.minimum.y,v.positionY),std::min(bounds.minimum.z,v.positionZ)};
        bounds.maximum={std::max(bounds.maximum.x,v.positionX),std::max(bounds.maximum.y,v.positionY),std::max(bounds.maximum.z,v.positionZ)};
    }
    return bounds;
}
Scene NormalizeAsset(const Scene& scene,double scale)
{
    const auto bounds=AssetBounds(scene);
    const Vector3 center{(bounds.minimum.x+bounds.maximum.x)*.5f,bounds.minimum.y,(bounds.minimum.z+bounds.maximum.z)*.5f};
    auto result=scene;
    for(auto& object:result.objects) { object.position=(object.position-center)*static_cast<float>(scale); object.scale*=static_cast<float>(scale); }
    return result;
}
void AddTriangle(ControlMesh& mesh,Racing2D::Triangle triangle)
{
    const auto first=static_cast<uint32_t>(mesh.positions.size());
    // Blender XY -> right-handed Y-up (X,0,-Y); CCW remains upward.
    for(auto p:{triangle.a,triangle.b,triangle.c}) mesh.positions.push_back({static_cast<float>(p.x),0,static_cast<float>(-p.y)});
    mesh.faces.push_back({first,first+1,first+2});
}
void AddCheckered(Scene& scene,Racing2D::Gate gate)
{
    const auto edge=gate.b-gate.a; const auto width=edge.Length();
    const auto across=edge*(1/width),along=Racing2D::Point{-across.y,across.x};
    for(unsigned color=0;color<2;++color)
    {
        ModelObject object; object.name=color ? "Checkered white" : "Checkered black";
        object.cage={}; object.subdivisionLevel=0; object.materialKind=MaterialKind::UnlitGround;
        object.unlitColorSrgb=color ? std::array<double,3>{1,1,1} : std::array<double,3>{0,0,0};
        for(unsigned x=0;x<12;++x) for(unsigned y=0;y<2;++y) if((x+y)%2==color)
        {
            const auto a=gate.a+across*(width*x/12)+along*((static_cast<double>(y)-1)*.6);
            const auto b=a+across*(width/12),c=b+along*.6,d=a+along*.6;
            AddTriangle(object.cage,{a,b,c}); AddTriangle(object.cage,{a,c,d});
        }
        for(auto& p:object.cage.positions) p.y=.015f;
        scene.objects.push_back(std::move(object));
    }
}
}
void ValidateTrackProject(const TrackProject& project,bool build)
{
    const auto& track=project.track;
    if(track.name.empty() || track.name.size()>128 || track.outlines.size()>6 || track.checkpoints.size()>32 || project.decorations.size()>16) throw std::invalid_argument("Track names/outline/checkpoint/decoration counts exceed the slice budget.");
    for(const auto& outline:track.outlines) Racing2D::ValidateCurve(outline);
    auto finitePoint=[](Racing2D::Point point) { return std::isfinite(point.x) && std::isfinite(point.y) && std::max(std::abs(point.x),std::abs(point.y))<=10000; };
    if(!finitePoint(track.spawn.position) || !std::isfinite(track.spawn.heading) || std::abs(track.spawn.heading)>10000) throw std::invalid_argument("Invalid planar spawn pose.");
    for(auto gate:track.checkpoints) if(!finitePoint(gate.a) || !finitePoint(gate.b) || (gate.b-gate.a).Length()<.1) throw std::invalid_argument("Invalid planar checkpoint endpoints.");
    if(track.hasFinish && (!finitePoint(track.finish.a) || !finitePoint(track.finish.b) || (track.finish.b-track.finish.a).Length()<.1)) throw std::invalid_argument("Invalid checkered line endpoints.");
    if(!std::isfinite(project.vehicleScale) || project.vehicleScale<.05 || project.vehicleScale>10) throw std::invalid_argument("Vehicle scale must be in [0.05,10].");
    for(auto zone:{project.deadzoneX,project.deadzoneY}) if(!std::isfinite(zone) || zone<0 || zone>=.95) throw std::invalid_argument("Inner deadzones must be in [0,0.95).");
    ValidateScene(project.vehicle); ValidateScene(project.decoration);
    if(project.vehicle.objects.empty() || project.decoration.objects.empty() || project.vehicle.objects.size()+project.decoration.objects.size()*project.decorations.size()+track.outlines.size()+2>MaximumObjects) throw std::invalid_argument("Shared vehicle/decor assets exceed the total 32-part scene budget.");
    if(MaterialCount(project.vehicle)+MaterialCount(project.decoration)*project.decorations.size()+track.outlines.size()+2>MaximumMaterials) throw std::invalid_argument("Shared vehicle/decor material slots exceed the total 32-material scene budget.");
    for(const auto& decoration:project.decorations) if(!finitePoint(decoration.pose.position) || !std::isfinite(decoration.pose.heading) || !std::isfinite(decoration.height) || std::abs(decoration.height)>100 || !std::isfinite(decoration.scale) || decoration.scale<.05 || decoration.scale>10) throw std::invalid_argument("Invalid decorative pose, visual height or scale.");
    if(build) (void)Racing2D::Generate(track);
}
std::string SerializeTrack(const TrackProject& project)
{
    ValidateTrackProject(project,false);
    const auto& track=project.track;
    Json root{{"Version",2},{"Coordinates","BlenderXYMeters"},{"Name",track.name},{"Outlines",Json::array()},
        {"HasSpawn",track.hasSpawn},{"Spawn",{{"Position",PointJson(track.spawn.position)},{"Heading",track.spawn.heading}}},
        {"HasFinish",track.hasFinish},{"Finish",GateJson(track.finish)},{"Checkpoints",Json::array()},
        {"VehicleScale",project.vehicleScale},{"DeadzoneX",project.deadzoneX},{"DeadzoneY",project.deadzoneY},
        {"VehicleAsset",Json::parse(SerializeScene(project.vehicle))},{"DecorationAsset",Json::parse(SerializeScene(project.decoration))},{"Decorations",Json::array()}};
    for(const auto& outline:track.outlines)
    {
        Json points=Json::array(); for(auto p:outline.controls) points.push_back({p.position.x,p.position.y,p.weight});
        root["Outlines"].push_back({{"Name",outline.name},{"Degree",outline.degree},{"Controls",points},{"Knots",outline.knots},{"ColorSrgb",outline.colorSrgb}});
    }
    for(auto gate:track.checkpoints) root["Checkpoints"].push_back(GateJson(gate));
    for(const auto& decoration:project.decorations) root["Decorations"].push_back({{"Position",PointJson(decoration.pose.position)},{"Heading",decoration.pose.heading},{"Height",decoration.height},{"Scale",decoration.scale}});
    return root.dump(2)+"\n";
}
TrackProject DeserializeTrack(std::string_view text)
{
    if(text.size()>32*1024*1024) throw std::invalid_argument("Tracks must be smaller than 32 MiB.");
    const auto root=Json::parse(text);
    if(root.at("Version")!=2 || root.at("Coordinates")!="BlenderXYMeters") throw std::invalid_argument("Unsupported track format.");
    TrackProject result; auto& track=result.track; track.outlines.clear(); track.checkpoints.clear();
    track.name=root.at("Name").get<std::string>();
    const auto& outlines=root.at("Outlines");
    if(!outlines.is_array() || outlines.size()>6) throw std::invalid_argument("Invalid outline list.");
    for(const auto& record:outlines)
    {
        Racing2D::NurbsOutline outline; outline.name=record.at("Name").get<std::string>(); outline.degree=record.at("Degree").get<unsigned>();
        const auto& controls=record.at("Controls"); const auto& knots=record.at("Knots");
        if(!controls.is_array() || controls.size()>64 || !knots.is_array() || knots.size()>71) throw std::invalid_argument("Invalid NURBS array budget.");
        for(const auto& point:controls) { if(!point.is_array() || point.size()!=3) throw std::invalid_argument("Invalid weighted planar control."); outline.controls.push_back({{point[0].get<double>(),point[1].get<double>()},point[2].get<double>()}); }
        outline.knots=knots.get<std::vector<double>>(); outline.colorSrgb=record.at("ColorSrgb").get<std::array<double,3>>(); track.outlines.push_back(std::move(outline));
    }
    track.hasSpawn=root.at("HasSpawn").get<bool>(); track.spawn={ReadPoint(root.at("Spawn").at("Position")),root.at("Spawn").at("Heading").get<double>()};
    track.hasFinish=root.at("HasFinish").get<bool>(); track.finish=ReadGate(root.at("Finish"));
    const auto& checkpoints=root.at("Checkpoints"); if(!checkpoints.is_array() || checkpoints.size()>32) throw std::invalid_argument("Invalid checkpoint list.");
    for(const auto& gate:checkpoints) track.checkpoints.push_back(ReadGate(gate));
    result.vehicleScale=root.at("VehicleScale").get<double>(); result.deadzoneX=root.at("DeadzoneX").get<double>(); result.deadzoneY=root.at("DeadzoneY").get<double>();
    result.vehicle=DeserializeScene(root.at("VehicleAsset").dump()); result.decoration=DeserializeScene(root.at("DecorationAsset").dump());
    const auto& decorations=root.at("Decorations"); if(!decorations.is_array() || decorations.size()>16) throw std::invalid_argument("Invalid decoration list.");
    for(const auto& item:decorations) result.decorations.push_back({{ReadPoint(item.at("Position")),item.at("Heading").get<double>()},item.at("Height").get<double>(),item.at("Scale").get<double>()});
    ValidateTrackProject(result,false); return result;
}
void SaveTrack(const TrackProject& project,const std::filesystem::path& path)
{
    const auto contents=SerializeTrack(project); auto temporary=path; temporary+=L".saving-"+std::to_wstring(std::random_device{}());
    try { std::ofstream stream(temporary,std::ios::binary); stream<<contents; stream.close(); if(!stream || !MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Could not save track; previous file is preserved."); }
    catch(...) { std::error_code error; std::filesystem::remove(temporary,error); throw; }
}
TrackProject LoadTrack(const std::filesystem::path& path)
{
    if(std::filesystem::file_size(path)>32*1024*1024) throw std::invalid_argument("Track file exceeds 32 MiB.");
    std::ifstream stream(path,std::ios::binary); return DeserializeTrack(std::string(std::istreambuf_iterator<char>(stream),{}));
}
double VehicleRadius(const TrackProject& project)
{
    const auto bounds=AssetBounds(project.vehicle);
    return std::max(.05,std::hypot(bounds.maximum.x-bounds.minimum.x,bounds.maximum.z-bounds.minimum.z)*.5*project.vehicleScale);
}
Scene NormalizedVehicle(const TrackProject& project) { return NormalizeAsset(project.vehicle,project.vehicleScale); }
Scene MakeTrackScene(const TrackProject& project,const Racing2D::GeneratedTrack* generated)
{
    Scene result; result.objects.clear();
    if(generated) for(size_t i=0;i<generated->surfaces.size();++i)
    {
        ModelObject object; object.name=project.track.outlines[i].name; object.cage={}; object.subdivisionLevel=0;
        object.materialKind=MaterialKind::UnlitGround; object.unlitColorSrgb=project.track.outlines[i].colorSrgb;
        for(const auto& triangle:generated->surfaces[i]) AddTriangle(object.cage,triangle);
        result.objects.push_back(std::move(object));
    }
    if(project.track.hasFinish) AddCheckered(result,project.track.finish);
    for(const auto& decoration:project.decorations)
    {
        auto asset=NormalizeAsset(project.decoration,decoration.scale);
        ModelObject pose; pose.rotationDegrees.y=static_cast<float>(decoration.pose.heading*180/std::numbers::pi);
        pose.position={static_cast<float>(decoration.pose.position.x),static_cast<float>(decoration.height),static_cast<float>(-decoration.pose.position.y)};
        for(auto& object:asset.objects) { object.position=WorldPosition(pose,object.position); object.rotationDegrees.y+=pose.rotationDegrees.y; result.objects.push_back(std::move(object)); }
    }
    return result;
}
