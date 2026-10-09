#include "TrackProject.h"
#include "SurfaceMaterials.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
int main(int count,char** arguments)
{
    try
    {
        if(count!=2) return 1; const auto directory=std::filesystem::path(arguments[1]); std::filesystem::create_directories(directory);
        TrackProject project; project.track.outlines[0].controls[0].weight=.8; project.track.outlines[0].colorSrgb={.2,.4,.6}; project.deadzoneX=.07; project.decorations.push_back({{{0,0},.2},3,1.2});
        const auto saved=SerializeTrack(project); SaveTrack(project,directory/"Persistence.track"); const auto loaded=LoadTrack(directory/"Persistence.track");
        Require(SerializeTrack(loaded)==saved,"Weighted controls/knots, race placements, assets, colors, decorative height and input settings must persist exactly.");
        const auto generated=Racing2D::Generate(loaded.track); Racing2D::ValidateRace(loaded.track,generated,VehicleRadius(loaded));
        auto scene=MakeTrackScene(loaded,&generated); Require(scene.objects[0].materialKind==MaterialKind::UnlitGround && scene.objects[0].unlitColorSrgb==std::array<double,3>{.2,.4,.6},"Semantic ground must use explicit unlit material/color.");
        Require(CompileSurfaceMaterial(scene.objects[0])[3]==1 && CompileSurfaceMaterial(ModelObject{})[3]==0,"Ordinary decorative/modeler planes must not be inferred as ground.");
        SaveScene(scene,directory/"Ground.modeler"); const auto ground=LoadScene(directory/"Ground.modeler"); Require(SerializeScene(ground)==SerializeScene(scene),"Explicit material type/color must round trip through the shared scene format.");
        ExportMesh(ground,directory/"Ground.generated.h");
        const auto radius=VehicleRadius(project); project.decorations[0].height=50; Require(VehicleRadius(project)==radius && Racing2D::Driveable(generated,project.track.spawn.position,radius),"Decorative height must not enter the 2D racing footprint or queries.");
        Racing2D::Session race; race.Begin(loaded.track,generated,radius); race.Step({{1,0},0,false},.01); race.End(); Require(SerializeTrack(loaded)==saved,"Driving must not mutate authored track/assets.");
        auto bad=project; bad.deadzoneX=.95; bool rejected=false; try { SerializeTrack(bad); } catch(const std::exception&) { rejected=true; } Require(rejected,"Invalid input settings must be rejected.");
        std::cout<<"PASS: track persistence, embedded shared assets, knots/weights, placements/height, explicit unlit ground, material export and non-mutating drive transition.\n"; return 0;
    }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
