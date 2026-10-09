#include "TextAtlas.h"
#include <iostream>
#include <stdexcept>
void Check(bool c,const char* text) { if(!c) throw std::runtime_error(text); }
int main()
{
    try
    {
        TextAtlas atlas; atlas.Initialize(); Ui::State ui; ui.Begin(); Ui::Control label; label.text=L"Mode"; label.rect={10,10,180,38};ui.Add(label);ui.Finish(60);atlas.Paint(ui,L"Ready");
        size_t ink=0;for(const auto pixel:atlas.Data().atlas)if(pixel)++ink;Check(ink>200,"Platform text rasterization produced no glyph coverage.");
        const auto revision=atlas.Data().revision;atlas.Paint(ui,L"Ready");Check(atlas.Data().revision==revision,"Unchanged text invalidated atlas.");
        const auto objects=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
        for(int i=0;i<700;++i) { ui.controls[0].text=L"Value "+std::to_wstring(i);atlas.Paint(ui,L"Ready");Check(atlas.Data().atlas.size()==2048*2048,"Text atlas grew past budget.");Check(atlas.CachedLabels()<304,"Text cache grew unbounded.");for(const auto& v:atlas.Data().vertices)Check(std::isfinite(v.x)&&std::isfinite(v.y)&&v.x>=0&&v.x<=2560&&v.y>=0&&v.y<=1440,"GPU UI vertex outside clip bounds."); }
        Check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=objects+2,"Text updates leaked GDI resources.");
        ui.Begin();Ui::Control knots;knots.id=9;knots.kind=Ui::Kind::KnotVector;knots.rect={12,0,404,38};knots.text=std::wstring(2048,L'8');ui.Add(knots);ui.Finish(100);ui.Down(20,75);
        for(size_t caret:{size_t{0},size_t{1},size_t{1024},size_t{2048}}) { ui.caret=ui.anchor=caret;atlas.Paint(ui,L"Knot draft");for(const auto& v:atlas.Data().vertices)Check(std::isfinite(v.x)&&std::isfinite(v.y)&&v.x>=0&&v.x<=2560&&v.y>=0&&v.y<=1440,"Long knot edit escaped GPU clipping");Check(atlas.Data().atlas.size()==2048*2048,"Long knot edit grew atlas"); }
        const auto stable=atlas.Data().revision;atlas.Paint(ui,L"Knot draft");Check(atlas.Data().revision==stable,"Static knot caret invalidated cached glyphs");
        std::cout<<"DirectWrite glyph coverage, fixed resource budget, cache reuse and clipped GPU draw data passed.\n";return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
