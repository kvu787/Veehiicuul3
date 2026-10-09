#include "Renderer.h"
#include "UiVertexShader.h"
#include "UiPixelShader.h"
#include "PresentVertexShader.h"
#include "PresentPixelShader.h"
#include "SimplePaint/Geometry.h"
#include "PaintVertexShader.h"
#include "PaintPixelShader.h"
#include "OverlayVertexShader.h"
#include "OverlayPixelShader.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace
{
void Check(HRESULT result, const char* operation)
{
    if (FAILED(result))
        throw std::runtime_error(std::string(operation) + " failed (HRESULT " + std::to_string(static_cast<unsigned long>(result)) + ").");
}
D3D12_HEAP_PROPERTIES Heap(D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES result{};
    result.Type = type;
    result.CreationNodeMask = result.VisibleNodeMask = 1;
    return result;
}
D3D12_RESOURCE_DESC Buffer(size_t size)
{
    D3D12_RESOURCE_DESC result{};
    result.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    result.Width = size;
    result.Height = 1;
    result.DepthOrArraySize = result.MipLevels = 1;
    result.SampleDesc.Count = 1;
    result.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return result;
}
D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER result{};
    result.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    result.Transition.pResource = resource;
    result.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    result.Transition.StateBefore = before;
    result.Transition.StateAfter = after;
    return result;
}
}

Renderer::~Renderer()
{
    try { WaitIdle(); } catch (...) {}
    if (frameReady_) CloseHandle(frameReady_);
    if (fenceEvent_) CloseHandle(fenceEvent_);
}

void Renderer::Initialize(HWND window, bool forceSoftware, bool debugLayer)
{
    UINT factoryFlags = 0;
    if (debugLayer)
    {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
        {
            debug->EnableDebugLayer();
            factoryFlags = DXGI_CREATE_FACTORY_DEBUG;
        }
    }
    ComPtr<IDXGIFactory6> factory;
    Check(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory)), "Create DXGI factory");
    ComPtr<IDXGIAdapter1> selected;
    auto tryAdapter = [&](IDXGIAdapter1* adapter)
    {
        ComPtr<ID3D12Device> candidate;
        if (FAILED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&candidate)))) return false;
        D3D12_FEATURE_DATA_SHADER_MODEL model{D3D_SHADER_MODEL_6_0};
        if (FAILED(candidate->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model))) || model.HighestShaderModel < D3D_SHADER_MODEL_6_0) return false;
        device_ = candidate;
        selected = adapter;
        return true;
    };
    if (!forceSoftware)
    {
        for (UINT index = 0; ; ++index)
        {
            ComPtr<IDXGIAdapter1> adapter;
            HRESULT result = factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
            if (result == DXGI_ERROR_NOT_FOUND) break;
            Check(result, "Enumerate adapter");
            DXGI_ADAPTER_DESC1 description{};
            adapter->GetDesc1(&description);
            if (!(description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && tryAdapter(adapter.Get())) break;
        }
    }
    if (!device_)
    {
        ComPtr<IDXGIAdapter1> warp;
        Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "Find WARP adapter");
        if (!tryAdapter(warp.Get())) throw std::runtime_error("DirectX 12 with Shader Model 6.0 is unavailable on hardware and WARP.");
    }
    DXGI_ADAPTER_DESC1 description{};
    selected->GetDesc1(&description);
    char adapterText[256]{};
    WideCharToMultiByte(CP_UTF8, 0, description.Description, -1, adapterText, sizeof(adapterText), nullptr, nullptr);
    adapterName_ = adapterText;
    if (SUCCEEDED(device_.As(&infoQueue_))) infoQueue_->ClearStoredMessages();
    D3D12_COMMAND_QUEUE_DESC queueDescription{};
    queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Check(device_->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&queue_)), "Create command queue");
    BOOL supportsTearing = FALSE;
    if (SUCCEEDED(factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &supportsTearing, sizeof(supportsTearing)))) tearing_ = supportsTearing != FALSE;
    RECT client{};
    GetClientRect(window, &client);
    displayWidth_ = std::max(1L, client.right); width_=2560;
    displayHeight_ = std::max(1L, client.bottom); height_=1440;
    DXGI_SWAP_CHAIN_DESC1 swapDescription{};
    swapDescription.Width = displayWidth_;
    swapDescription.Height = displayHeight_;
    swapDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDescription.SampleDesc.Count = 1;
    swapDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDescription.BufferCount = 2;
    swapDescription.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapDescription.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | (tearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
    ComPtr<IDXGISwapChain1> swap;
    Check(factory->CreateSwapChainForHwnd(queue_.Get(), window, &swapDescription, nullptr, nullptr, &swap), "Create swap chain");
    Check(swap.As(&swapChain_), "Query swap chain");
    Check(factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER), "Disable automatic fullscreen");
    Check(swapChain_->SetMaximumFrameLatency(1), "Set maximum frame latency");
    frameReady_ = swapChain_->GetFrameLatencyWaitableObject();
    if (!frameReady_) throw std::runtime_error("DXGI frame latency wait handle unavailable.");
    D3D12_DESCRIPTOR_HEAP_DESC heapDescription{};
    heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDescription.NumDescriptors = 3;
    Check(device_->CreateDescriptorHeap(&heapDescription, IID_PPV_ARGS(&renderHeap_)), "Create render target heap");
    descriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heapDescription.NumDescriptors = 1;
    Check(device_->CreateDescriptorHeap(&heapDescription, IID_PPV_ARGS(&depthHeap_)), "Create depth heap");
    CreateTargets();
    for (auto& frame : frames_)
        Check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.allocator)), "Create command allocator");
    Check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frames_[0].allocator.Get(), nullptr, IID_PPV_ARGS(&commands_)), "Create command list");
    Check(commands_->Close(), "Close initial command list");
    Check(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "Create GPU fence");
    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_) throw std::runtime_error("Create GPU fence event failed.");

    D3D12_ROOT_PARAMETER parameters[3]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants = {0, 0, 24};
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[1].Descriptor.ShaderRegister = 1;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[2].Descriptor.ShaderRegister=2;
    parameters[2].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rootDescription{};
    rootDescription.NumParameters = 3;
    rootDescription.pParameters = parameters;
    rootDescription.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> signature, errors;
    Check(D3D12SerializeRootSignature(&rootDescription, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors), "Serialize root signature");
    Check(device_->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature_)), "Create root signature");
    D3D12_INPUT_ELEMENT_DESC paintLayout[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"MATERIAL",0,DXGI_FORMAT_R32_UINT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
    D3D12_INPUT_ELEMENT_DESC overlayLayout[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
    pipeline.pRootSignature = rootSignature_.Get();
    pipeline.VS = {PaintVertexShader, sizeof(PaintVertexShader)};
    pipeline.PS = {PaintPixelShader, sizeof(PaintPixelShader)};
    pipeline.InputLayout = {paintLayout, 3};
    pipeline.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pipeline.SampleMask = UINT_MAX;
    pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pipeline.RasterizerState.DepthClipEnable = TRUE;
    pipeline.DepthStencilState.DepthEnable = TRUE;
    pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    pipeline.DepthStencilState.StencilReadMask = pipeline.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline.NumRenderTargets = 1;
    pipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    pipeline.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pipeline.SampleDesc.Count = 1;
    Check(device_->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&paintPipeline_)), "Create SimplePaint pipeline");
    pipeline.VS = {OverlayVertexShader, sizeof(OverlayVertexShader)};
    pipeline.PS = {OverlayPixelShader, sizeof(OverlayPixelShader)};
    pipeline.InputLayout = {overlayLayout, 2};
    pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    Check(device_->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&gridPipeline_)), "Create grid pipeline");
    pipeline.DepthStencilState.DepthEnable = FALSE;
    Check(device_->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&cagePipeline_)), "Create control cage pipeline");
    pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    Check(device_->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&markerPipeline_)), "Create selection marker pipeline");
    const auto heap = Heap(D3D12_HEAP_TYPE_UPLOAD);
    const auto materialBuffer = Buffer(6144); // 2560 bytes paint + 512 bytes explicit surface kinds per frame.
    Check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &materialBuffer, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&materials_)), "Create material buffer");
    D3D12_RANGE noRead{0, 0};
    Check(materials_->Map(0, &noRead, reinterpret_cast<void**>(&mappedMaterials_)), "Map material buffer");
    CreateUi();
}

void Renderer::CreateTargets()
{
    auto handle = renderHeap_->GetCPUDescriptorHandleForHeapStart();
    D3D12_RENDER_TARGET_VIEW_DESC view{};
    view.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    for (unsigned index = 0; index < 2; ++index)
    {
        Check(swapChain_->GetBuffer(index, IID_PPV_ARGS(&targets_[index])), "Get swap chain buffer");
        device_->CreateRenderTargetView(targets_[index].Get(), &view, handle);
        handle.ptr += descriptorSize_;
    }
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width = width_;
    description.Height = height_;
    description.DepthOrArraySize = description.MipLevels = 1;
    description.Format = DXGI_FORMAT_D32_FLOAT;
    description.SampleDesc.Count = 1;
    description.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;
    description.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE colorClear{}; colorClear.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    colorClear.Color[0]=.025f; colorClear.Color[1]=.03f; colorClear.Color[2]=.04f; colorClear.Color[3]=1;
    const auto surfaceHeap=Heap(D3D12_HEAP_TYPE_DEFAULT);
    Check(device_->CreateCommittedResource(&surfaceHeap,D3D12_HEAP_FLAG_NONE,&description,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,&colorClear,IID_PPV_ARGS(&surface_)),"Create fixed 2560x1440 application surface");
    device_->CreateRenderTargetView(surface_.Get(),&view,handle);
    description.Format=DXGI_FORMAT_D32_FLOAT;
    description.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1;
    const auto heap = Heap(D3D12_HEAP_TYPE_DEFAULT);
    Check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&depth_)), "Create depth buffer");
    device_->CreateDepthStencilView(depth_.Get(), nullptr, depthHeap_->GetCPUDescriptorHandleForHeapStart());
}

void Renderer::WaitFence(uint64_t value)
{
    if (fence_->GetCompletedValue() < value)
    {
        Check(fence_->SetEventOnCompletion(value, fenceEvent_), "Wait for GPU completion");
        if (WaitForSingleObject(fenceEvent_, 10000) != WAIT_OBJECT_0)
            throw std::runtime_error("GPU fence wait timed out.");
    }
    Check(device_->GetDeviceRemovedReason(), "Check GPU device");
}
void Renderer::WaitIdle()
{
    if (!queue_ || !fence_) return;
    const auto value = nextFence_++;
    Check(queue_->Signal(fence_.Get(), value), "Signal GPU fence");
    WaitFence(value);
}
void Renderer::Resize(unsigned width, unsigned height)
{
    if(width!=2560 || height!=1440) throw std::invalid_argument("Veehiicuul3 has a fixed 2560x1440 render surface.");
    if (!width || !height || (width == width_ && height == height_)) return;
    WaitIdle();
    for (auto& target : targets_) target.Reset();
    depth_.Reset();
    width_ = width;
    height_ = height;
    DXGI_SWAP_CHAIN_DESC1 description{};
    Check(swapChain_->GetDesc1(&description), "Read swap chain description");
    Check(swapChain_->ResizeBuffers(2, width, height, description.Format, description.Flags), "Resize swap chain");
    CreateTargets();
}

bool Renderer::TestVisibility()
{
    const auto result=swapChain_->Present(0,DXGI_PRESENT_TEST);
    Check(result,"Test presentation visibility"); occluded_=result==DXGI_STATUS_OCCLUDED; return !occluded_;
}

void Renderer::CreateUi()
{
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};
    D3D12_ROOT_PARAMETER parameter{}; parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable={1,&range}; parameter.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR; sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_NEVER;
    sampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL; sampler.MaxLOD=D3D12_FLOAT32_MAX;
    D3D12_ROOT_SIGNATURE_DESC root{}; root.NumParameters=1; root.pParameters=&parameter; root.NumStaticSamplers=1; root.pStaticSamplers=&sampler;
    root.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> signature,error;
    Check(D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&error),"Serialize UI root");
    Check(device_->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&uiRoot_)),"Create UI root");
    D3D12_INPUT_ELEMENT_DESC layout[]={
        {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,16,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC p{}; p.pRootSignature=uiRoot_.Get(); p.VS={UiVertexShader,sizeof(UiVertexShader)}; p.PS={UiPixelShader,sizeof(UiPixelShader)};
    p.InputLayout={layout,3}; p.SampleMask=UINT_MAX; p.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID; p.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    p.RasterizerState.DepthClipEnable=TRUE; p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    p.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS; p.DepthStencilState.StencilReadMask=p.DepthStencilState.StencilWriteMask=D3D12_DEFAULT_STENCIL_READ_MASK;
    p.NumRenderTargets=1; p.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; p.SampleDesc.Count=1;
    auto& blend=p.BlendState.RenderTarget[0]; blend.BlendEnable=TRUE; blend.SrcBlend=D3D12_BLEND_SRC_ALPHA; blend.DestBlend=D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOp=D3D12_BLEND_OP_ADD; blend.SrcBlendAlpha=D3D12_BLEND_ONE; blend.DestBlendAlpha=D3D12_BLEND_INV_SRC_ALPHA; blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;
    blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    Check(device_->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&uiPipeline_)),"Create DX12 UI pipeline");
    p.VS={PresentVertexShader,sizeof(PresentVertexShader)}; p.PS={PresentPixelShader,sizeof(PresentPixelShader)}; p.InputLayout={nullptr,0}; blend.BlendEnable=FALSE;
    Check(device_->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&presentPipeline_)),"Create DX12 presentation pipeline");
    D3D12_DESCRIPTOR_HEAP_DESC heap{}; heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; heap.NumDescriptors=2; heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    Check(device_->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&textureHeap_)),"Create UI texture descriptors");
    D3D12_RESOURCE_DESC image{}; image.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; image.Width=image.Height=UiGpuData::AtlasSize;
    image.DepthOrArraySize=image.MipLevels=1; image.SampleDesc.Count=1; image.Format=DXGI_FORMAT_R8_UNORM;
    const auto textureHeap=Heap(D3D12_HEAP_TYPE_DEFAULT);
    Check(device_->CreateCommittedResource(&textureHeap,D3D12_HEAP_FLAG_NONE,&image,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&atlasTexture_)),"Create text atlas");
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format=DXGI_FORMAT_R8_UNORM; srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels=1;
    auto descriptor=textureHeap_->GetCPUDescriptorHandleForHeapStart(); device_->CreateShaderResourceView(atlasTexture_.Get(),&srv,descriptor);
    descriptor.ptr+=device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); srv.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    device_->CreateShaderResourceView(surface_.Get(),&srv,descriptor);
}
void Renderer::DrawUi(Frame& frame,const UiGpuData& ui)
{
    if(ui.revision!=atlasRevision_)
    {
        if(ui.atlas.size()!=UiGpuData::AtlasSize*UiGpuData::AtlasSize) throw std::invalid_argument("Wrong text atlas size.");
        if(!frame.atlasUpload)
        {
            const auto heap=Heap(D3D12_HEAP_TYPE_UPLOAD); const auto buffer=Buffer(ui.atlas.size());
            Check(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&frame.atlasUpload)),"Create text atlas upload");
        }
        void* mapped=nullptr; D3D12_RANGE noRead{0,0}; Check(frame.atlasUpload->Map(0,&noRead,&mapped),"Map text atlas upload");
        std::memcpy(mapped,ui.atlas.data(),ui.atlas.size()); frame.atlasUpload->Unmap(0,nullptr);
        auto barrier=Transition(atlasTexture_.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST); commands_->ResourceBarrier(1,&barrier);
        D3D12_TEXTURE_COPY_LOCATION source{},dest{}; source.pResource=frame.atlasUpload.Get(); source.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint.Footprint={DXGI_FORMAT_R8_UNORM,UiGpuData::AtlasSize,UiGpuData::AtlasSize,1,UiGpuData::AtlasSize};
        dest.pResource=atlasTexture_.Get(); dest.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        commands_->CopyTextureRegion(&dest,0,0,0,&source,nullptr);
        barrier=Transition(atlasTexture_.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE); commands_->ResourceBarrier(1,&barrier); atlasRevision_=ui.revision;
    }
    const size_t bytes=ui.vertices.size()*sizeof(UiVertex); if(!bytes) return;
    if(bytes>frame.uiCapacity)
    {
        frame.uiBuffer.Reset(); frame.uiCapacity=std::max(bytes,frame.uiCapacity*2);
        const auto heap=Heap(D3D12_HEAP_TYPE_UPLOAD);
        const auto buffer=Buffer(frame.uiCapacity);
        Check(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&frame.uiBuffer)),"Create UI vertex upload");
        D3D12_RANGE noRead{0,0}; Check(frame.uiBuffer->Map(0,&noRead,reinterpret_cast<void**>(&frame.uiMapped)),"Map UI vertices");
    }
    std::memcpy(frame.uiMapped,ui.vertices.data(),bytes);
    D3D12_VIEWPORT viewport{0,0,2560,1440,0,1}; D3D12_RECT clip{0,0,2560,1440}; commands_->RSSetViewports(1,&viewport); commands_->RSSetScissorRects(1,&clip);
    auto target=renderHeap_->GetCPUDescriptorHandleForHeapStart(); target.ptr+=2*descriptorSize_; commands_->OMSetRenderTargets(1,&target,FALSE,nullptr);
    commands_->SetGraphicsRootSignature(uiRoot_.Get()); commands_->SetPipelineState(uiPipeline_.Get());
    ID3D12DescriptorHeap* heaps[]={textureHeap_.Get()}; commands_->SetDescriptorHeaps(1,heaps);
    commands_->SetGraphicsRootDescriptorTable(0,textureHeap_->GetGPUDescriptorHandleForHeapStart());
    const D3D12_VERTEX_BUFFER_VIEW vertices{frame.uiBuffer->GetGPUVirtualAddress(),static_cast<UINT>(bytes),sizeof(UiVertex)};
    commands_->IASetVertexBuffers(0,1,&vertices); commands_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands_->DrawInstanced(static_cast<UINT>(ui.vertices.size()),1,0,0);
}
void Renderer::PresentSurface()
{
    const auto index=swapChain_->GetCurrentBackBufferIndex();
    auto barrier=Transition(targets_[index].Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET); commands_->ResourceBarrier(1,&barrier);
    auto target=renderHeap_->GetCPUDescriptorHandleForHeapStart(); target.ptr+=index*descriptorSize_;
    commands_->OMSetRenderTargets(1,&target,FALSE,nullptr);
    constexpr float black[4]={0,0,0,1}; commands_->ClearRenderTargetView(target,black,0,nullptr);
    D3D12_VIEWPORT viewport{static_cast<float>((displayWidth_-2560)/2),static_cast<float>((displayHeight_-1440)/2),2560,1440,0,1};
    D3D12_RECT clip{0,0,static_cast<LONG>(displayWidth_),static_cast<LONG>(displayHeight_)}; commands_->RSSetViewports(1,&viewport); commands_->RSSetScissorRects(1,&clip);
    commands_->SetGraphicsRootSignature(uiRoot_.Get()); commands_->SetPipelineState(presentPipeline_.Get());
    ID3D12DescriptorHeap* heaps[]={textureHeap_.Get()}; commands_->SetDescriptorHeaps(1,heaps);
    auto descriptor=textureHeap_->GetGPUDescriptorHandleForHeapStart(); descriptor.ptr+=device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    commands_->SetGraphicsRootDescriptorTable(0,descriptor); commands_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); commands_->DrawInstanced(3,1,0,0);
    barrier=Transition(targets_[index].Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PRESENT); commands_->ResourceBarrier(1,&barrier);
}

void Renderer::FillFrame(Frame& frame, const ViewGeometry& geometry)
{
    if (frame.revision == geometry.revision && frame.owner==&geometry) return;
    const size_t vertexBytes = geometry.vertices.size() * sizeof(PaintVertex);
    const size_t indexBytes = geometry.indices.size() * sizeof(uint32_t);
    const size_t gridBytes = geometry.grid.size() * sizeof(OverlayVertex);
    const size_t cageBytes = geometry.cage.size() * sizeof(OverlayVertex);
    const size_t markerBytes = geometry.markers.size() * sizeof(OverlayVertex);
    const size_t total = std::max(size_t(256), vertexBytes + indexBytes + gridBytes + cageBytes + markerBytes);
    if (total > UINT_MAX) throw std::runtime_error("Viewport geometry exceeds the buffer size limit.");
    if (total > frame.capacity)
    {
        frame.buffer.Reset();
        frame.capacity = std::max(total, frame.capacity * 2);
        const auto heap = Heap(D3D12_HEAP_TYPE_UPLOAD);
        const auto description = Buffer(frame.capacity);
        Check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&frame.buffer)), "Create viewport geometry buffer");
        D3D12_RANGE noRead{0, 0};
        Check(frame.buffer->Map(0, &noRead, reinterpret_cast<void**>(&frame.mapped)), "Map geometry buffer");
    }
    const auto address = frame.buffer->GetGPUVirtualAddress();
    size_t offset = 0;
    auto copy = [&](const void* data, size_t bytes) { if (bytes) std::memcpy(frame.mapped + offset, data, bytes); offset += bytes; };
    frame.vertices = {address, static_cast<UINT>(vertexBytes), sizeof(PaintVertex)};
    copy(geometry.vertices.data(), vertexBytes);
    frame.indices = {address + offset, static_cast<UINT>(indexBytes), DXGI_FORMAT_R32_UINT};
    copy(geometry.indices.data(), indexBytes);
    frame.grid = {address + offset, static_cast<UINT>(gridBytes), sizeof(OverlayVertex)};
    copy(geometry.grid.data(), gridBytes);
    frame.cage = {address + offset, static_cast<UINT>(cageBytes), sizeof(OverlayVertex)};
    copy(geometry.cage.data(), cageBytes);
    frame.markers = {address + offset, static_cast<UINT>(markerBytes), sizeof(OverlayVertex)};
    copy(geometry.markers.data(), markerBytes);
    frame.revision = geometry.revision;
    frame.owner=&geometry;
}

void Renderer::Render(const ViewGeometry& geometry, const Orthographic::ObjectTransforms& transforms,
                      std::span<const SimplePaint::GpuMaterial> materials,std::span<const std::array<float,4>> surfaces,const Orthographic::ObjectTransforms* vehicle,const DisplayPresentProbe* probe,const UiGpuData* ui,Ui::Rect view,bool present)
{
    const unsigned index = swapChain_->GetCurrentBackBufferIndex();
    auto& frame = frames_[index];
    WaitFence(frame.fence);
    FillFrame(frame, geometry);
    if (materials.size() > 32) throw std::invalid_argument("Too many viewport materials.");
    if(surfaces.size()>32) throw std::invalid_argument("Too many surface materials.");
    if (!materials.empty()) std::memcpy(mappedMaterials_ + 3072 * index, materials.data(), materials.size_bytes());
    std::memset(mappedMaterials_+3072*index+2560,0,512);
    if(!surfaces.empty()) std::memcpy(mappedMaterials_+3072*index+2560,surfaces.data(),surfaces.size_bytes());
    Check(frame.allocator->Reset(), "Reset command allocator");
    Check(commands_->Reset(frame.allocator.Get(), paintPipeline_.Get()), "Reset command list");
    auto barrier = Transition(surface_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    commands_->ResourceBarrier(1, &barrier);
    D3D12_VIEWPORT viewport{view.x,view.y,view.w,view.h,0,1};
    D3D12_RECT rectangle{static_cast<LONG>(view.x),static_cast<LONG>(view.y),static_cast<LONG>(view.x+view.w),static_cast<LONG>(view.y+view.h)};
    commands_->RSSetViewports(1, &viewport);
    commands_->RSSetScissorRects(1, &rectangle);
    auto target = renderHeap_->GetCPUDescriptorHandleForHeapStart();
    target.ptr += descriptorSize_ * 2;
    const auto depth = depthHeap_->GetCPUDescriptorHandleForHeapStart();
    const std::array<float,4> canvas{.025f,.03f,.04f,1};
    commands_->ClearRenderTargetView(target,canvas.data(),0,nullptr);
    commands_->ClearDepthStencilView(depth, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
    commands_->OMSetRenderTargets(1, &target, FALSE, &depth);
    commands_->SetGraphicsRootSignature(rootSignature_.Get());
    commands_->SetGraphicsRoot32BitConstants(0, 24, &transforms, 0);
    commands_->SetGraphicsRootConstantBufferView(1, materials_->GetGPUVirtualAddress() + 3072 * index);
    commands_->SetGraphicsRootConstantBufferView(2,materials_->GetGPUVirtualAddress()+3072*index+2560);
    if (!geometry.indices.empty())
    {
        commands_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commands_->IASetVertexBuffers(0, 1, &frame.vertices);
        commands_->IASetIndexBuffer(&frame.indices);
        const auto staticCount=vehicle ? geometry.vehicleFirstIndex : static_cast<UINT>(geometry.indices.size());
        if(staticCount) commands_->DrawIndexedInstanced(staticCount,1,0,0,0);
        if(vehicle && geometry.vehicleIndexCount)
        {
            commands_->SetGraphicsRoot32BitConstants(0,24,vehicle,0);
            commands_->DrawIndexedInstanced(geometry.vehicleIndexCount,1,geometry.vehicleFirstIndex,0,0);
            commands_->SetGraphicsRoot32BitConstants(0,24,&transforms,0);
        }
    }
    auto drawOverlay = [&](ID3D12PipelineState* pipeline, const D3D12_VERTEX_BUFFER_VIEW& buffer, size_t count, D3D_PRIMITIVE_TOPOLOGY topology)
    {
        if (!count) return;
        commands_->SetPipelineState(pipeline);
        commands_->IASetPrimitiveTopology(topology);
        commands_->IASetVertexBuffers(0, 1, &buffer);
        commands_->DrawInstanced(static_cast<UINT>(count), 1, 0, 0);
    };
    drawOverlay(gridPipeline_.Get(), frame.grid, geometry.grid.size(), D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    drawOverlay(cagePipeline_.Get(), frame.cage, geometry.cage.size(), D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    drawOverlay(markerPipeline_.Get(), frame.markers, geometry.markers.size(), D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    if(ui) DrawUi(frame,*ui);
    barrier = Transition(surface_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    commands_->ResourceBarrier(1, &barrier);
    if(present) PresentSurface();
    Check(commands_->Close(), "Close frame commands");
    ID3D12CommandList* lists[] = {commands_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    if(present && probe)
    {
        auto& measurement=*probe->frame; measurement.thread=GetCurrentThreadId(); measurement.swapChain=reinterpret_cast<std::uint64_t>(swapChain_.Get());
        LARGE_INTEGER counter{}; QueryPerformanceCounter(&counter); measurement.clockFirstQpc=static_cast<std::uint64_t>(counter.QuadPart);
        measurement.gameInputTime=probe->clock(probe->context);
        QueryPerformanceCounter(&counter); measurement.clockLastQpc=static_cast<std::uint64_t>(counter.QuadPart);
        QueryPerformanceCounter(&counter); measurement.presentFirstQpc=static_cast<std::uint64_t>(counter.QuadPart);
        const auto result=swapChain_->Present(0,tearing_ ? DXGI_PRESENT_ALLOW_TEARING : 0);
        QueryPerformanceCounter(&counter); measurement.presentEndQpc=static_cast<std::uint64_t>(counter.QuadPart); measurement.accepted=result==S_OK;
        Check(result,"Present frame");
    }
    else if(present) { const auto result=swapChain_->Present(0, tearing_ ? DXGI_PRESENT_ALLOW_TEARING : 0); occluded_=result==DXGI_STATUS_OCCLUDED; Check(result,"Present frame"); }
    if(present) ++presents_;
    frame.fence = nextFence_++;
    Check(queue_->Signal(fence_.Get(), frame.fence), "Signal frame completion");
    lastFrame_ = index;
}

unsigned Renderer::DebugErrors() const
{
    if (!infoQueue_) return 0;
    unsigned errors = 0;
    for (UINT64 index = 0; index < infoQueue_->GetNumStoredMessagesAllowedByRetrievalFilter(); ++index)
    {
        SIZE_T size = 0;
        infoQueue_->GetMessage(index, nullptr, &size);
        std::vector<std::byte> storage(size);
        auto message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        if (SUCCEEDED(infoQueue_->GetMessage(index, message, &size)) && message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING) ++errors;
    }
    return errors;
}

std::string Renderer::DebugMessages() const
{
    std::string output; if(!infoQueue_) return "Debug layer unavailable.";
    for(UINT64 i=0;i<infoQueue_->GetNumStoredMessagesAllowedByRetrievalFilter();++i)
    {
        SIZE_T size=0; infoQueue_->GetMessage(i,nullptr,&size); std::vector<std::byte> storage(size); auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        if(SUCCEEDED(infoQueue_->GetMessage(i,message,&size)) && message->Severity<=D3D12_MESSAGE_SEVERITY_WARNING) { output.append(message->pDescription,message->DescriptionByteLength); output.push_back('\n'); }
    }
    return output;
}

void Renderer::Capture(const std::filesystem::path& path,bool presented)
{
    WaitIdle();
    auto& frame = frames_[lastFrame_];
    auto* resource=presented ? targets_[lastFrame_].Get() : surface_.Get();
    const auto initial=presented ? D3D12_RESOURCE_STATE_PRESENT : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    const auto description = resource->GetDesc();
    const auto captureWidth=static_cast<unsigned>(description.Width),captureHeight=description.Height;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 bytes = 0;
    device_->GetCopyableFootprints(&description, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
    ComPtr<ID3D12Resource> readback;
    const auto heap = Heap(D3D12_HEAP_TYPE_READBACK);
    const auto buffer = Buffer(static_cast<size_t>(bytes));
    Check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)), "Create screenshot readback");
    Check(frame.allocator->Reset(), "Reset capture allocator");
    Check(commands_->Reset(frame.allocator.Get(), nullptr), "Reset capture commands");
    auto barrier = Transition(resource, initial, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commands_->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION source{}, destination{};
    source.pResource = resource;
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.pResource = readback.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprint;
    commands_->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    barrier = Transition(resource, D3D12_RESOURCE_STATE_COPY_SOURCE, initial);
    commands_->ResourceBarrier(1, &barrier);
    Check(commands_->Close(), "Close capture commands");
    ID3D12CommandList* lists[] = {commands_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    WaitIdle();
    std::byte* pixels = nullptr;
    D3D12_RANGE range{0, static_cast<SIZE_T>(bytes)};
    Check(readback->Map(0, &range, reinterpret_cast<void**>(&pixels)), "Map screenshot");
    std::vector<unsigned char> output(static_cast<size_t>(captureWidth) * captureHeight * 4);
    for (unsigned y = 0; y < captureHeight; ++y)
        for (unsigned x = 0; x < captureWidth; ++x)
        {
            const auto input = reinterpret_cast<const unsigned char*>(pixels) + footprint.Offset + y * footprint.Footprint.RowPitch + x * 4;
            const auto pixel = output.data() + (static_cast<size_t>(captureHeight - 1 - y) * captureWidth + x) * 4;
            pixel[0] = input[2]; pixel[1] = input[1]; pixel[2] = input[0]; pixel[3] = 255;
        }
    D3D12_RANGE noWrite{0, 0};
    readback->Unmap(0, &noWrite);
    BITMAPFILEHEADER file{};
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER);
    file.bfSize = file.bfOffBits + static_cast<DWORD>(output.size());
    BITMAPINFOHEADER image{};
    image.biSize = sizeof(image);
    image.biWidth = static_cast<LONG>(captureWidth);
    image.biHeight = static_cast<LONG>(captureHeight);
    image.biPlanes = 1;
    image.biBitCount = 32;
    image.biCompression = BI_RGB;
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(&file), sizeof(file));
    stream.write(reinterpret_cast<const char*>(&image), sizeof(image));
    stream.write(reinterpret_cast<const char*>(output.data()), static_cast<std::streamsize>(output.size()));
    if (!stream) throw std::runtime_error("Could not write viewport capture.");
}
