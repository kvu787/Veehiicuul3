#include "Geometry.h"
#include <algorithm>
#include <map>
#include <numbers>
#include <set>

namespace
{
using EdgeKey = std::pair<uint32_t,uint32_t>;
EdgeKey Key(uint32_t a, uint32_t b) { return std::minmax(a,b); }
constexpr size_t MaximumFaces = 131072;
ControlMesh Refine(const ControlMesh& mesh)
{
    const auto edges = ValidateControlMesh(mesh);
    size_t newFaces = 0;
    for (const auto& face : mesh.faces) newFaces += face.size();
    if (newFaces > MaximumFaces) throw std::invalid_argument("Subdivision exceeds the 131072-face preview budget. Reduce the level or cage size.");
    std::vector<Vector3> centers(mesh.faces.size());
    std::vector<std::vector<uint32_t>> incidentFaces(mesh.positions.size()), incidentEdges(mesh.positions.size());
    for (uint32_t f = 0; f < mesh.faces.size(); ++f)
    {
        for (auto v : mesh.faces[f]) { centers[f] += mesh.positions[v]; incidentFaces[v].push_back(f); }
        centers[f] = centers[f]/static_cast<float>(mesh.faces[f].size());
    }
    for (uint32_t e = 0; e < edges.size(); ++e)
    {
        incidentEdges[edges[e].a].push_back(e);
        incidentEdges[edges[e].b].push_back(e);
    }
    ControlMesh result;
    result.positions.resize(mesh.positions.size());
    for (uint32_t v = 0; v < mesh.positions.size(); ++v)
    {
        Vector3 boundary{}; unsigned boundaryCount = 0;
        Vector3 midpointSum{}, faceSum{};
        for (auto e : incidentEdges[v])
        {
            const auto& edge = edges[e];
            midpointSum += (mesh.positions[edge.a]+mesh.positions[edge.b])*.5f;
            if (edge.faces.size() == 1) { boundary += mesh.positions[edge.a == v ? edge.b : edge.a]; ++boundaryCount; }
        }
        if (boundaryCount) result.positions[v] = mesh.positions[v]*.75f + boundary*.125f;
        else
        {
            for (auto f : incidentFaces[v]) faceSum += centers[f];
            const auto n = static_cast<float>(incidentFaces[v].size());
            result.positions[v] = (faceSum/n + midpointSum*(2/n) + mesh.positions[v]*(n-3))/n;
        }
    }
    std::map<EdgeKey,uint32_t> edgePoints;
    for (const auto& edge : edges)
    {
        Vector3 point = (mesh.positions[edge.a]+mesh.positions[edge.b])*.5f;
        if (edge.faces.size() == 2) point = (mesh.positions[edge.a]+mesh.positions[edge.b]+centers[edge.faces[0]]+centers[edge.faces[1]])*.25f;
        edgePoints[Key(edge.a,edge.b)] = static_cast<uint32_t>(result.positions.size());
        result.positions.push_back(point);
    }
    const uint32_t faceStart = static_cast<uint32_t>(result.positions.size());
    result.positions.insert(result.positions.end(), centers.begin(), centers.end());
    result.faces.reserve(newFaces);
    for (uint32_t f = 0; f < mesh.faces.size(); ++f)
    {
        const auto& face = mesh.faces[f];
        for (size_t corner = 0; corner < face.size(); ++corner)
        {
            result.faces.push_back({face[corner], edgePoints.at(Key(face[corner],face[(corner+1)%face.size()])),
                faceStart+f, edgePoints.at(Key(face[(corner+face.size()-1)%face.size()],face[corner]))});
            result.faceMaterials.push_back(FaceMaterial(mesh,f));
        }
    }
    return result;
}
}

std::vector<MeshEdge> ValidateControlMesh(const ControlMesh& mesh)
{
    if(!mesh.faceMaterials.empty() && mesh.faceMaterials.size()!=mesh.faces.size())
        throw std::invalid_argument("Face material assignments must match the face count.");
    if (mesh.positions.empty() || mesh.positions.size() > MaximumFaces*2 || mesh.faces.empty() || mesh.faces.size() > MaximumFaces)
        throw std::invalid_argument("The mesh must contain vertices and faces within the preview budget.");
    for (auto p : mesh.positions)
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || std::max({std::abs(p.x),std::abs(p.y),std::abs(p.z)}) > 100000)
            throw std::invalid_argument("Mesh coordinates must be finite and within +/-100000 units.");
    std::map<EdgeKey,MeshEdge> lookup;
    std::map<EdgeKey,uint32_t> orientation;
    std::vector<std::vector<uint32_t>> incident(mesh.positions.size());
    for (uint32_t f = 0; f < mesh.faces.size(); ++f)
    {
        const auto& face = mesh.faces[f];
        if (face.size() < 3 || face.size() > 256 || std::set<uint32_t>(face.begin(),face.end()).size() != face.size())
            throw std::invalid_argument("Faces need 3 to 256 distinct vertices.");
        for (size_t corner = 0; corner < face.size(); ++corner)
        {
            uint32_t a = face[corner], b = face[(corner+1)%face.size()];
            if (a >= mesh.positions.size() || b >= mesh.positions.size()) throw std::invalid_argument("Face contains an invalid vertex index.");
            auto key = Key(a,b);
            auto& edge = lookup[key];
            edge.a = key.first; edge.b = key.second;
            if (edge.faces.size() == 2) throw std::invalid_argument("An edge cannot belong to more than two faces.");
            if (!edge.faces.empty() && orientation[key] == a) throw std::invalid_argument("Neighboring faces must have consistent winding.");
            orientation[key] = a;
            edge.faces.push_back(f);
            incident[a].push_back(f);
        }
        (void)FaceNormal(mesh,f);
    }
    std::vector<MeshEdge> edges;
    std::vector<std::vector<size_t>> vertexEdges(mesh.positions.size());
    for (const auto& [key,edge] : lookup)
    {
        vertexEdges[key.first].push_back(edges.size()); vertexEdges[key.second].push_back(edges.size());
        edges.push_back(edge);
    }
    for (size_t v = 0; v < incident.size(); ++v)
    {
        if (incident[v].empty()) throw std::invalid_argument("Loose vertices are unsupported in a surface cage.");
        unsigned boundary = 0;
        std::set<uint32_t> visited{incident[v][0]};
        std::vector<uint32_t> pending{incident[v][0]};
        while (!pending.empty())
        {
            const auto f = pending.back(); pending.pop_back();
            for (auto e : vertexEdges[v])
            {
                const auto& faces = edges[e].faces;
                if (std::find(faces.begin(),faces.end(),f) == faces.end()) continue;
                for (auto neighbor : faces) if (visited.insert(neighbor).second) pending.push_back(neighbor);
            }
        }
        for (auto e : vertexEdges[v]) if (edges[e].faces.size() == 1) ++boundary;
        if (visited.size() != incident[v].size() || (boundary && boundary != 2))
            throw std::invalid_argument("Each vertex must have one connected face fan and at most one surface boundary.");
    }
    return edges;
}

ControlMesh MakeBox()
{
    return {{{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}},
        {{0,3,2,1},{4,5,6,7},{0,1,5,4},{3,7,6,2},{0,4,7,3},{1,2,6,5}}};
}
ControlMesh MakePlane() { return {{{-1,0,-1},{-1,0,1},{1,0,1},{1,0,-1}},{{0,1,2,3}}}; }
ControlMesh MakeCylinder()
{
    ControlMesh mesh;
    constexpr uint32_t sides = 12;
    for (uint32_t ring = 0; ring < 2; ++ring)
        for (uint32_t v = 0; v < sides; ++v)
        {
            const float angle = 2*std::numbers::pi_v<float>*float(v)/float(sides);
            mesh.positions.push_back({std::cos(angle),float(ring)*2-1,std::sin(angle)});
        }
    Face bottom, top;
    for (uint32_t v = 0; v < sides; ++v)
    {
        bottom.push_back(v); top.push_back(sides+sides-1-v);
        uint32_t next = (v+1)%sides;
        mesh.faces.push_back({v,sides+v,sides+next,next});
    }
    mesh.faces.push_back(bottom); mesh.faces.push_back(top);
    return mesh;
}
ControlMesh Subdivide(const ControlMesh& mesh, unsigned levels)
{
    if (levels > 4) throw std::invalid_argument("Subdivision level must be between 0 and 4.");
    (void)ValidateControlMesh(mesh);
    auto result = mesh;
    for (unsigned level = 0; level < levels; ++level) result = Refine(result);
    return result;
}
Vector3 FaceNormal(const ControlMesh& mesh, size_t faceIndex)
{
    const auto& face = mesh.faces.at(faceIndex);
    Vector3 normal{};
    for (size_t i = 0; i < face.size(); ++i)
    {
        const auto a = mesh.positions.at(face[i]), b = mesh.positions.at(face[(i+1)%face.size()]);
        normal.x += (a.y-b.y)*(a.z+b.z); normal.y += (a.z-b.z)*(a.x+b.x); normal.z += (a.x-b.x)*(a.y+b.y);
    }
    return Normalize(normal);
}
void ExtrudeFace(ControlMesh& mesh, size_t faceIndex, float distance)
{
    if (!std::isfinite(distance) || std::abs(distance) < 1e-5f) throw std::invalid_argument("Extrusion distance must be finite and nonzero.");
    auto candidate = mesh;
    if(candidate.faceMaterials.empty()) candidate.faceMaterials.resize(candidate.faces.size(),0);
    const auto face = mesh.faces.at(faceIndex);
    const auto normal = FaceNormal(mesh,faceIndex);
    Face cap;
    for (auto v : face) { cap.push_back(static_cast<uint32_t>(candidate.positions.size())); candidate.positions.push_back(mesh.positions[v]+normal*distance); }
    candidate.faces[faceIndex] = cap;
    for (size_t i = 0; i < face.size(); ++i)
    {
        size_t next = (i+1)%face.size();
        candidate.faces.push_back({face[i],face[next],cap[next],cap[i]});
        candidate.faceMaterials.push_back(FaceMaterial(mesh,faceIndex));
    }
    (void)ValidateControlMesh(candidate);
    mesh = std::move(candidate);
}
void AppendSurface(const ControlMesh& mesh, uint32_t material, bool smooth,
                   std::vector<PaintVertex>& vertices, std::vector<uint32_t>& indices)
{
    std::vector<Vector3> normals(mesh.positions.size());
    for (size_t f = 0; f < mesh.faces.size(); ++f)
    {
        const auto& face = mesh.faces[f];
        const auto normal = FaceNormal(mesh,f);
        for (auto v : face) normals[v] += normal;
    }
    if (smooth) for (auto& normal : normals) normal = Normalize(normal);
    for (size_t f = 0; f < mesh.faces.size(); ++f)
    {
        const auto& face = mesh.faces[f];
        const auto flatNormal = FaceNormal(mesh,f);
        const uint32_t first = static_cast<uint32_t>(vertices.size());
        for (auto v : face)
        {
            const auto p = mesh.positions[v], n = smooth ? normals[v] : flatNormal;
            vertices.push_back({p.x,p.y,p.z,n.x,n.y,n.z,material+FaceMaterial(mesh,f)});
        }
        for (uint32_t corner = 1; corner+1 < face.size(); ++corner)
        {
            indices.push_back(first); indices.push_back(first+corner); indices.push_back(first+corner+1);
        }
    }
}
