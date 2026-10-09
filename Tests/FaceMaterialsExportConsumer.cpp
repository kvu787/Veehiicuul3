#include "FaceMaterials.generated.h"
#include <iterator>
static_assert(GeneratedModelMesh::MaterialCount==2);
static_assert(GeneratedModelMesh::Materials[1].baseColor[0]==.8);
static_assert(GeneratedModelMesh::Materials[1].baseColor[1]==.1);
static_assert(GeneratedModelMesh::Materials[1].baseColor[2]==.2);
static_assert(std::size(GeneratedModelMesh::Vertices)==1536);
constexpr size_t PaintedVertices()
{
    size_t count=0; for(const auto& v:GeneratedModelMesh::Vertices) if(v.materialIndex==1) ++count; return count;
}
static_assert(PaintedVertices()==256);
int main() { return 0; }
