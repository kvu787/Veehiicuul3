#include "SimplePaintReference.h"
#include "SimplePaint/Geometry.h"
#include "SimplePaint/OrthographicTransforms.h"
#include <iostream>
#include <limits>

using namespace SimplePaint;
using PaintTest::Require;

template<class Function> void Throws(Function function)
{
    try { function(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Invalid input did not throw std::invalid_argument");
}

int main()
{
    try
    {
        Parameters p;
        // Exercise every scalar independently, including both adjacent doubles
        // at each boundary so invalid input cannot become valid by float rounding.
        for (unsigned field = 0; field < 8; ++field)
        {
            const double lo = field < 4 || field == 7 ? Margin : 0.0;
            const double hi = field == 5 ? 360.0 : field == 7 ? 1.0 : InteriorMaximum;
            auto set = [&](double value) {
                p = {};
                double* fields[] = {&p.baseColorSrgb[0],&p.baseColorSrgb[1],&p.baseColorSrgb[2],
                    &p.brightness,&p.shift,&p.rotationDegrees,&p.darkPoint,&p.lightPoint};
                *fields[field] = value;
            };
            for (double bad : {std::nextafter(lo,-INFINITY),std::nextafter(hi,INFINITY),
                -double(INFINITY),double(INFINITY),std::numeric_limits<double>::quiet_NaN()})
            {
                set(bad); Throws([&]{ (void)Material::Compile(p); });
            }
            set(lo); (void)Material::Compile(p);
            set(field == 5 ? std::nextafter(hi,0.0) : hi); (void)Material::Compile(p);
            if (field == 5) { set(360.0); Throws([&]{ (void)Material::Compile(p); }); }
        }
        // Compare packed GPU endpoint coefficients to the independent reference
        // over the whole tone interval; assert anchors, endpoints and monotonicity.
        std::size_t checks = 0;
        for (double c : {Margin,0.04045,0.223,0.5,InteriorMaximum})
        for (double b : {Margin,0.126,0.5,InteriorMaximum})
        {
            p = {
                .baseColorSrgb = {c,c,c},
                .brightness = b,
                .shift = 0,
                .rotationDegrees = 0,
                .darkPoint = 0,
                .lightPoint = 1,
            };
            const auto m = Material::Compile(p).Constants();
            double previous = -1.0;
            for (unsigned i = 0; i <= 4096; ++i)
            {
                const double f = double(i)/4096;
                const double numerator = m.numeratorDark[0]*(1-f)+m.numeratorLight[0]*f;
                const double complement = m.complementDark[0]*(1-f)+m.complementLight[0]*f;
                const double value = numerator/(numerator+complement);
                Require(std::abs(value-PaintTest::Curve(c,b,f)) < 1.0e-7,"Packed curve differs from reference");
                Require(value >= previous,"Schlick curve is not monotone"); previous=value; ++checks;
            }
            Require(std::abs(PaintTest::Curve(c,b,1-b)-PaintTest::Linear(c)) < 1.0e-13,"Base-color anchor failed");
            Require(PaintTest::Curve(c,b,0)==0 && PaintTest::Curve(c,b,1)==1,"Endpoints failed");
        }
        using namespace DirectX;
        const auto projection = Orthographic::MakeProjection(5,5,1,20);
        Throws([&]{(void)Orthographic::MakeProjection(0,5,1,20);});
        Throws([&]{(void)Orthographic::MakeProjection(5,5,20,1);});
        Throws([&]{(void)Orthographic::BuildObjectTransforms(XMMatrixScaling(1,2,1),projection);});
        Throws([&]{(void)Orthographic::BuildObjectTransforms(XMMatrixScaling(0,0,0),projection);});
        Throws([&]{(void)Orthographic::BuildObjectTransforms(XMMatrixPerspectiveFovRH(1,1,1,20),projection);});
        // Independently preserve K12's original slice -> Schlick -> remap math
        // away from its removed cutoff and ill-conditioned slice endpoints.
        std::mt19937 random(0x4b3132);
        std::uniform_real_distribution<double> unit(0.0,1.0);
        for (unsigned i=0;i<20000;++i)
        {
            const double y=1.8*unit(random)-0.9;
            const double r=std::sqrt(1-y*y);
            const double angle=2.8*unit(random)-1.4;
            const double x=r*std::sin(angle),z=r*std::cos(angle),s=InteriorMaximum*unit(random);
            const double t=(x+r)/(2*r),u1=(1+s)/2,u2=1-u1;
            const double schlick=u2*t/(u1*(1-t)+u2*t);
            const double warpedX=-r+2*r*schlick;
            const double original=std::sqrt(std::max(0.0,1-warpedX*warpedX-y*y));
            const double reduced=r*z*std::sqrt((1-s)*(1+s))/(r-s*x);
            Require(std::abs(original-reduced)<1.0e-11,"Original K12 lobe and rational reduction differ");
        }
        std::cout << checks << " curve comparisons; parameter boundaries, anchors and transform validation passed.\n";
        std::cout << "20,000 independent original K12 slice/Schlick/remap comparisons passed.\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
