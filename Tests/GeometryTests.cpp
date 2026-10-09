#include "Geometry.h"
#include "Scene.h"
#include "Camera.h"
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
void Require(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
void Near(float value,float expected) { if(std::abs(value-expected)>std::max(1e-5f,std::abs(expected)*2e-6f)) throw std::runtime_error("Expected "+std::to_string(expected)+", received "+std::to_string(value)); }
template<class F> void Reject(F operation) { bool rejected=false; try { operation(); } catch(const std::exception&) { rejected=true; } Require(rejected,"Invalid data was accepted."); }
}
int main(int argc,char** argv)
{
    try
    {
        auto cube = MakeBox();
        Require(ValidateControlMesh(cube).size()==12,"Cube edge topology.");
        auto level1 = Subdivide(cube,1);
        Require(level1.positions.size()==26 && level1.faces.size()==24,"Catmull-Clark cube counts.");
        Near(level1.positions[0].x,-5.0f/9); Near(level1.positions[0].y,-5.0f/9); Near(level1.positions[0].z,-5.0f/9);
        auto level3 = Subdivide(cube,3);
        Require(level3.faces.size()==384,"Preview face growth.");
        auto plane = Subdivide(MakePlane(),1);
        Near(plane.positions[0].x,-.75f); Near(plane.positions[0].z,-.75f);
        Require(plane.positions.size()==9 && plane.faces.size()==4,"Open boundary refinement.");
        (void)ValidateControlMesh(MakeCylinder());
        ExtrudeFace(cube,3,.4f);
        Require(cube.positions.size()==12 && cube.faces.size()==10,"Extrusion must preserve an editable manifold cage.");
        Require(Subdivide(cube,2).faces.size()==160,"Extruded cage preview.");
        auto bad=MakeBox(); bad.faces.push_back(bad.faces[0]); Reject([&]{(void)Subdivide(bad,1);});
        bad=MakeBox(); std::reverse(bad.faces[0].begin(),bad.faces[0].end()); Reject([&]{(void)ValidateControlMesh(bad);});
        bad=MakePlane(); bad.positions.push_back({0,0,0}); Reject([&]{(void)ValidateControlMesh(bad);});
        bad=MakeBox(); bad.positions[0].x=std::numeric_limits<float>::quiet_NaN(); Reject([&]{(void)ValidateControlMesh(bad);});
        Reject([&]{(void)Subdivide(MakeBox(),5);});
        Scene scene; scene.objects[0].cage=cube; scene.objects[0].name="Body \"Blue\" \\ \xc3\xa9 \xf0\x9f\x9a\x97";
        scene.objects[0].position={2,3,4}; scene.objects[0].rotationDegrees={0,90,0}; scene.objects[0].scale=2;
        const auto direction = WorldDirection(scene.objects[0],{0,0,1}); Near(direction.x,1); Near(direction.z,0);
        std::vector<PaintVertex> vertices; std::vector<uint32_t> indices; BuildSurface(scene,vertices,indices);
        Require(!vertices.empty() && indices.size()==3840,"Evaluated scene draw data.");
        const auto directory = argc>1 ? std::filesystem::path(argv[1]) : std::filesystem::temp_directory_path();
        std::filesystem::create_directories(directory);
        auto dump=[&](const ControlMesh& mesh,const char* name)
        {
            std::ofstream output(directory/name); output.precision(9);
            for(auto p : mesh.positions) output << p.x << ',' << p.y << ',' << p.z << '\n';
        };
        dump(level1,"CubeLevel1.csv"); dump(plane,"PlaneLevel1.csv");
        const auto project=directory/"RoundTrip.modeler";
        SaveScene(scene,project);
        const auto loaded=LoadScene(project);
        Require(loaded.objects[0].name==scene.objects[0].name && loaded.objects[0].cage.faces==scene.objects[0].cage.faces,"Save/load topology and names.");
        Near(loaded.objects[0].cage.positions[8].y,1.4f);
        ExportMesh(loaded,directory/"Export.generated.h");
        std::ofstream(directory/"Invalid.modeler") << "{\"Version\":1}";
        Reject([&]{(void)LoadScene(directory/"Invalid.modeler");});
        Camera camera; auto projected=camera.Screen(camera.target,800,600); Near(projected.x,400); Near(projected.y,300);
        const auto planePoint = camera.PlanePoint(517,251,800,600); projected=camera.Screen(planePoint,800,600); Near(projected.x,517); Near(projected.y,251);
        std::cout << "Passed analytic Catmull-Clark, open-boundary, extrusion, topology validation, transforms, project round-trip, export and picking projection tests.\n";
        return 0;
    }
    catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
