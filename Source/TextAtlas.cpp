#include "TextAtlas.h"
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace
{
void CheckText(HRESULT r) { if(FAILED(r)) throw std::runtime_error("DirectWrite text rasterization failed."); }
class GlyphRenderer final : public IDWriteTextRenderer
{
public:
    GlyphRenderer(IDWriteBitmapRenderTarget* target,IDWriteRenderingParams* params) : target_(target),params_(params) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p) override
    {
        if(!p) return E_POINTER;
        if(id==__uuidof(IUnknown) || id==__uuidof(IDWriteTextRenderer) || id==__uuidof(IDWritePixelSnapping)) { *p=this; return S_OK; }
        *p=nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*,BOOL* disabled) override { *disabled=FALSE; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*,DWRITE_MATRIX* matrix) override { *matrix={1,0,0,1,0,0}; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*,FLOAT* value) override { *value=1; return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*,FLOAT x,FLOAT y,DWRITE_MEASURING_MODE mode,const DWRITE_GLYPH_RUN* run,const DWRITE_GLYPH_RUN_DESCRIPTION*,IUnknown*) override
    { return target_->DrawGlyphRun(x,y,mode,run,params_,RGB(255,255,255),nullptr); }
    HRESULT STDMETHODCALLTYPE DrawUnderline(void*,FLOAT,FLOAT,const DWRITE_UNDERLINE*,IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*,FLOAT,FLOAT,const DWRITE_STRIKETHROUGH*,IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void*,FLOAT,FLOAT,IDWriteInlineObject*,BOOL,BOOL,IUnknown*) override { return E_NOTIMPL; }
private:
    IDWriteBitmapRenderTarget* target_;
    IDWriteRenderingParams* params_;
};
}
void TextAtlas::Initialize()
{
    CheckText(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(factory_.GetAddressOf())));
    CheckText(factory_->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,20,L"en-us",&format_));
    CheckText(format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
    CheckText(factory_->CreateCustomRenderingParams(1,1,0,DWRITE_PIXEL_GEOMETRY_FLAT,DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,&params_));
    Microsoft::WRL::ComPtr<IDWriteGdiInterop> interop;
    CheckText(factory_->GetGdiInterop(&interop));
    CheckText(interop->CreateBitmapRenderTarget(nullptr,UiGpuData::AtlasSize,64,&target_));
    CheckText(target_->SetPixelsPerDip(1));
    pixels_.assign(UiGpuData::AtlasSize*UiGpuData::AtlasSize,0); pixels_[0]=255;
    data_.atlas=pixels_; data_.revision=1;
}
TextAtlas::Entry TextAtlas::Text(const std::wstring& text)
{
    if(const auto it=entries_.find(text);it!=entries_.end()) return it->second;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    CheckText(factory_->CreateTextLayout(text.c_str(),static_cast<UINT32>(text.size()),format_.Get(),1800,48,&layout));
    DWRITE_TEXT_METRICS metrics{}; CheckText(layout->GetMetrics(&metrics));
    const auto width=static_cast<unsigned>(std::min(1800.f,std::ceil(metrics.widthIncludingTrailingWhitespace)+4));
    const auto height=static_cast<unsigned>(std::min(60.f,std::ceil(metrics.height)+4));
    if(x_+width+2>=UiGpuData::AtlasSize) { x_=2; y_+=rowHeight_+2; rowHeight_=0; }
    if(y_+height+2>=UiGpuData::AtlasSize)
    {
        // Whole-atlas invalidation is safe: Paint prewarms labels before it
        // emits quads, so no draw list references displaced cache entries.
        entries_.clear(); std::fill(pixels_.begin(),pixels_.end(),uint8_t{0}); pixels_[0]=255;
        x_=y_=2; rowHeight_=0; ++data_.revision;
    }
    const HDC dc=target_->GetMemoryDC(); PatBlt(dc,0,0,UiGpuData::AtlasSize,64,BLACKNESS);
    GlyphRenderer renderer(target_.Get(),params_.Get());
    CheckText(layout->Draw(nullptr,&renderer,2,2));
    DIBSECTION dib{};
    if(!GetObjectW(GetCurrentObject(dc,OBJ_BITMAP),sizeof(dib),&dib) || !dib.dsBm.bmBits) throw std::runtime_error("DirectWrite bitmap is unavailable.");
    GdiFlush();
    const auto* bits=static_cast<const uint8_t*>(dib.dsBm.bmBits);
    for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x)
    {
        // IDWriteBitmapRenderTarget exposes its DIB in top-to-bottom memory
        // order. Its DIBSECTION height sign is not the pixel-row orientation.
        const auto* pixel=bits+static_cast<size_t>(y)*dib.dsBm.bmWidthBytes+x*4;
        pixels_[(y_+y)*UiGpuData::AtlasSize+x_+x]=std::max({pixel[0],pixel[1],pixel[2]});
    }
    const Entry entry{static_cast<float>(x_),static_cast<float>(y_),static_cast<float>(width),static_cast<float>(height)};
    entries_.emplace(text,entry); x_+=width+2; rowHeight_=std::max(rowHeight_,height); ++data_.revision; return entry;
}
void TextAtlas::Quad(Ui::Rect r,Ui::Rect uv,Color c,Ui::Rect clip)
{
    const auto visible=Ui::Intersect(r,clip); if(visible.w<=0 || visible.h<=0 || r.w<=0 || r.h<=0) return;
    const auto u0=uv.x+(visible.x-r.x)/r.w*uv.w,v0=uv.y+(visible.y-r.y)/r.h*uv.h;
    const auto u1=u0+visible.w/r.w*uv.w,v1=v0+visible.h/r.h*uv.h;
    const auto a=UiVertex{visible.x,visible.y,u0,v0,c[0],c[1],c[2],c[3]};
    const auto b=UiVertex{visible.x+visible.w,visible.y,u1,v0,c[0],c[1],c[2],c[3]};
    const auto d=UiVertex{visible.x,visible.y+visible.h,u0,v1,c[0],c[1],c[2],c[3]};
    const auto e=UiVertex{visible.x+visible.w,visible.y+visible.h,u1,v1,c[0],c[1],c[2],c[3]};
    data_.vertices.insert(data_.vertices.end(),{a,b,d,d,b,e});
}
void TextAtlas::Rectangle(Ui::Rect r,Color c,Ui::Rect clip) { Quad(r,{.5f/UiGpuData::AtlasSize,.5f/UiGpuData::AtlasSize,0,0},c,clip); }
void TextAtlas::Label(const std::wstring& text,float x,float y,Color c,Ui::Rect clip)
{
    const auto e=Text(text); const auto size=static_cast<float>(UiGpuData::AtlasSize);
    Quad({x-2,y-2,e.w,e.h},{e.x/size,e.y/size,e.w/size,e.h/size},c,clip);
}
void TextAtlas::Paint(const Ui::State& ui,const std::wstring& status)
{
    const Ui::Rect all{0,0,Ui::Width,Ui::Height};
    auto value=[&](const Ui::Control& c) { if(c.id==ui.focus && c.kind==Ui::Kind::Number) return ui.edit; std::wostringstream s; s<<std::setprecision(12)<<c.value; return s.str(); };
    // Bound numeric-string growth and prewarm before any UVs are emitted.
    if(entries_.size()>300 || y_>1400) { entries_.clear(); std::fill(pixels_.begin(),pixels_.end(),uint8_t{0}); pixels_[0]=255; x_=y_=2; rowHeight_=0; ++data_.revision; }
    Text(status);
    for(const auto& c:ui.controls) { Text(c.text); if(c.kind==Ui::Kind::Number) Text(value(c)); }
    if(ui.Editing()) Text(ui.edit.substr(0,ui.caret));
    data_.vertices.clear();
    Rectangle({0,0,2560,62},{.055f,.06f,.07f,1},all);
    Rectangle(ui.panel,{.085f,.09f,.105f,1},all);
    Rectangle({0,1378,2560,62},{.055f,.06f,.07f,1},all);
    for(const auto& c:ui.controls)
    {
        const auto clip=c.clip;
        if(c.kind!=Ui::Kind::Label)
        {
            const auto color=!c.enabled ? Color{.09f,.095f,.105f,1} : c.id==ui.capture ? Color{.18f,.22f,.29f,1} : c.selected ? Color{.14f,.20f,.29f,1} : c.id==ui.hover ? Color{.17f,.18f,.21f,1} : Color{.12f,.13f,.15f,1};
            Rectangle(c.rect,color,clip);
            if(c.id==ui.focus) { const auto col=ui.invalid ? Color{.7f,.13f,.08f,1} : Color{.28f,.49f,.8f,1}; Rectangle({c.rect.x,c.rect.y+c.rect.h-2,c.rect.w,2},col,clip); }
        }
        const auto textColor=c.enabled ? Color{.82f,.84f,.88f,1} : Color{.32f,.34f,.38f,1};
        if(c.kind==Ui::Kind::Slider)
        {
            Rectangle({c.rect.x+8,c.rect.y+c.rect.h*.5f-2,c.rect.w-16,4},{.28f,.30f,.34f,1},clip);
            const auto t=static_cast<float>((c.value-c.minimum)/(c.maximum-c.minimum));
            Rectangle({c.rect.x+8+t*(c.rect.w-16)-3,c.rect.y+5,6,c.rect.h-10},textColor,clip);
        }
        else
        {
            const auto text=c.kind==Ui::Kind::Number ? value(c) : c.text;
            if(c.kind==Ui::Kind::Number && c.id==ui.focus && ui.caret!=ui.anchor) Rectangle({c.rect.x+6,c.rect.y+5,c.rect.w-12,c.rect.h-10},{.12f,.26f,.47f,1},clip);
            const auto textClip=Ui::Intersect(c.rect,clip);
            float textX=c.rect.x+6;
            if(c.kind==Ui::Kind::Number && c.id==ui.focus) { const auto prefix=Text(ui.edit.substr(0,ui.caret)); textX-=std::max(0.f,prefix.w-4-(c.rect.w-14)); }
            Label(text,textX,c.rect.y+7,textColor,textClip);
            // Static caret: numeric editing never arms a periodic blink timer.
            if(c.kind==Ui::Kind::Number && c.id==ui.focus)
            {
                const auto prefix=Text(ui.edit.substr(0,ui.caret));
                Rectangle({textX+prefix.w-4,c.rect.y+5,1,c.rect.h-10},textColor,textClip);
            }
        }
    }
    if(ui.contentHeight>ui.panel.h) { Rectangle({ui.panel.x+ui.panel.w-14,ui.panel.y,14,ui.panel.h},{.05f,.055f,.065f,1},all); Rectangle(ui.Thumb(),{.28f,.30f,.34f,1},all); }
    Label(status,12,1398,{.64f,.68f,.73f,1},all);
}
