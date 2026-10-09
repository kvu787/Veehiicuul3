#include "Scene.h"
#include "PaintControls.h"
#include "SimplePaint/Geometry.h"
#include "Assets/SlopeCar.generated.h"
#include <nlohmann/json.hpp>
#include <DirectXMath.h>
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <locale>
#include <random>
#include <sstream>

namespace
{
using Json = nlohmann::json;
DirectX::XMMATRIX Rotation(const ModelObject& object)
{
    const auto r = object.rotationDegrees;
    return DirectX::XMMatrixRotationRollPitchYaw(DirectX::XMConvertToRadians(r.x),DirectX::XMConvertToRadians(r.y),DirectX::XMConvertToRadians(r.z));
}
Json ToJson(Vector3 value) { return Json::array({value.x,value.y,value.z}); }
Vector3 FromJson(const Json& value)
{
    if (!value.is_array() || value.size() != 3) throw std::invalid_argument("Expected a vector with three components.");
    return {value.at(0).get<float>(),value.at(1).get<float>(),value.at(2).get<float>()};
}
Json PaintJson(const SimplePaint::Parameters& p)
{
    return {{"BaseColor",p.baseColorSrgb},{"Brightness",p.brightness},{"Shift",p.shift},{"RotationDegrees",p.rotationDegrees},{"DarkPoint",p.darkPoint},{"LightPoint",p.lightPoint}};
}
SimplePaint::Parameters PaintFromJson(const Json& p)
{
    SimplePaint::Parameters result;
    result.baseColorSrgb = p.at("BaseColor").get<std::array<double,3>>();
    result.brightness = p.at("Brightness").get<double>(); result.shift = p.at("Shift").get<double>();
    result.rotationDegrees = p.at("RotationDegrees").get<double>(); result.darkPoint = p.at("DarkPoint").get<double>(); result.lightPoint = p.at("LightPoint").get<double>();
    SimplePaint::ValidateParameters(result);
    return result;
}
void WriteAtomic(const std::filesystem::path& path, const std::string& contents)
{
    auto temporary = path;
    temporary += L".saving-" + std::to_wstring(std::random_device{}());
    try
    {
        std::ofstream stream(temporary,std::ios::binary);
        stream.write(contents.data(),static_cast<std::streamsize>(contents.size()));
        stream.close();
        if (!stream) throw std::runtime_error("Could not write the selected file.");
        if (!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Could not finish saving the file. The previous file has been preserved.");
    }
    catch (...) { std::error_code error; std::filesystem::remove(temporary,error); throw; }
}
Json MaterialJson(const MaterialSlot& material)
{
    return {{"SimplePaint",PaintJson(material.paint)},{"MaterialKind",material.materialKind==MaterialKind::UnlitGround ? "UnlitGround" : "SimplePaint"},{"UnlitColorSrgb",material.unlitColorSrgb}};
}
MaterialSlot MaterialFromJson(const Json& data)
{
    MaterialSlot material; material.paint=PaintFromJson(data.at("SimplePaint"));
    const auto kind=data.at("MaterialKind").get<std::string>();
    if(kind!="SimplePaint" && kind!="UnlitGround") throw std::invalid_argument("Unknown saved material kind.");
    material.materialKind=kind=="UnlitGround" ? MaterialKind::UnlitGround : MaterialKind::SimplePaint;
    material.unlitColorSrgb=data.at("UnlitColorSrgb").get<std::array<double,3>>();
    return material;
}
std::string CppString(const std::string& value)
{
    std::ostringstream text;
    text << '"';
    for(unsigned char byte : value)
    {
        if(byte=='"' || byte=='\\') text << '\\' << static_cast<char>(byte);
        else if(byte<32 || byte>=127) text << '\\' << std::oct << std::setw(3) << std::setfill('0') << static_cast<unsigned>(byte) << std::dec;
        else text << static_cast<char>(byte);
    }
    text << '"';
    return text.str();
}
}

Vector3 WorldDirection(const ModelObject& object, Vector3 direction)
{
    DirectX::XMFLOAT3 result;
    DirectX::XMStoreFloat3(&result,DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(direction.x,direction.y,direction.z,0),Rotation(object)));
    return {result.x,result.y,result.z};
}
Vector3 WorldPosition(const ModelObject& object, Vector3 position)
{
    return WorldDirection(object,position*object.scale)+object.position;
}
size_t MaterialCount(const Scene& scene)
{
    size_t count=0; for(const auto& object:scene.objects) count+=MaterialCount(object); return count;
}
MaterialAddress LocateMaterial(const Scene& scene,uint32_t material)
{
    for(size_t object=0;object<scene.objects.size();++object)
    {
        const auto count=MaterialCount(scene.objects[object]);
        if(material<count) return {object,material};
        material-=static_cast<uint32_t>(count);
    }
    throw std::out_of_range("Invalid scene material index.");
}
uint32_t AddMaterialSlot(Scene& scene,size_t objectIndex,size_t copySlot)
{
    auto candidate=scene; auto& object=candidate.objects.at(objectIndex);
    if(object.evaluatedSurface) throw std::invalid_argument("Imported source parts retain one source material; add slots to an editable cage.");
    const auto material=ObjectMaterial(object,copySlot);
    const auto slot=static_cast<uint32_t>(MaterialCount(object));
    object.additionalMaterials.push_back(material); ValidateScene(candidate);
    scene=std::move(candidate); return slot;
}
void AssignFaceMaterial(Scene& scene,size_t objectIndex,size_t face,uint32_t slot)
{
    auto candidate=scene; auto& object=candidate.objects.at(objectIndex);
    if(object.evaluatedSurface) throw std::invalid_argument("Imported source face assignments are preserved.");
    if(face>=object.cage.faces.size() || slot>=MaterialCount(object)) throw std::out_of_range("Invalid face or material slot.");
    if(object.cage.faceMaterials.empty()) object.cage.faceMaterials.resize(object.cage.faces.size(),0);
    object.cage.faceMaterials[face]=slot; ValidateScene(candidate); scene=std::move(candidate);
}
void ValidateScene(const Scene& scene)
{
    ValidateDragSpeed(scene.sliderDragSpeed);
    if (scene.objects.size() > MaximumObjects) throw std::invalid_argument("A project supports at most 32 shapes in this first version.");
    if(MaterialCount(scene)>MaximumMaterials) throw std::invalid_argument("A scene supports at most 32 material slots across all shapes.");
    for (const auto& object : scene.objects)
    {
        if (object.name.empty() || object.name.size() > 128) throw std::invalid_argument("Shape names need 1 to 128 characters.");
        if (!std::isfinite(object.scale) || object.scale < 1.0f/1024 || object.scale > 1024)
            throw std::invalid_argument("Uniform object scale must be in [1/1024,1024].");
        for (auto value : {object.position.x,object.position.y,object.position.z,object.rotationDegrees.x,object.rotationDegrees.y,object.rotationDegrees.z})
            if (!std::isfinite(value) || std::abs(value) > 100000) throw std::invalid_argument("Transforms must be finite and bounded.");
        if (object.subdivisionLevel > 4) throw std::invalid_argument("Subdivision level must be 0 to 4.");
        for(size_t slot=0;slot<MaterialCount(object);++slot)
        {
            const auto& material=ObjectMaterial(object,slot);
            SimplePaint::ValidateParameters(material.paint);
            if(material.materialKind!=MaterialKind::SimplePaint && material.materialKind!=MaterialKind::UnlitGround) throw std::invalid_argument("Unknown material kind.");
            for(auto value:material.unlitColorSrgb) if(!std::isfinite(value) || value<0 || value>1) throw std::invalid_argument("Unlit colors must be finite sRGB values in [0,1].");
        }
        (void)ValidateControlMesh(object.cage);
        for(auto slot:object.cage.faceMaterials) if(slot>=MaterialCount(object)) throw std::invalid_argument("A face references an invalid material slot.");
        if (object.evaluatedSurface)
        {
            if(MaterialCount(object)!=1 || std::any_of(object.cage.faceMaterials.begin(),object.cage.faceMaterials.end(),[](uint32_t slot) { return slot!=0; })) throw std::invalid_argument("Imported evaluated parts retain their exact source material assignments.");
            if (object.subdivisionLevel != 0) throw std::invalid_argument("Imported evaluated parts have no original subdivision cage; preview level must be zero.");
            const auto& surface = *object.evaluatedSurface;
            if(surface.corners.empty() || surface.corners.size()>1000000 || surface.indices.empty() || surface.indices.size()>3000000 || surface.indices.size()%3)
                throw std::invalid_argument("Invalid evaluated surface size.");
            for(const auto& corner : surface.corners)
            {
                if(corner.vertex>=object.cage.positions.size() || !std::isfinite(corner.normal.Length()) || std::abs(corner.normal.Length()-1)>1e-4f)
                    throw std::invalid_argument("Invalid evaluated corner vertex or unit normal.");
            }
            for(auto index : surface.indices) if(index>=surface.corners.size()) throw std::invalid_argument("Invalid evaluated triangle corner index.");
        }
    }
}
void BuildSurface(const Scene& scene, std::vector<PaintVertex>& vertices, std::vector<uint32_t>& indices)
{
    ValidateScene(scene);
    std::vector<PaintVertex> builtVertices; std::vector<uint32_t> builtIndices;
    uint32_t materialBase=0;
    for (uint32_t objectIndex = 0; objectIndex < scene.objects.size(); ++objectIndex)
    {
        const auto& object = scene.objects[objectIndex];
        if (object.evaluatedSurface)
        {
            const auto first=static_cast<uint32_t>(builtVertices.size());
            for(const auto& corner : object.evaluatedSurface->corners)
            {
                const auto p=WorldPosition(object,object.cage.positions[corner.vertex]);
                const auto n=WorldDirection(object,corner.normal);
                builtVertices.push_back({p.x,p.y,p.z,n.x,n.y,n.z,materialBase});
            }
            for(auto index : object.evaluatedSurface->indices) builtIndices.push_back(first+index);
        }
        else
        {
            auto evaluated = Subdivide(object.cage,object.subdivisionLevel);
            for (auto& position : evaluated.positions) position = WorldPosition(object,position);
            AppendSurface(evaluated,materialBase,object.subdivisionLevel != 0,builtVertices,builtIndices);
        }
        materialBase+=static_cast<uint32_t>(MaterialCount(object));
        if (builtVertices.size() > 1000000) throw std::invalid_argument("The project exceeds the one-million-vertex viewport budget.");
    }
    if (!builtIndices.empty()) SimplePaint::ValidateMesh<PaintVertex>(builtVertices,builtIndices,materialBase);
    vertices.swap(builtVertices); indices.swap(builtIndices);
}
std::string SerializeScene(const Scene& scene)
{
    ValidateScene(scene);
    Json document{{"Version",2},{"Coordinates","RightHandedYUp"},{"SliderDragSpeed",scene.sliderDragSpeed},{"Objects",Json::array()}};
    for (const auto& object : scene.objects)
    {
        Json positions = Json::array();
        for (auto p : object.cage.positions) positions.push_back(ToJson(p));
        document["Objects"].push_back({{"Name",object.name},{"Position",ToJson(object.position)},{"RotationDegrees",ToJson(object.rotationDegrees)},
            {"Scale",object.scale},{"SubdivisionLevel",object.subdivisionLevel},{"Vertices",positions},{"Faces",object.cage.faces},{"Materials",Json::array()},{"FaceMaterials",Json::array()}});
        auto& record=document["Objects"].back();
        for(size_t slot=0;slot<MaterialCount(object);++slot) record["Materials"].push_back(MaterialJson(ObjectMaterial(object,slot)));
        for(size_t face=0;face<object.cage.faces.size();++face) record["FaceMaterials"].push_back(FaceMaterial(object.cage,face));
        if(object.evaluatedSurface)
        {
            Json corners=Json::array();
            for(const auto& corner : object.evaluatedSurface->corners) corners.push_back(Json::array({corner.vertex,corner.normal.x,corner.normal.y,corner.normal.z}));
            auto& data=document["Objects"].back();
            data["EvaluatedSurface"]={{"Vertices",corners},{"Indices",object.evaluatedSurface->indices}};
            data["SourceObject"]=object.sourceObject; data["SourceMaterial"]=object.sourceMaterial;
        }
    }
    return document.dump(2)+"\n";
}
void SaveScene(const Scene& scene,const std::filesystem::path& path) { WriteAtomic(path,SerializeScene(scene)); }
static Scene SceneFromJson(const Json& document)
{
    if (document.at("Version") != 2 || document.at("Coordinates") != "RightHandedYUp") throw std::invalid_argument("Unsupported project version or coordinates.");
    Scene result; result.objects.clear();
    result.sliderDragSpeed=document.at("SliderDragSpeed").get<double>();
    const auto& objects = document.at("Objects");
    if (!objects.is_array() || objects.size() > MaximumObjects) throw std::invalid_argument("Invalid project object list.");
    for (const auto& data : objects)
    {
        ModelObject object;
        object.name = data.at("Name").get<std::string>();
        object.position = FromJson(data.at("Position")); object.rotationDegrees = FromJson(data.at("RotationDegrees"));
        object.scale = data.at("Scale").get<float>();
        if (!data.at("SubdivisionLevel").is_number_unsigned()) throw std::invalid_argument("Subdivision level must be a nonnegative integer.");
        object.subdivisionLevel = data.at("SubdivisionLevel").get<unsigned>();
        const auto& materials=data.at("Materials");
        if(!materials.is_array() || materials.empty() || materials.size()>MaximumMaterials) throw std::invalid_argument("Invalid saved material slot count.");
        static_cast<MaterialSlot&>(object)=MaterialFromJson(materials[0]);
        for(size_t slot=1;slot<materials.size();++slot) object.additionalMaterials.push_back(MaterialFromJson(materials[slot]));
        object.cage.positions.clear();
        object.cage.faces.clear();
        const auto& positions = data.at("Vertices");
        if (!positions.is_array() || positions.size() > 262144) throw std::invalid_argument("Invalid vertex list.");
        for (const auto& position : positions) object.cage.positions.push_back(FromJson(position));
        const auto& faces = data.at("Faces");
        if (!faces.is_array() || faces.size() > 131072) throw std::invalid_argument("Invalid face list.");
        for (const auto& face : faces)
        {
            if (!face.is_array() || face.size() < 3 || face.size() > 256) throw std::invalid_argument("Invalid face record.");
            Face polygon;
            for (const auto& index : face)
            {
                if (!index.is_number_unsigned() || index.get<uint64_t>() > UINT32_MAX) throw std::invalid_argument("Face indices must be nonnegative 32-bit integers.");
                polygon.push_back(index.get<uint32_t>());
            }
            object.cage.faces.push_back(std::move(polygon));
        }
        const auto& assignments=data.at("FaceMaterials");
        if(!assignments.is_array() || assignments.size()!=object.cage.faces.size()) throw std::invalid_argument("Invalid saved face material assignment count.");
        for(const auto& slot:assignments)
        {
            if(!slot.is_number_unsigned() || slot.get<uint64_t>()>=materials.size()) throw std::invalid_argument("Invalid saved face material slot.");
            object.cage.faceMaterials.push_back(slot.get<uint32_t>());
        }
        if(data.contains("EvaluatedSurface"))
        {
            EvaluatedSurface surface;
            const auto& evaluated=data.at("EvaluatedSurface");
            const auto& corners=evaluated.at("Vertices");
            if(!corners.is_array() || corners.size()>1000000) throw std::invalid_argument("Invalid evaluated vertex list.");
            for(const auto& corner : corners)
            {
                if(!corner.is_array() || corner.size()!=4 || !corner.at(0).is_number_unsigned() || corner.at(0).get<uint64_t>()>UINT32_MAX)
                    throw std::invalid_argument("Invalid evaluated vertex record.");
                surface.corners.push_back({corner.at(0).get<uint32_t>(),{corner.at(1).get<float>(),corner.at(2).get<float>(),corner.at(3).get<float>()}});
            }
            const auto& indices=evaluated.at("Indices");
            if(!indices.is_array() || indices.size()>3000000) throw std::invalid_argument("Invalid evaluated triangle list.");
            for(const auto& index : indices)
            {
                if(!index.is_number_unsigned() || index.get<uint64_t>()>UINT32_MAX) throw std::invalid_argument("Invalid evaluated index.");
                surface.indices.push_back(index.get<uint32_t>());
            }
            object.evaluatedSurface=std::move(surface);
            object.sourceObject=data.at("SourceObject").get<std::string>(); object.sourceMaterial=data.at("SourceMaterial").get<std::string>();
            if(object.sourceObject.size()>128 || object.sourceMaterial.size()>128) throw std::invalid_argument("Source names must be at most 128 bytes.");
        }
        result.objects.push_back(std::move(object));
    }
    ValidateScene(result);
    std::vector<PaintVertex> vertices; std::vector<uint32_t> indices;
    BuildSurface(result,vertices,indices);
    return result;
}
Scene LoadScene(const std::filesystem::path& path)
{
    if (std::filesystem::file_size(path) > 16*1024*1024) throw std::invalid_argument("Project files must be smaller than 16 MiB.");
    std::ifstream stream(path);
    return SceneFromJson(Json::parse(stream));
}
Scene MakeSlopeCarScene() { return SceneFromJson(Json::parse(SlopeCarProject)); }
Scene DeserializeScene(std::string_view text)
{
    if(text.size()>16*1024*1024) throw std::invalid_argument("Asset scenes must be smaller than 16 MiB.");
    return SceneFromJson(Json::parse(text));
}
void ExportMesh(const Scene& scene, const std::filesystem::path& path)
{
    std::vector<PaintVertex> vertices; std::vector<uint32_t> indices;
    BuildSurface(scene,vertices,indices);
    if (vertices.empty()) throw std::invalid_argument("Add a shape before exporting.");
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::setprecision(9) << std::showpoint;
    text << "// Generated by Veehiicuul2. Evaluated surfaces and finite Catmull-Clark previews; right-handed Y-up.\n#pragma once\n#include <cstdint>\nnamespace GeneratedModelMesh {\n";
    text << "struct Vertex { float positionX,positionY,positionZ,normalX,normalY,normalZ; std::uint32_t materialIndex; };\n";
    text << "inline constexpr std::uint32_t MaterialCount = " << MaterialCount(scene) << "u;\n";
    text << "struct PaintInput { const char* name; double baseColor[3], brightness, shift, rotationDegrees, darkPoint, lightPoint; };\n";
    text << "inline constexpr PaintInput Materials[] = {\n";
    text << std::setprecision(17); // Preserve binary64 paint inputs at strict domain boundaries.
    for (const auto& object : scene.objects) for(size_t slot=0;slot<MaterialCount(object);++slot)
    {
        const auto& p = ObjectMaterial(object,slot).paint;
        const auto name=slot==0 ? object.name : object.name+" / Material "+std::to_string(slot+1);
        text << "{" << CppString(name) << ",{" << p.baseColorSrgb[0] << ',' << p.baseColorSrgb[1] << ',' << p.baseColorSrgb[2] << "},"
             << p.brightness << ',' << p.shift << ',' << p.rotationDegrees << ',' << p.darkPoint << ',' << p.lightPoint << "},\n";
    }
    text << "};\nenum class MaterialKind { SimplePaint, UnlitGround };\nstruct SurfaceMaterial { MaterialKind kind; double colorSrgb[3]; };\ninline constexpr SurfaceMaterial SurfaceMaterials[] = {\n";
    for(const auto& object:scene.objects) for(size_t slot=0;slot<MaterialCount(object);++slot)
    {
        const auto& material=ObjectMaterial(object,slot);
        text<<"{MaterialKind::"<<(material.materialKind==MaterialKind::UnlitGround ? "UnlitGround" : "SimplePaint")<<",{"<<material.unlitColorSrgb[0]<<','<<material.unlitColorSrgb[1]<<','<<material.unlitColorSrgb[2]<<"}},\n";
    }
    text << "};\ninline constexpr Vertex Vertices[] = {\n";
    text << std::setprecision(9); // Nine significant digits round-trip binary32 geometry.
    for (const auto& v : vertices)
        text << '{' << v.positionX << "f," << v.positionY << "f," << v.positionZ << "f," << v.normalX << "f," << v.normalY << "f," << v.normalZ << "f," << v.materialIndex << "u},\n";
    text << "};\ninline constexpr std::uint32_t Indices[] = {\n";
    for (size_t i = 0; i < indices.size(); ++i) text << indices[i] << "u," << (i%24 == 23 ? '\n' : ' ');
    text << "\n};\nstatic_assert(sizeof(Vertex) == 28);\n}\n";
    WriteAtomic(path,text.str());
}
