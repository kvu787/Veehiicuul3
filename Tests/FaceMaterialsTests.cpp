#include "Scene.h"
#include "TrackProject.h"
#include "SurfaceMaterials.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>

namespace
{
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
template<class F> void Reject(F operation) { bool rejected=false; try { operation(); } catch(const std::exception&) { rejected=true; } Require(rejected,"Invalid material data was accepted."); }
std::string Read(const std::filesystem::path& path) { std::ifstream file(path,std::ios::binary); return {std::istreambuf_iterator<char>(file),{}}; }
void SameGeometry(const Scene& a,const Scene& b)
{
    std::vector<PaintVertex> av,bv; std::vector<uint32_t> ai,bi; BuildSurface(a,av,ai); BuildSurface(b,bv,bi);
    Require(ai==bi && av.size()==bv.size(),"Material assignments must not change triangulation or vertex counts.");
    for(size_t i=0;i<av.size();++i) Require(std::memcmp(&av[i],&bv[i],6*sizeof(float))==0,"Material assignments must not change positions or normals.");
}
}
int main(int count,char** arguments)
{
    try
    {
        Require(count==2,"Expected fixture output directory."); const std::filesystem::path output=arguments[1]; std::filesystem::create_directories(output);
        Scene scene; scene.objects[0].subdivisionLevel=0;
        Require(AddMaterialSlot(scene,0,0)==1,"New slot must duplicate selected paint and keep slot zero.");
        ObjectMaterial(scene.objects[0],1).paint.baseColorSrgb={.8,.1,.2};
        AssignFaceMaterial(scene,0,1,1);
        auto plain=scene; plain.objects[0].cage.faceMaterials.clear();
        size_t descendants=1;
        for(unsigned level=0;level<=4;++level)
        {
            scene.objects[0].subdivisionLevel=plain.objects[0].subdivisionLevel=level; SameGeometry(scene,plain);
            const auto mesh=Subdivide(scene.objects[0].cage,level);
            Require(mesh.faces.size()==6*descendants && mesh.faceMaterials.size()==mesh.faces.size(),"Subdivision must propagate explicit face material entries.");
            for(size_t face=0;face<mesh.faces.size();++face)
                Require(FaceMaterial(mesh,face)==(face>=descendants && face<2*descendants ? 1u : 0u),"Each child quad must inherit its original face's slot.");
            descendants*=4;
        }
        scene.objects[0].subdivisionLevel=0;
        const auto authored=SerializeScene(scene);
        auto refined=scene; refined.objects[0].cage=Subdivide(refined.objects[0].cage,1); refined.objects[0].subdivisionLevel=3;
        auto preview=scene; preview.objects[0].subdivisionLevel=4; SameGeometry(refined,preview);
        const auto originalAssignments=scene.objects[0].cage.faceMaterials;
        auto extruded=scene; ExtrudeFace(extruded.objects[0].cage,1,.4f);
        for(size_t face=0;face<6;++face) Require(FaceMaterial(extruded.objects[0].cage,face)==originalAssignments[face],"Extrusion must preserve unrelated face assignments and the selected cap.");
        for(size_t face=6;face<10;++face) Require(FaceMaterial(extruded.objects[0].cage,face)==1,"Extruded side faces must inherit selected face's slot.");
        SaveScene(scene,output/"TwoPaintCube.modeler"); Require(SerializeScene(LoadScene(output/"TwoPaintCube.modeler"))==authored,"Slots and assignments must persist exactly.");
        auto history=scene; AssignFaceMaterial(history,0,2,1); const auto changed=SerializeScene(history); history=DeserializeScene(authored);
        Require(SerializeScene(history)==authored && SerializeScene(DeserializeScene(changed))==changed,"Scene snapshots used by undo/redo/cancel must preserve all assignments.");
        auto invalid=scene; invalid.objects[0].cage.faceMaterials[0]=2;
        std::vector<PaintVertex> vertices{{1,2,3,0,1,0,0}}; std::vector<uint32_t> indices{17};
        Reject([&] { BuildSurface(invalid,vertices,indices); }); Require(vertices.size()==1 && vertices[0].positionX==1 && indices==std::vector<uint32_t>{17},"Rejected surface must leave caller output intact.");
        Reject([&] { SaveScene(invalid,output/"TwoPaintCube.modeler"); }); Require(Read(output/"TwoPaintCube.modeler")==authored,"Rejected save must preserve existing project bytes.");
        Reject([&] { AssignFaceMaterial(scene,0,9,0); }); Reject([&] { AssignFaceMaterial(scene,0,0,2); });
        Require(SerializeScene(scene)==authored,"Rejected assignment must be atomic.");
        auto budget=scene; while(MaterialCount(budget)<MaximumMaterials) AddMaterialSlot(budget,0,0);
        const auto atLimit=SerializeScene(budget); Reject([&] { AddMaterialSlot(budget,0,0); }); Require(SerializeScene(budget)==atLimit,"Adding a 33rd material must preserve the entire scene.");
        auto document=nlohmann::json::parse(authored);
        for(const auto field:{"Materials","FaceMaterials"}) { auto bad=document; bad["Objects"][0].erase(field); Reject([&] { DeserializeScene(bad.dump()); }); }
        auto bad=document; bad["Version"]=1; Reject([&] { DeserializeScene(bad.dump()); });
        bad=document; bad["Objects"][0]["FaceMaterials"][0]=-1; Reject([&] { DeserializeScene(bad.dump()); });
        bad=document; bad["Objects"][0]["FaceMaterials"].erase(0); Reject([&] { DeserializeScene(bad.dump()); });
        bad=document; bad["Objects"][0]["FaceMaterials"][0]=UINT64_MAX; Reject([&] { DeserializeScene(bad.dump()); });
        auto mapped=scene; mapped.objects.push_back(ModelObject{}); mapped.objects.back().position.x=5;
        Require(MaterialCount(mapped)==3 && LocateMaterial(mapped,1).object==0 && LocateMaterial(mapped,1).slot==1 && LocateMaterial(mapped,2).object==1 && LocateMaterial(mapped,2).slot==0,"Flattened material index must map back to object and local slot for picking.");
        Reject([&] { LocateMaterial(mapped,3); });
        std::array<SimplePaint::GpuMaterial,MaximumMaterials> gpu{}; std::array<std::array<float,4>,MaximumMaterials> surfaces{};
        CompileSceneMaterials(mapped,gpu,surfaces);
        const auto paint=SimplePaint::Material::Compile(ObjectMaterial(mapped.objects[0],1).paint).Constants();
        Require(std::memcmp(&gpu[1],&paint,sizeof(paint))==0 && surfaces[0][3]==0,"Upload order must match mesh material indices and preserve SimplePaint semantics.");
        TrackProject project; project.vehicle=scene; project.decoration=scene; project.decorations.push_back({{{0,0},.3},4,1});
        const auto trackText=SerializeTrack(project); SaveTrack(project,output/"TwoPaint.track"); const auto loaded=LoadTrack(output/"TwoPaint.track");
        Require(SerializeTrack(loaded)==trackText && SerializeScene(loaded.vehicle)==authored && SerializeScene(loaded.decoration)==authored,"Embedded vehicle/decor assets must retain slots and assignments exactly.");
        const auto generated=Racing2D::Generate(loaded.track); auto presentation=MakeTrackScene(loaded,&generated);
        Require(presentation.objects[0].materialKind==MaterialKind::UnlitGround && CompileSurfaceMaterial(presentation.objects[0])[3]==1,"Track ground must remain explicitly unlit.");
        const auto vehicle=NormalizedVehicle(loaded); presentation.objects.insert(presentation.objects.end(),vehicle.objects.begin(),vehicle.objects.end());
        ValidateScene(presentation); BuildSurface(presentation,vertices,indices); CompileSceneMaterials(presentation,gpu,surfaces);
        Require(MaterialCount(presentation)==9,"Track ground/checkered/decor/vehicle materials must flatten consistently.");
        for(const auto& v:vertices) Require(v.materialIndex<9,"Track/vehicle vertices must reference the combined material layout.");
        Racing2D::Session race; race.Begin(loaded.track,generated,VehicleRadius(loaded)); race.Step({{.2,0},0,false},1./120); race.End(); Require(SerializeTrack(loaded)==trackText,"Driving must not mutate authored face assignments or assets.");
        auto tooMany=project; tooMany.decoration=budget; Reject([&] { ValidateTrackProject(tooMany,false); }); Require(SerializeTrack(project)==trackText,"Rejected track composition must not mutate the authored project.");
        auto source=MakeSlopeCarScene(); const auto sourceText=SerializeScene(source); Reject([&] { AddMaterialSlot(source,0,0); }); Reject([&] { AssignFaceMaterial(source,0,0,0); }); Require(SerializeScene(source)==sourceText,"Imported source parts must retain exact source material assignments.");
        scene.objects[0].subdivisionLevel=3; ExportMesh(scene,output/"FaceMaterials.generated.h");
        std::cout<<"PASS: two-paint cube levels 0-4 unchanged geometry, inheritance/refine/extrude, strong rejection/save guarantees, current-format round trips, GPU/picking remap, embedded track/non-mutating drive and source-car protection.\n"; return 0;
    }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
