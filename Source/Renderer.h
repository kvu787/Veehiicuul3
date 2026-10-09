#pragma once
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>
#include "SimplePaint/Material.h"
#include "SimplePaint/OrthographicTransforms.h"
#include "Geometry.h"
#include "DisplayTiming.h"
#include "TextAtlas.h"

class Renderer
{
public:
    ~Renderer();
    void Initialize(HWND window, bool forceSoftware, bool debugLayer);
    void Resize(unsigned width, unsigned height);
    void Render(const ViewGeometry& geometry, const Orthographic::ObjectTransforms& transforms,
                std::span<const SimplePaint::GpuMaterial> materials,std::span<const std::array<float,4>> surfaces={},const Orthographic::ObjectTransforms* vehicle=nullptr,const DisplayPresentProbe* probe=nullptr,
                const UiGpuData* ui=nullptr,Ui::Rect view={438,62,2122,1316},bool present=true);
    void Capture(const std::filesystem::path& path,bool presented=false);
    HANDLE FrameReady() const { return frameReady_; }
    const std::string& AdapterName() const { return adapterName_; }
    unsigned Width() const { return width_; }
    unsigned Height() const { return height_; }
    unsigned DebugErrors() const;
    std::string DebugMessages() const;
    bool HasDebugLayer() const { return infoQueue_ != nullptr; }
    void WaitIdle();
    static constexpr UINT VisibilityMessage=WM_APP+23;
    void StopVisibilityNotifications();
    uint64_t Presents() const { return presents_; }
private:
    template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
    struct Frame
    {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12Resource> buffer;
        std::byte* mapped = nullptr;
        size_t capacity = 0;
        uint64_t fence = 0;
        uint64_t revision = 0;
        const ViewGeometry* owner=nullptr;
        D3D12_VERTEX_BUFFER_VIEW vertices{}, grid{}, cage{}, markers{};
        D3D12_INDEX_BUFFER_VIEW indices{};
        ComPtr<ID3D12Resource> uiBuffer,atlasUpload;
        size_t uiCapacity=0;
        uint8_t* uiMapped=nullptr;
    };
    void CreateTargets();
    void WaitFence(uint64_t value);
    void FillFrame(Frame& frame, const ViewGeometry& geometry);
    void CreateUi();
    void DrawUi(Frame& frame,const UiGpuData& ui);
    void PresentSurface();
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<IDXGISwapChain3> swapChain_;
    ComPtr<IDXGIFactory6> factory_;
    DWORD occlusionCookie_=0;
    bool occlusionRegistered_=false;
    ComPtr<ID3D12DescriptorHeap> renderHeap_, depthHeap_;
    std::array<ComPtr<ID3D12Resource>, 2> targets_;
    ComPtr<ID3D12Resource> depth_;
    ComPtr<ID3D12RootSignature> rootSignature_;
    ComPtr<ID3D12PipelineState> paintPipeline_, gridPipeline_, cagePipeline_, markerPipeline_;
    ComPtr<ID3D12GraphicsCommandList> commands_;
    ComPtr<ID3D12Fence> fence_;
    ComPtr<ID3D12InfoQueue> infoQueue_;
    ComPtr<ID3D12RootSignature> uiRoot_;
    ComPtr<ID3D12PipelineState> uiPipeline_,presentPipeline_;
    ComPtr<ID3D12Resource> atlasTexture_,surface_;
    ComPtr<ID3D12DescriptorHeap> textureHeap_;
    uint64_t atlasRevision_=0,presents_=0;
    unsigned displayWidth_=1,displayHeight_=1;
    std::array<Frame, 2> frames_;
    ComPtr<ID3D12Resource> materials_;
    std::byte* mappedMaterials_ = nullptr;
    HANDLE fenceEvent_ = nullptr, frameReady_ = nullptr;
    unsigned width_ = 1, height_ = 1, descriptorSize_ = 0, lastFrame_ = 0;
    uint64_t nextFence_ = 1;
    bool tearing_ = false;
    std::string adapterName_;
};
