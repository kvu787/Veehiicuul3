#include "PaintControls.h"
#include "Scene.h"
#include <fstream>
#include <iostream>
#include <limits>

namespace
{
void Require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
template<class F> void Reject(F action) { bool rejected=false; try { action(); } catch(const std::exception&) { rejected=true; } Require(rejected,"Invalid control value was accepted."); }
}
int main(int argc,char** argv)
{
    try
    {
        for(int i=0;i<PaintParameterCount;++i)
        {
            const auto parameter=static_cast<PaintParameter>(i);
            const auto range=ParameterRange(parameter);
            auto paint=SimplePaint::Parameters{};
            for(double value : {range.minimum,range.maximum})
            { SetParameter(paint,parameter,value); SimplePaint::ValidateParameters(paint); Require(ParameterValue(paint,parameter)==value,"Boundary changed."); }
            Require(SliderValue(0,parameter)==range.minimum && SliderValue(range.steps,parameter)==range.maximum,"Slider endpoints exclude valid paint.");
            const double current=range.minimum+(range.maximum-range.minimum)*.345097446;
            const double delta=DraggedValue(current,1,270,1,parameter)-current;
            for(double speed : {.1,.01,.0001})
            {
                const double fine=DraggedValue(current,1,270,speed,parameter)-current;
                Require(fine>0 && std::abs(fine/delta-speed)<1e-10,"Fine drag was lost to thumb quantization.");
            }
            Require(DraggedValue(current,0,270,.01,parameter)==current,"A drag start must not jump.");
            Require(DraggedValue(range.minimum,-1,270,1,parameter)==range.minimum && DraggedValue(range.maximum,1,270,1,parameter)==range.maximum,"Drag boundary constraints failed.");
            const double farRight=DraggedValue(current,2000000000.0,270,.0001,parameter);
            Require(farRight==range.maximum && DraggedValue(farRight,-1,270,.0001,parameter)<farRight,"Huge relative travel must reach the boundary and reverse immediately.");
            Require(DraggedValue(current,-2000000000.0,270,.0001,parameter)==range.minimum,"Unlimited negative travel must retain left semantics.");
            (void)SliderPosition(current,parameter);
            Reject([&]{(void)SliderPosition(std::nextafter(range.minimum,-1.0),parameter);});
            Reject([&]{(void)SliderPosition(std::nextafter(range.maximum,1000.0),parameter);});
        }
        Reject([]{ValidateDragSpeed(0);}); Reject([]{ValidateDragSpeed(11);}); Reject([]{ValidateDragSpeed(std::numeric_limits<double>::quiet_NaN());});
        Scene scene=MakeSlopeCarScene(); scene.sliderDragSpeed=.01;
        scene.objects[1].paint.rotationDegrees=std::nextafter(360.0,0.0);
        scene.objects[1].paint.shift=.345097446;
        const auto directory=argc>1 ? std::filesystem::path(argv[1]) : std::filesystem::temp_directory_path();
        std::filesystem::create_directories(directory);
        const auto path=directory/"SliderRoundTrip.modeler"; SaveScene(scene,path);
        const auto loaded=LoadScene(path);
        Require(loaded.sliderDragSpeed==.01 && SamePaint(loaded.objects[1].paint,scene.objects[1].paint),"Exact paint or drag speed did not persist.");
        const auto seed=LoadScene(std::filesystem::path(MODELER_SOURCE)/"Source/Assets/SlopeCar.modeler");
        Require(seed.sliderDragSpeed==1,"The current seed must explicitly store normal drag speed.");
        const auto saved=SerializeScene(seed);
        for(const std::string field:{"SliderDragSpeed","MaterialKind","UnlitColorSrgb"})
        {
            auto missing=saved; const auto first=missing.find('"'+field+'"'); const auto last=missing.find(',',first);
            Require(first!=std::string::npos && last!=std::string::npos,"Current format field missing from seed serialization.");
            // Rename the required key without touching any numeric/geometry data.
            missing.replace(first+1,field.size(),"MissingField"); Reject([&]{ (void)DeserializeScene(missing); });
        }
        scene.sliderDragSpeed=.00001; Reject([&]{ValidateScene(scene);});
        std::cout << "Passed exact full domains, sub-thumb fine pointer motion, boundaries, sensitivity validation, exact project round trip and strict current-format fields.\n";
        return 0;
    }
    catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
