#include "Scene.h"
#include <windows.h>
#include <bcrypt.h>
#include <cstring>
#include <iostream>
#include <sstream>
#include <iomanip>

namespace
{
void Require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
bool SamePaint(const SimplePaint::Parameters& a,const SimplePaint::Parameters& b)
{
    return a.baseColorSrgb==b.baseColorSrgb && a.brightness==b.brightness && a.shift==b.shift &&
        a.rotationDegrees==b.rotationDegrees && a.darkPoint==b.darkPoint && a.lightPoint==b.lightPoint;
}
std::string RuntimeFingerprint(const Scene& scene)
{
    std::vector<PaintVertex> vertices; std::vector<uint32_t> indices;
    BuildSurface(scene,vertices,indices);
    std::vector<unsigned char> bytes(vertices.size()*sizeof(PaintVertex)+indices.size()*sizeof(uint16_t));
    std::memcpy(bytes.data(),vertices.data(),vertices.size()*sizeof(PaintVertex));
    for(size_t i=0;i<indices.size();++i)
    {
        Require(indices[i]<=UINT16_MAX,"Runtime index format.");
        const auto index=static_cast<uint16_t>(indices[i]);
        std::memcpy(bytes.data()+vertices.size()*sizeof(PaintVertex)+i*sizeof(index),&index,sizeof(index));
    }
    unsigned char digest[32]{};
    Require(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,bytes.data(),static_cast<ULONG>(bytes.size()),digest,sizeof(digest))>=0,"SHA256 failed.");
    std::ostringstream text;
    for(auto byte : digest) text << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(byte);
    return text.str();
}
}
int main(int argc,char** argv)
{
    try
    {
        const auto original=MakeSlopeCarScene();
        const char* names[]={"SlopeCarAxles","SlopeCarBodyBlue","SlopeCarCabinBlue","SlopeCarHeadlights","SlopeCarWheels"};
        Require(original.objects.size()==5,"Default scene must be the actual five-region car.");
        size_t polygonCount=0,positionCount=0;
        for(size_t i=0;i<5;++i)
        {
            const auto& object=original.objects[i];
            Require(object.name==names[i] && object.sourceMaterial==names[i] && object.sourceObject=="Car","Preserve source object/material identities.");
            Require(object.evaluatedSurface.has_value() && object.subdivisionLevel==0,"Do not invent an original cage.");
            polygonCount+=object.cage.faces.size(); positionCount+=object.cage.positions.size();
            Require(object.paint.brightness==.5 && object.paint.shift==0 && object.paint.rotationDegrees==0 && object.paint.darkPoint==0 && object.paint.lightPoint==.8,"Runtime SimplePaint defaults.");
        }
        Require(positionCount==962 && polygonCount==867,"Preserve original evaluated polygon topology.");
        // This hash is from the read-only runtime GeneratedCarMesh header, not
        // regenerated from this editor's expected output.
        Require(RuntimeFingerprint(original)=="187701d56bd6e77ccd27990d79bd7b0061ab17de5a13e1b451b32e7a09b5df9e","Default car must match runtime positions, corner normals, triangles and material indices exactly.");
        const std::array<double,3> colors[]={{.678429127,.678431321,.678431321},{.0009765625,.436627067,.9990234375},
            {.506386429,.756053146,.9990234375},{.9990234375,.815686771,.0009765625},{.345097446,.345097446,.345097446}};
        auto edited=original;
        for(size_t part=0;part<5;++part)
        {
            Require(original.objects[part].paint.baseColorSrgb==colors[part],"Copy actual runtime color values.");
            auto& paint=edited.objects[part].paint;
            paint.baseColorSrgb={.2+part*.1,.3,.4}; paint.brightness=.3+part*.1;
            paint.shift=.1+part*.1; paint.rotationDegrees=23+part*31.; paint.darkPoint=.1; paint.lightPoint=.75;
            ValidateScene(edited);
            Require(RuntimeFingerprint(edited)==RuntimeFingerprint(original),"Paint editing must preserve exact geometry and material assignments.");
            for(size_t other=part+1;other<5;++other) Require(SamePaint(edited.objects[other].paint,original.objects[other].paint),"Independent material isolation.");
        }
        const auto directory=argc>1 ? std::filesystem::path(argv[1]) : std::filesystem::temp_directory_path()/"ModelerSlopeCarTests";
        std::filesystem::create_directories(directory);
        SaveScene(edited,directory/"SlopeCarRoundTrip.modeler");
        const auto loaded=LoadScene(directory/"SlopeCarRoundTrip.modeler");
        Require(RuntimeFingerprint(loaded)==RuntimeFingerprint(original),"Persist exact corner normals and triangulation.");
        for(size_t i=0;i<5;++i)
        {
            Require(SamePaint(loaded.objects[i].paint,edited.objects[i].paint),"Persist every independently edited SimplePaint parameter.");
            Require(loaded.objects[i].cage.faces==original.objects[i].cage.faces,"Persist source polygons.");
        }
        auto transformed=loaded; transformed.objects[1].position={3,2,1}; transformed.objects[1].rotationDegrees={0,90,0}; transformed.objects[1].scale=.7f;
        std::vector<PaintVertex> vertices; std::vector<uint32_t> indices; BuildSurface(transformed,vertices,indices);
        for(const auto& vertex : vertices)
            Require(std::abs(Vector3{vertex.normalX,vertex.normalY,vertex.normalZ}.Length()-1)<1e-4f,"Transform imported normals correctly.");
        auto invalid=original; invalid.objects[0].subdivisionLevel=1;
        bool rejected=false; try { ValidateScene(invalid); } catch(const std::exception&) { rejected=true; }
        Require(rejected,"Imported evaluated mesh cannot be silently subdivided.");
        invalid=original; invalid.objects[0].evaluatedSurface->indices[0]=UINT32_MAX;
        rejected=false; try { ValidateScene(invalid); } catch(const std::exception&) { rejected=true; }
        Require(rejected,"Validate imported corner indices.");
        ExportMesh(loaded,directory/"SlopeCarExport.generated.h");
        ExportMesh(original,directory/"SlopeCarSourceExport.generated.h");
        std::cout << "PASS: exact runtime car SHA256, source polygons/names, runtime paints, per-part isolation, persistence, transformed normals and evaluated-mesh validation.\n";
        return 0;
    }
    catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
