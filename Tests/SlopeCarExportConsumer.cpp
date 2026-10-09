#include "SlopeCarSourceExport.generated.h"
#include <iterator>
#include <string_view>
static_assert(sizeof(GeneratedModelMesh::Vertex)==28);
static_assert(GeneratedModelMesh::MaterialCount==5);
static_assert(std::size(GeneratedModelMesh::Vertices)==1938);
static_assert(std::size(GeneratedModelMesh::Indices)==5652);
static_assert(std::string_view(GeneratedModelMesh::Materials[1].name)=="SlopeCarBodyBlue");
static_assert(GeneratedModelMesh::Materials[1].baseColor[0]==1.0/1024);
static_assert(GeneratedModelMesh::Materials[1].baseColor[2]==1.0-1.0/1024);
static_assert(GeneratedModelMesh::Materials[3].baseColor[0]==1.0-1.0/1024);
static_assert(GeneratedModelMesh::Materials[3].baseColor[2]==1.0/1024);
static_assert(GeneratedModelMesh::Materials[2].baseColor[1]==.756053146);
static_assert(GeneratedModelMesh::Materials[4].lightPoint==.8);
int main() { return 0; }
