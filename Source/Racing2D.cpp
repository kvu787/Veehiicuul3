#include "Racing2D.h"
#include <algorithm>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <tuple>

namespace Racing2D
{
namespace
{
constexpr double Epsilon=1e-8;
void Require(bool condition,const char* message) { if(!condition) throw std::invalid_argument(message); }
bool Finite(Point p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::abs(p.x)<=10000 && std::abs(p.y)<=10000; }
double SegmentDistance(Point p,Point a,Point b)
{
    const auto d=b-a;
    const auto length=Dot(d,d);
    const auto t=length>0 ? std::clamp(Dot(p-a,d)/length,0.0,1.0) : 0.0;
    return (p-(a+d*t)).Length();
}
bool Intersects(Point a,Point b,Point c,Point d)
{
    if(SegmentDistance(a,c,d)<Epsilon || SegmentDistance(b,c,d)<Epsilon ||
       SegmentDistance(c,a,b)<Epsilon || SegmentDistance(d,a,b)<Epsilon) return true;
    return Cross(b-a,c-a)*Cross(b-a,d-a)<0 && Cross(d-c,a-c)*Cross(d-c,b-c)<0;
}
void ValidateLoop(const std::vector<Point>& loop)
{
    Require(loop.size()>=3,"An outline needs at least three generated vertices.");
    double area=0;
    for(size_t i=0;i<loop.size();++i)
    {
        const auto a=loop[i],b=loop[(i+1)%loop.size()];
        Require((b-a).Length()>Epsilon,"A curve produces a zero-length boundary edge.");
        area+=Cross(a,b);
        for(size_t j=i+1;j<loop.size();++j)
        {
            if(j==i+1 || (i==0 && j==loop.size()-1)) continue;
            Require(!Intersects(a,b,loop[j],loop[(j+1)%loop.size()]),"An outline intersects itself. Move its control points before building.");
        }
    }
    Require(std::abs(area)>1e-5,"An outline needs a nonzero enclosed area.");
}
// Exact planar decomposition of tessellated boundaries into scanline slabs.
// Each contour toggles inside/outside, preserving arbitrary concavity and holes.
std::vector<Triangle> Triangulate(const std::vector<const std::vector<Point>*>& contours)
{
    std::vector<double> rows;
    for(auto loop:contours) for(auto p:*loop) rows.push_back(p.y);
    std::sort(rows.begin(),rows.end());
    rows.erase(std::unique(rows.begin(),rows.end(),[](double a,double b) { return std::abs(a-b)<Epsilon; }),rows.end());
    std::vector<Triangle> triangles;
    struct Edge { Point a,b; double X(double y) const { return a.x+(b.x-a.x)*(y-a.y)/(b.y-a.y); } };
    std::vector<Edge> edges;
    for(size_t row=1;row<rows.size();++row)
    {
        const auto low=rows[row-1],high=rows[row],middle=(low+high)*.5;
        edges.clear();
        for(auto loop:contours) for(size_t i=0;i<loop->size();++i)
        {
            const auto a=(*loop)[i],b=(*loop)[(i+1)%loop->size()];
            if((a.y<middle && b.y>middle) || (b.y<middle && a.y>middle)) edges.push_back({a,b});
        }
        std::sort(edges.begin(),edges.end(),[&](const Edge& a,const Edge& b) { return a.X(middle)<b.X(middle); });
        Require(edges.size()%2==0,"Could not triangulate these track boundaries.");
        for(size_t i=0;i<edges.size();i+=2)
        {
            const Point a{edges[i].X(low),low},b{edges[i+1].X(low),low},c{edges[i+1].X(high),high},d{edges[i].X(high),high};
            if(std::abs(Cross(b-a,c-a))>Epsilon) triangles.push_back({a,b,c});
            if(std::abs(Cross(c-a,d-a))>Epsilon) triangles.push_back({a,c,d});
        }
    }
    Require(!triangles.empty(),"Generated track surface is empty.");
    return triangles;
}
}
void MakeUniformKnots(NurbsOutline& curve)
{
    curve.knots.resize(curve.controls.size()+2*curve.degree+1);
    for(size_t i=0;i<curve.knots.size();++i) curve.knots[i]=static_cast<double>(i);
}
void ValidateCurve(const NurbsOutline& curve)
{
    Require(!curve.name.empty() && curve.name.size()<=128,"Outline names need 1 to 128 characters.");
    Require(curve.degree>=1 && curve.degree<=MaximumNurbsDegree,"This slice supports closed NURBS degree 1, 2 or 3.");
    Require(curve.controls.size()>curve.degree && curve.controls.size()<=64,"An outline needs degree+1 to 64 weighted control points.");
    Require(curve.knots.size()==curve.controls.size()+2*curve.degree+1,"Invalid periodic NURBS knot count.");
    for(auto p:curve.controls) Require(Finite(p.position) && std::isfinite(p.weight) && p.weight>=.01 && p.weight<=100,"Control positions must be finite; weights must be in [0.01,100].");
    for(size_t i=0;i<curve.knots.size();++i) Require(std::isfinite(curve.knots[i]) && std::abs(curve.knots[i])<=100000 && (i==0 || curve.knots[i]>=curve.knots[i-1]),"Knots must be finite, bounded and nondecreasing.");
    Require(curve.knots[curve.controls.size()+curve.degree]-curve.knots[curve.degree]>Epsilon,"The knot domain has no length.");
    for(auto color:curve.colorSrgb) Require(std::isfinite(color) && color>=0 && color<=1,"Ground colors must be in [0,1].");
}
Point Evaluate(const NurbsOutline& curve,double parameter)
{
    ValidateCurve(curve);
    Require(std::isfinite(parameter) && parameter>=0 && parameter<=1,"Curve parameter must be in [0,1].");
    const auto n=curve.controls.size(); const auto p=curve.degree;
    const auto low=curve.knots[p],high=curve.knots[n+p];
    const auto u=std::min(low+(high-low)*parameter,std::nextafter(high,low));
    const auto found=std::upper_bound(curve.knots.begin()+p,curve.knots.begin()+n+p+1,u);
    const auto span=static_cast<size_t>(found-curve.knots.begin()-1);
    struct Homogeneous { Point xy; double w; };
    std::array<Homogeneous,MaximumNurbsDegree+1> d{};
    for(unsigned j=0;j<=p;++j) { const auto& c=curve.controls[(span-p+j)%n]; d[j]={c.position*c.weight,c.weight}; }
    for(unsigned r=1;r<=p;++r) for(unsigned j=p;j>=r;--j)
    {
        const auto i=span-p+j;
        const auto denominator=curve.knots[i+p-r+1]-curve.knots[i];
        const auto alpha=denominator>Epsilon ? (u-curve.knots[i])/denominator : 0;
        d[j]={d[j-1].xy*(1-alpha)+d[j].xy*alpha,d[j-1].w*(1-alpha)+d[j].w*alpha};
    }
    Require(d[p].w>Epsilon,"The curve has an invalid rational denominator.");
    return d[p].xy*(1/d[p].w);
}
std::vector<Point> Tessellate(const NurbsOutline& curve,unsigned samplesPerSpan)
{
    ValidateCurve(curve);
    Require(samplesPerSpan>=4 && samplesPerSpan<=32,"Curve sampling must be 4 to 32 per control span.");
    Require((Evaluate(curve,0)-Evaluate(curve,1)).Length()<1e-5,"The supplied knots do not close this periodic curve.");
    const auto samples=static_cast<unsigned>(curve.controls.size())*samplesPerSpan;
    std::vector<Point> result; result.reserve(samples);
    for(unsigned i=0;i<samples;++i)
    {
        auto point=Evaluate(curve,static_cast<double>(i)/samples);
        if(result.empty() || (point-result.back()).Length()>Epsilon) result.push_back(point);
    }
    if(result.size()>1 && (result.back()-result.front()).Length()<Epsilon) result.pop_back();
    return result;
}
bool Contains(const std::vector<Point>& polygon,Point point)
{
    bool inside=false;
    for(size_t i=0;i<polygon.size();++i)
    {
        const auto a=polygon[i],b=polygon[(i+1)%polygon.size()];
        if(SegmentDistance(point,a,b)<Epsilon) return true;
        if((a.y>point.y)!=(b.y>point.y) && point.x<(b.x-a.x)*(point.y-a.y)/(b.y-a.y)+a.x) inside=!inside;
    }
    return inside;
}
GeneratedTrack Generate(const Track& track)
{
    Require(track.outlines.size()>=2 && track.outlines.size()<=6,"Create enclosing ground, outer track and up to four inner islands.");
    GeneratedTrack result;
    for(const auto& curve:track.outlines) { result.boundaries.push_back(Tessellate(curve)); ValidateLoop(result.boundaries.back()); }
    for(size_t i=0;i<result.boundaries.size();++i) for(size_t j=i+1;j<result.boundaries.size();++j)
    {
        const auto& a=result.boundaries[i]; const auto& b=result.boundaries[j];
        for(size_t ai=0;ai<a.size();++ai) for(size_t bi=0;bi<b.size();++bi)
            Require(!Intersects(a[ai],a[(ai+1)%a.size()],b[bi],b[(bi+1)%b.size()]),"Outline boundaries touch or intersect. Separate them before building.");
        if(i==0 || i==1) Require(Contains(a,b.front()),"Ground must enclose the outer track; the outer track must enclose every island.");
        else Require(!Contains(a,b.front()) && !Contains(b,a.front()),"Inner islands cannot be nested.");
    }
    result.surfaces.push_back(Triangulate({&result.boundaries[0],&result.boundaries[1]}));
    std::vector<const std::vector<Point>*> road{&result.boundaries[1]};
    for(size_t i=2;i<result.boundaries.size();++i) road.push_back(&result.boundaries[i]);
    result.surfaces.push_back(Triangulate(road));
    for(size_t i=2;i<result.boundaries.size();++i) result.surfaces.push_back(Triangulate({&result.boundaries[i]}));
    return result;
}
Track MakeExampleTrack()
{
    Track result; result.name="First circuit";
    for(auto [name,rx,ry,color] : std::vector<std::tuple<std::string,double,double,std::array<double,3>>>{
        {"Ground",44,34,{.55,.63,.43}},{"Outer track",35,26,{.38,.39,.40}},{"Inner island",23,14,{.55,.63,.43}}})
    {
        NurbsOutline outline; outline.name=name;
        for(unsigned i=0;i<12;++i) { const auto angle=2*std::numbers::pi*i/12; outline.controls.push_back({{rx*std::cos(angle),ry*std::sin(angle)},1}); }
        outline.colorSrgb=color; MakeUniformKnots(outline); result.outlines.push_back(std::move(outline));
    }
    result.hasSpawn=result.hasFinish=true; result.spawn={{0,-18},0};
    result.finish={{-4,-13},{-4,-24}};
    // Gate orientation follows counter-clockwise travel around the island.
    result.checkpoints={{{23,-4},{33,-4}},{{4,14},{4,24}},{{-23,4},{-33,4}}};
    return result;
}
bool Driveable(const GeneratedTrack& track,Point position,double radius)
{
    if(track.boundaries.size()<2 || !Contains(track.boundaries[1],position)) return false;
    for(size_t i=2;i<track.boundaries.size();++i) if(Contains(track.boundaries[i],position)) return false;
    for(size_t i=1;i<track.boundaries.size();++i)
    {
        const auto& loop=track.boundaries[i];
        for(size_t j=0;j<loop.size();++j) if(SegmentDistance(position,loop[j],loop[(j+1)%loop.size()])<radius) return false;
    }
    return true;
}
void ValidateRace(const Track& track,const GeneratedTrack& generated,double radius)
{
    Require(track.hasSpawn && track.hasFinish,"Place a vehicle placeholder and a checkered finish line first.");
    Require(track.checkpoints.size()>=1 && track.checkpoints.size()<=32,"Place 1 to 32 checkpoints in driving order.");
    Require(Finite(track.spawn.position) && std::isfinite(track.spawn.heading) && radius>0 && radius<100 && Driveable(generated,track.spawn.position,radius),"The vehicle placeholder must fit completely inside the road, away from its boundaries.");
    auto validateGate=[&](Gate gate) { Require(Finite(gate.a) && Finite(gate.b) && (gate.b-gate.a).Length()>.1,"A finish/checkpoint needs two distinct finite endpoints."); Require(Driveable(generated,(gate.a+gate.b)*.5,0),"A finish/checkpoint's center must lie on the road."); };
    validateGate(track.finish); for(auto gate:track.checkpoints) validateGate(gate);
}
bool Crosses(Gate gate,Point previous,Point current)
{
    const auto line=gate.b-gate.a;
    const auto before=Cross(line,previous-gate.a),after=Cross(line,current-gate.a);
    if(before>=0 || after<0) return false;
    const auto position=previous+(current-previous)*(before/(before-after));
    const auto along=Dot(position-gate.a,line)/Dot(line,line);
    return along>=0 && along<=1;
}
void Session::Begin(const Track& track,const GeneratedTrack& generated,double collisionRadius)
{
    ValidateRace(track,generated,collisionRadius);
    track_=&track; generated_=&generated; radius_=collisionRadius; active_=true; state_={}; Reset();
}
void AdvanceProgress(const Track& track,Point previous,Point current,CarState& state)
{
    if(state.nextCheckpoint<track.checkpoints.size() && Crosses(track.checkpoints[state.nextCheckpoint],previous,current))
    {
        ++state.nextCheckpoint; state.lapArmed=state.nextCheckpoint==track.checkpoints.size();
    }
    if(state.lapArmed && Crosses(track.finish,previous,current)) { ++state.laps; state.nextCheckpoint=0; state.lapArmed=false; }
}
void Session::Reset()
{
    if(!active_) return;
    state_.position=track_->spawn.position; state_.velocity={}; state_.heading=track_->spawn.heading;
    state_.angularVelocity=0; state_.nextCheckpoint=0; state_.lapArmed=false;
}
void Session::Step(const Commands& input,double seconds)
{
    if(!active_) return;
    Require(std::isfinite(seconds) && seconds>0 && seconds<=.1 && Finite(input.acceleration) && std::isfinite(input.brake),"Invalid simulation input or step.");
    if(input.reset) { Reset(); return; }
    auto acceleration=input.acceleration;
    if(acceleration.Length()>1) acceleration=acceleration*(1/acceleration.Length());
    const auto oldPosition=state_.position;
    state_.velocity=state_.velocity+acceleration*(22*seconds);
    const auto speed=state_.velocity.Length();
    const auto deceleration=(.7+std::clamp(input.brake,0.0,1.0)*60)*seconds;
    if(speed<=deceleration) state_.velocity={}; else state_.velocity=state_.velocity*(std::min(32.0,speed-deceleration)/speed);
    // Bounded distance substeps avoid skipping a narrow boundary at high speed.
    const auto displacement=state_.velocity*seconds;
    const auto steps=std::max(1u,static_cast<unsigned>(std::ceil(displacement.Length()/std::max(.05,radius_*.25))));
    for(unsigned step=0;step<steps;++step)
    {
        const auto next=state_.position+displacement*(1.0/steps);
        if(!Driveable(*generated_,next,radius_)) { ++state_.collisions; Reset(); return; }
        state_.position=next;
    }
    const auto previousHeading=state_.heading;
    if(state_.velocity.Length()>.05) state_.heading=std::atan2(state_.velocity.y,state_.velocity.x);
    state_.angularVelocity=std::remainder(state_.heading-previousHeading,2*std::numbers::pi)/seconds;
    AdvanceProgress(*track_,oldPosition,state_.position,state_);
}
}
