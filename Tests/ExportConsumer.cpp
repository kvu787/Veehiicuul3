#include "Export.generated.h"
#include <iterator>
#include <string_view>
static_assert(sizeof(GeneratedModelMesh::Vertex)==28);
static_assert(GeneratedModelMesh::MaterialCount==1);
static_assert(std::size(GeneratedModelMesh::Indices)==3840);
static_assert(std::string_view(GeneratedModelMesh::Materials[0].name)=="Body \"Blue\" \\ \xc3\xa9 \xf0\x9f\x9a\x97");
int main() { return 0; }
