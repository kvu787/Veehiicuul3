#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// This module has no renderer, window, mesh, or spatial 3D dependencies.
namespace Racing2D
{
inline constexpr unsigned MaximumNurbsDegree=3;
inline constexpr size_t MaximumKnotCount=71,MaximumKnotTextLength=2048;
struct Point
{
    double x=0,y=0;
    Point operator+(Point b) const { return {x+b.x,y+b.y}; }
    Point operator-(Point b) const { return {x-b.x,y-b.y}; }
    Point operator*(double s) const { return {x*s,y*s}; }
    double Length() const { return std::hypot(x,y); }
    bool operator==(const Point&) const = default;
};
inline double Dot(Point a,Point b) { return a.x*b.x+a.y*b.y; }
inline double Cross(Point a,Point b) { return a.x*b.y-a.y*b.x; }
struct WeightedPoint { Point position; double weight=1; };
struct NurbsOutline
{
    std::string name="Outline";
    unsigned degree=3;
    std::vector<WeightedPoint> controls;
    std::vector<double> knots; // Explicit periodic knot vector, not a polyline.
    std::array<double,3> colorSrgb{.35,.45,.25};
};
struct Gate { Point a,b; }; // Directed: progress crosses from negative to positive side.
struct GatePlacement { Point center; double headingDegrees=0,width=1; }; // Heading is the left-facing crossing normal.
GatePlacement DescribeGate(Gate gate);
Gate PlaceGate(GatePlacement placement);
struct Pose { Point position; double heading=0; }; // CCW radians from +X.
struct Triangle { Point a,b,c; };
struct Track
{
    std::string name="My track";
    std::vector<NurbsOutline> outlines; // Ground, outer road, inner islands.
    Pose spawn{{0,-17},0};
    bool hasSpawn=false,hasFinish=false;
    Gate finish{{-4,-22},{-4,-12}};
    std::vector<Gate> checkpoints; // In explicitly authored order.
};
struct GeneratedTrack
{
    std::vector<std::vector<Point>> boundaries;
    std::vector<std::vector<Triangle>> surfaces;
};
void MakeUniformKnots(NurbsOutline& curve);
void ValidateCurve(const NurbsOutline& curve);
std::vector<double> ParseKnots(std::string_view text);
std::string FormatKnots(const std::vector<double>& knots);
void ValidatePeriodicKnots(const NurbsOutline& curve);
Point Evaluate(const NurbsOutline& curve,double parameter);
std::vector<Point> Tessellate(const NurbsOutline& curve,unsigned samplesPerSpan=12);
bool Contains(const std::vector<Point>& polygon,Point point);
GeneratedTrack Generate(const Track& track);
Track MakeExampleTrack();
bool Driveable(const GeneratedTrack& track,Point position,double radius);
void ValidateRace(const Track& track,const GeneratedTrack& generated,double radius);
bool Crosses(Gate gate,Point previous,Point current);
struct Commands
{
    Point acceleration; // Track-space continuous input; controller API independent.
    double brake=0;
    bool reset=false;
};
struct CarState
{
    Point position,velocity;
    double heading=0,angularVelocity=0;
    unsigned nextCheckpoint=0,laps=0,collisions=0;
    bool lapArmed=false;
};
void AdvanceProgress(const Track& track,Point previous,Point current,CarState& state);
class Session
{
public:
    void Begin(const Track& track,const GeneratedTrack& generated,double collisionRadius);
    void Step(const Commands& input,double seconds);
    const CarState& State() const { return state_; }
    bool Active() const { return active_; }
    void End() { active_=false; }
    void Reset();
private:
    const Track* track_=nullptr;
    const GeneratedTrack* generated_=nullptr;
    CarState state_;
    double radius_=1;
    bool active_=false;
};
}
