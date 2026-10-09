#pragma once
#include "Scene.h"
#include <span>
inline std::array<float,4> CompileSurfaceMaterial(const MaterialSlot& object)
{
    std::array<float,4> result{};
    if(object.materialKind==MaterialKind::UnlitGround)
    {
        for(unsigned i=0;i<3;++i) { const auto c=object.unlitColorSrgb[i]; result[i]=static_cast<float>(c<=.04045 ? c/12.92 : std::pow((c+.055)/1.055,2.4)); }
        result[3]=1;
    }
    return result;
}
inline void CompileSceneMaterials(const Scene& scene,std::span<SimplePaint::GpuMaterial> paints,std::span<std::array<float,4>> surfaces)
{
    if(MaterialCount(scene)>MaximumMaterials || paints.size()<MaterialCount(scene) || surfaces.size()<MaterialCount(scene)) throw std::invalid_argument("Material upload storage is too small.");
    size_t index=0;
    for(const auto& object:scene.objects) for(size_t slot=0;slot<MaterialCount(object);++slot)
    {
        const auto& material=ObjectMaterial(object,slot);
        paints[index]=SimplePaint::Material::Compile(material.paint).Constants();
        surfaces[index++]=CompileSurfaceMaterial(material);
    }
}
