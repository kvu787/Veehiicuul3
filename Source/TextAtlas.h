#pragma once
#include "Ui.h"
#include <dwrite.h>
#include <wrl/client.h>
#include <array>
#include <map>
#include <cstdint>
#include <span>

struct UiVertex { float x,y,u,v,r,g,b,a; };
struct UiGpuData
{
    std::vector<UiVertex> vertices;
    std::span<const uint8_t> atlas;
    uint64_t revision=0;
    static constexpr unsigned AtlasSize=2048;
};
class TextAtlas
{
public:
    void Initialize();
    void Paint(const Ui::State& ui,const std::wstring& status);
    const UiGpuData& Data() const { return data_; }
    size_t CachedLabels() const { return entries_.size(); }
private:
    using Color=std::array<float,4>;
    struct Entry { float x,y,w,h; };
    Entry Text(const std::wstring& text);
    void Quad(Ui::Rect r,Ui::Rect uv,Color color,Ui::Rect clip);
    void Rectangle(Ui::Rect r,Color color,Ui::Rect clip);
    void Label(const std::wstring& value,float x,float y,Color color,Ui::Rect clip);
    Microsoft::WRL::ComPtr<IDWriteFactory> factory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format_;
    Microsoft::WRL::ComPtr<IDWriteRenderingParams> params_;
    Microsoft::WRL::ComPtr<IDWriteBitmapRenderTarget> target_;
    std::map<std::wstring,Entry> entries_;
    std::vector<uint8_t> pixels_;
    unsigned x_=2,y_=2,rowHeight_=0;
    UiGpuData data_;
};
