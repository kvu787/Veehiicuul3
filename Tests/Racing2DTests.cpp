#include "Racing2D.h"
#include <iostream>
#include <numbers>
#include <stdexcept>

using namespace Racing2D;
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
template<class F> void Reject(F function,const char* message) { try { function(); } catch(const std::invalid_argument&) { return; } throw std::runtime_error(message); }
int main()
{
    try
    {
        NurbsOutline circle; circle.degree=2;
        const auto w=std::sqrt(.5);
        circle.controls={{{0,-1},1},{{-1,-1},w},{{-1,0},1},{{-1,1},w},{{0,1},1},{{1,1},w},{{1,0},1},{{1,-1},w}};
        circle.knots={-1,0,0,1,1,2,2,3,3,4,4,5,5};
        for(unsigned i=0;i<=100;++i) Require(std::abs(Evaluate(circle,i/100.0).Length()-1)<1e-12,"Rational NURBS circle must stay on the unit circle.");
        auto uniform=circle; MakeUniformKnots(uniform);
        Require((Evaluate(uniform,0)-Evaluate(uniform,1)).Length()<1e-12,"Periodic knot seam must close.");
        uniform.controls[0].weight=0; Reject([&] { Tessellate(uniform); },"Invalid weights were accepted.");
        auto track=MakeExampleTrack(); const auto generated=Generate(track);
        Require(generated.surfaces.size()==3 && Driveable(generated,{0,-18},2) && !Driveable(generated,{0,0},0) && !Driveable(generated,{90,0},0),"Road must exclude inner islands and exterior.");
        double roadArea=0;
        for(auto triangle:generated.surfaces[1])
        {
            roadArea+=std::abs(Cross(triangle.b-triangle.a,triangle.c-triangle.a))*.5;
            Require(Driveable(generated,(triangle.a+triangle.b+triangle.c)*(1.0/3),0),"Triangulation filled a road hole or exterior.");
        }
        Require(roadArea>1000 && roadArea<2500,"Generated road area outside expected range.");
        auto broken=track; broken.outlines[2]=broken.outlines[1]; Reject([&] { Generate(broken); },"Intersecting islands were accepted.");
        Require(Crosses({{0,2},{0,-2}},{-1,0},{1,0}) && !Crosses({{0,2},{0,-2}},{1,0},{-1,0}) && !Crosses({{0,2},{0,-2}},{-1,3},{1,3}),"Directed gate crossing must respect direction and finite segment extent.");
        Session session; session.Begin(track,generated,2);
        CarState progress;
        AdvanceProgress(track,{-8,-18},{0,-18},progress); Require(progress.laps==0,"Finish crossing before checkpoints must not count a lap.");
        AdvanceProgress(track,{8,18},{0,18},progress); Require(progress.nextCheckpoint==0,"Out-of-order checkpoints must not advance.");
        AdvanceProgress(track,{28,-8},{28,0},progress); Require(progress.nextCheckpoint==1,"First directed checkpoint must advance.");
        AdvanceProgress(track,{8,18},{0,18},progress); AdvanceProgress(track,{-28,8},{-28,0},progress);
        Require(progress.lapArmed && progress.nextCheckpoint==3,"Every ordered checkpoint must arm the lap.");
        AdvanceProgress(track,{0,-18},{-8,-18},progress); Require(progress.laps==0,"Reverse finish crossing must not count a lap.");
        AdvanceProgress(track,{-8,-18},{0,-18},progress); Require(progress.laps==1 && progress.nextCheckpoint==0 && !progress.lapArmed,"Ordered checkpoints plus forward finish must count exactly one lap and reset progression.");
        const auto spawn=session.State().position;
        for(unsigned i=0;i<60;++i) session.Step({{1,0},0,false},1.0/120);
        Require(session.State().position.x>spawn.x+1 && session.State().velocity.x>5,"2D continuous acceleration must drive the vehicle.");
        session.Step({{},0,true},1.0/120); Require(session.State().position==spawn && session.State().velocity.Length()==0,"Reset must use authored spawn.");
        for(unsigned i=0;i<500;++i) session.Step({{0,-1},0,false},1.0/120);
        Require(session.State().collisions>0 && Driveable(generated,session.State().position,2),"A boundary collision must reset without tunneling out of the road.");
        session.End(); const auto state=session.State().position; session.Step({{1,0},0,false},.01);
        Require(session.State().position==state,"Ending drive mode must stop simulation.");
        std::cout<<"PASS: rational/periodic NURBS, nested outlines, concave/holed triangulation, 2D bounds, directed gates, simulation, collision reset and mode stop.\n";
        return 0;
    }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
