#pragma once
#include "Geometry.h"
#include "SimplePaint/Material.h"
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

inline constexpr unsigned MaximumObjects = 32;
inline constexpr unsigned MaximumMaterials = 32;
struct SurfaceCorner { uint32_t vertex; Vector3 normal; };
struct EvaluatedSurface
{
    std::vector<SurfaceCorner> corners;
    std::vector<uint32_t> indices;
};
enum class MaterialKind { SimplePaint, UnlitGround };
struct MaterialSlot
{
    SimplePaint::Parameters paint{};
    MaterialKind materialKind=MaterialKind::SimplePaint;
    std::array<double,3> unlitColorSrgb{.5,.5,.5};
};
// Slot zero is stored directly; later slots have the same material structure.
struct ModelObject : MaterialSlot
{
    std::string name = "Shape";
    ControlMesh cage = MakeBox();
    Vector3 position{}, rotationDegrees{};
    float scale = 1;
    unsigned subdivisionLevel = 3;
    std::vector<MaterialSlot> additionalMaterials;
    // Imported evaluated polygon topology and Blender corner normals/triangles.
    // This is not an original subdivision cage. Only rigid part transforms and
    // paint edits are exposed until a proper evaluated-mesh editing path exists.
    std::optional<EvaluatedSurface> evaluatedSurface;
    std::string sourceObject, sourceMaterial;
};
struct Scene
{
    std::vector<ModelObject> objects{ModelObject{}};
    double sliderDragSpeed=1; // Project-wide editor preference, not a shader input.
};
enum class SelectionMode { Object, Vertex, Face };
struct Selection
{
    SelectionMode mode = SelectionMode::Vertex;
    int object = 0, face = -1;
    std::vector<uint32_t> vertices;
    uint32_t material=0;
};
inline size_t MaterialCount(const ModelObject& object) { return 1+object.additionalMaterials.size(); }
inline MaterialSlot& ObjectMaterial(ModelObject& object,size_t slot)
{
    return slot==0 ? static_cast<MaterialSlot&>(object) : object.additionalMaterials.at(slot-1);
}
inline const MaterialSlot& ObjectMaterial(const ModelObject& object,size_t slot)
{
    return slot==0 ? static_cast<const MaterialSlot&>(object) : object.additionalMaterials.at(slot-1);
}
size_t MaterialCount(const Scene& scene);
struct MaterialAddress { size_t object; uint32_t slot; };
MaterialAddress LocateMaterial(const Scene& scene,uint32_t material);
inline size_t MaterialObject(const Scene& scene,uint32_t material) { return LocateMaterial(scene,material).object; }
uint32_t AddMaterialSlot(Scene& scene,size_t object,size_t copySlot);
void AssignFaceMaterial(Scene& scene,size_t object,size_t face,uint32_t slot);
Vector3 WorldPosition(const ModelObject& object, Vector3 position);
Vector3 WorldDirection(const ModelObject& object, Vector3 direction);
void ValidateScene(const Scene& scene);
void BuildSurface(const Scene& scene, std::vector<PaintVertex>& vertices, std::vector<uint32_t>& indices);
void SaveScene(const Scene& scene, const std::filesystem::path& path);
std::string SerializeScene(const Scene& scene);
Scene DeserializeScene(std::string_view text);
Scene LoadScene(const std::filesystem::path& path);
Scene MakeSlopeCarScene();
void ExportMesh(const Scene& scene, const std::filesystem::path& path);
