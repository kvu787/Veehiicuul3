#pragma once
#include <cstdint>
#include <vector>
#include <cmath>
#include <stdexcept>

struct Vector3
{
    float x = 0, y = 0, z = 0;
    Vector3 operator+(Vector3 b) const { return {x+b.x,y+b.y,z+b.z}; }
    Vector3 operator-(Vector3 b) const { return {x-b.x,y-b.y,z-b.z}; }
    Vector3 operator*(float s) const { return {x*s,y*s,z*s}; }
    Vector3 operator/(float s) const { return *this * (1/s); }
    Vector3& operator+=(Vector3 b) { *this = *this+b; return *this; }
    float Length() const { return std::sqrt(x*x+y*y+z*z); }
};
inline float Dot(Vector3 a, Vector3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vector3 Cross(Vector3 a, Vector3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline Vector3 Normalize(Vector3 value)
{
    float length = value.Length();
    if (length < 1e-8f || !std::isfinite(length)) throw std::invalid_argument("The edit creates a degenerate face or normal.");
    return value/length;
}
using Face = std::vector<uint32_t>;
struct ControlMesh
{
    std::vector<Vector3> positions;
    std::vector<Face> faces;
    // Empty is the compact all-slot-zero representation for generated cages.
    // Saved projects always write an explicit entry for every face.
    std::vector<uint32_t> faceMaterials;
};
inline uint32_t FaceMaterial(const ControlMesh& mesh,size_t face)
{
    if(face>=mesh.faces.size()) throw std::out_of_range("Invalid material face.");
    return mesh.faceMaterials.empty() ? 0 : mesh.faceMaterials.at(face);
}
struct MeshEdge { uint32_t a, b; std::vector<uint32_t> faces; };
struct PaintVertex
{
    float positionX, positionY, positionZ;
    float normalX, normalY, normalZ;
    uint32_t materialIndex = 0;
};
struct OverlayVertex { float x, y, z, r, g, b, a; };
struct ViewGeometry
{
    std::vector<PaintVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<OverlayVertex> grid, cage, markers;
    uint32_t vehicleFirstIndex=0,vehicleIndexCount=0;
    uint64_t revision = 1;
};

std::vector<MeshEdge> ValidateControlMesh(const ControlMesh& mesh);
ControlMesh MakeBox();
ControlMesh MakePlane();
ControlMesh MakeCylinder();
ControlMesh Subdivide(const ControlMesh& mesh, unsigned levels);
Vector3 FaceNormal(const ControlMesh& mesh, size_t face);
void ExtrudeFace(ControlMesh& mesh, size_t face, float distance);
void AppendSurface(const ControlMesh& mesh, uint32_t material, bool smooth,
                   std::vector<PaintVertex>& vertices, std::vector<uint32_t>& indices);
