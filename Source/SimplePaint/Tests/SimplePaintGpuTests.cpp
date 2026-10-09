#include "SimplePaintReference.h"
#include "SimplePaint/OrthographicTransforms.h"
#include "SimplePaintVS.h"
#include "SimplePaintPS.h"
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <string>
#include <string_view>

using Microsoft::WRL::ComPtr;
using PaintTest::Require;
namespace
{
void Check(HRESULT result, const char* operation)
{
    if (FAILED(result)) throw std::runtime_error(std::string(operation)+" failed: "+std::to_string(result));
}
struct alignas(256) Constants
{
    Orthographic::ObjectTransforms object{};
    std::array<std::byte,160> objectPadding{};
    std::array<SimplePaint::GpuMaterial,6> materials{};
    std::array<std::byte,32> tailPadding{};
};
static_assert(sizeof(Constants)==768);
struct Vertex { float x,y,z,nx,ny,nz; std::uint32_t material; };
static_assert(sizeof(Vertex)==28);

ComPtr<ID3D12Resource> Buffer(ID3D12Device* device, UINT64 size, D3D12_HEAP_TYPE type)
{
    const D3D12_HEAP_PROPERTIES heap{.Type = type};
    const D3D12_RESOURCE_DESC desc{
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Width = size,
        .Height = 1,
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .SampleDesc = {.Count = 1},
        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
    };
    ComPtr<ID3D12Resource> buffer;
    Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
        type==D3D12_HEAP_TYPE_READBACK ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,IID_PPV_ARGS(&buffer)),"Create buffer");
    return buffer;
}
void Upload(ID3D12Resource* buffer, const void* data, std::size_t size)
{
    void* mapped=nullptr; const D3D12_RANGE noRead{.Begin = 0, .End = 0};
    Check(buffer->Map(0,&noRead,&mapped),"Map upload");
    std::memcpy(mapped,data,size); buffer->Unmap(0,nullptr);
}
}

int main(int argc, char** argv)
{
    try
    {
        // Both runs use the real production VS/PS and root bindings. The WARP
        // run is reproducible on machines without a discrete graphics adapter.
        const bool warp=argc>1 && std::string_view(argv[1])=="--warp";
        const bool benchmark=argc>1 && std::string_view(argv[1])=="--benchmark";
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
        ComPtr<IDXGIFactory6> factory;
        Check(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)),"Create factory");
        ComPtr<IDXGIAdapter1> adapter;
        if (warp) Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)),"WARP adapter");
        else Check(factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(&adapter)),"Hardware adapter");
        DXGI_ADAPTER_DESC1 adapterDesc{}; Check(adapter->GetDesc1(&adapterDesc),"Adapter description");
        std::wcout << L"Adapter: " << adapterDesc.Description << L'\n';
        ComPtr<ID3D12Device> device;
        Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"Create device");
        ComPtr<ID3D12InfoQueue> info;
        device.As(&info);

        D3D12_ROOT_PARAMETER parameters[2]{};
        for (unsigned i=0;i<2;++i)
        {
            parameters[i] = {
                .ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV,
                .Descriptor = {.ShaderRegister = i},
            };
        }
        const D3D12_ROOT_SIGNATURE_DESC rootDesc{
            .NumParameters = 2,
            .pParameters = parameters,
            .NumStaticSamplers = 0,
            .pStaticSamplers = nullptr,
            .Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
        };
        ComPtr<ID3DBlob> blob,error;
        Check(D3D12SerializeRootSignature(&rootDesc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"Serialize root");
        ComPtr<ID3D12RootSignature> root;
        Check(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)),"Create root");
        const D3D12_INPUT_ELEMENT_DESC elements[]{
            {.SemanticName = "POSITION", .SemanticIndex = 0,
                .Format = DXGI_FORMAT_R32G32B32_FLOAT, .InputSlot = 0, .AlignedByteOffset = 0,
                .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, .InstanceDataStepRate = 0},
            {.SemanticName = "NORMAL", .SemanticIndex = 0,
                .Format = DXGI_FORMAT_R32G32B32_FLOAT, .InputSlot = 0, .AlignedByteOffset = 12,
                .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, .InstanceDataStepRate = 0},
            {.SemanticName = "MATERIAL", .SemanticIndex = 0,
                .Format = DXGI_FORMAT_R32_UINT, .InputSlot = 0, .AlignedByteOffset = 24,
                .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, .InstanceDataStepRate = 0}};
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc{
            .pRootSignature = root.Get(),
            .VS = {.pShaderBytecode = g_simplePaintVertexShader, .BytecodeLength = sizeof(g_simplePaintVertexShader)},
            .PS = {.pShaderBytecode = g_simplePaintPixelShader, .BytecodeLength = sizeof(g_simplePaintPixelShader)},
            .BlendState = {
                .RenderTarget = {{.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL}},
            },
            .SampleMask = UINT_MAX,
            .RasterizerState = {
                .FillMode = D3D12_FILL_MODE_SOLID,
                .CullMode = D3D12_CULL_MODE_NONE,
                .DepthClipEnable = TRUE,
            },
            .InputLayout = {.pInputElementDescs = elements, .NumElements = 3},
            .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
            .NumRenderTargets = 1,
            .RTVFormats = {DXGI_FORMAT_R32G32B32A32_FLOAT},
            .SampleDesc = {.Count = 1},
        };
        ComPtr<ID3D12PipelineState> pipeline;
        Check(device->CreateGraphicsPipelineState(&pipelineDesc,IID_PPV_ARGS(&pipeline)),"Create production PSO");

        auto cases=PaintTest::Cases();
        if (benchmark)
        {
            cases = {
                {.parameters = {}, .normal = {0.6f,0.2f,0.8f}},
                {.parameters = {.shift = 0.6}, .normal = {0.6f,0.2f,0.8f}},
            };
        }
        std::vector<Constants> constants(cases.size());
        std::vector<Vertex> vertices; vertices.reserve(cases.size()*3);
        for (std::size_t i=0;i<cases.size();++i)
        {
            constants[i].object=Orthographic::BuildObjectTransforms(DirectX::XMMatrixIdentity(),
                Orthographic::MakeProjection(2,2,0,1));
            const auto index=static_cast<unsigned>(i%6);
            constants[i].materials[index]=SimplePaint::Material::Compile(cases[i].parameters).Constants();
            const auto n=cases[i].normal;
            for (const auto position : {std::array{-1.0f,-1.0f},std::array{3.0f,-1.0f},std::array{-1.0f,3.0f}})
                vertices.push_back({
                    .x = position[0], .y = position[1], .z = 0,
                    .nx = n[0], .ny = n[1], .nz = n[2], .material = index,
                });
            if (!benchmark && i>=cases.size()-4096)
            {
                // Pixel-center barycentrics are (1/2,1/4,1/4). Distinct vertex
                // normals exercise interpolation and the VS rotation together.
                auto* triangle=vertices.data()+i*3;
                const float delta=0.125f*std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
                triangle[0].nx+=delta; triangle[1].nx-=2*delta;
                triangle[0].ny-=delta; triangle[2].ny+=2*delta;
                cases[i].normal={float(0.5*triangle[0].nx+0.25*triangle[1].nx+0.25*triangle[2].nx),
                    float(0.5*triangle[0].ny+0.25*triangle[1].ny+0.25*triangle[2].ny),n[2]};
            }
        }
        const auto cb=Buffer(device.Get(),constants.size()*sizeof(Constants),D3D12_HEAP_TYPE_UPLOAD);
        Upload(cb.Get(),constants.data(),constants.size()*sizeof(Constants));
        const auto vb=Buffer(device.Get(),vertices.size()*sizeof(Vertex),D3D12_HEAP_TYPE_UPLOAD);
        Upload(vb.Get(),vertices.data(),vertices.size()*sizeof(Vertex));
        const D3D12_VERTEX_BUFFER_VIEW vertexView{
            .BufferLocation = vb->GetGPUVirtualAddress(),
            .SizeInBytes = static_cast<UINT>(vertices.size()*sizeof(Vertex)),
            .StrideInBytes = sizeof(Vertex),
        };

        const UINT width=benchmark ? 1024 : 256;
        const UINT height=benchmark ? 1024 : static_cast<UINT>((cases.size()+width-1)/width);
        const D3D12_RESOURCE_DESC textureDesc{
            .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
            .Width = width,
            .Height = height,
            .DepthOrArraySize = 1,
            .MipLevels = 1,
            .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
            .SampleDesc = {.Count = 1},
            .Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
        };
        const D3D12_HEAP_PROPERTIES defaultHeap{.Type = D3D12_HEAP_TYPE_DEFAULT};
        ComPtr<ID3D12Resource> target;
        Check(device->CreateCommittedResource(&defaultHeap,D3D12_HEAP_FLAG_NONE,&textureDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&target)),"Create target");
        const D3D12_DESCRIPTOR_HEAP_DESC heapDesc{
            .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
            .NumDescriptors = 1,
        };
        ComPtr<ID3D12DescriptorHeap> rtvHeap;
        Check(device->CreateDescriptorHeap(&heapDesc,IID_PPV_ARGS(&rtvHeap)),"Create RTV heap");
        const auto rtv=rtvHeap->GetCPUDescriptorHandleForHeapStart();
        device->CreateRenderTargetView(target.Get(),nullptr,rtv);
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT64 readSize=0;
        device->GetCopyableFootprints(&textureDesc,0,1,0,&footprint,nullptr,nullptr,&readSize);
        const auto readback=Buffer(device.Get(),readSize,D3D12_HEAP_TYPE_READBACK);

        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        ComPtr<ID3D12CommandQueue> queue;
        Check(device->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&queue)),"Create queue");
        ComPtr<ID3D12CommandAllocator> allocator;
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"Create allocator");
        ComPtr<ID3D12GraphicsCommandList> list;
        Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),pipeline.Get(),IID_PPV_ARGS(&list)),"Create list");
        ComPtr<ID3D12QueryHeap> timestamps;
        ComPtr<ID3D12Resource> timestampReadback;
        constexpr UINT repetitions=128;
        if (benchmark)
        {
            const D3D12_QUERY_HEAP_DESC queryDesc{.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP, .Count = 4, .NodeMask = 0};
            Check(device->CreateQueryHeap(&queryDesc,IID_PPV_ARGS(&timestamps)),"Create timestamp queries");
            timestampReadback=Buffer(device.Get(),4*sizeof(UINT64),D3D12_HEAP_TYPE_READBACK);
        }
        list->SetGraphicsRootSignature(root.Get()); list->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
        list->IASetVertexBuffers(0,1,&vertexView); list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        for (std::size_t i=0;i<cases.size();++i)
        {
            const auto x=static_cast<LONG>(i%width), y=static_cast<LONG>(i/width);
            const D3D12_VIEWPORT viewport{
                .TopLeftX = benchmark ? 0.0f : float(x),
                .TopLeftY = benchmark ? 0.0f : float(y),
                .Width = benchmark ? float(width) : 1.0f,
                .Height = benchmark ? float(height) : 1.0f,
                .MinDepth = 0,
                .MaxDepth = 1,
            };
            const D3D12_RECT scissor{
                .left = benchmark ? 0 : x,
                .top = benchmark ? 0 : y,
                .right = benchmark ? LONG(width) : x+1,
                .bottom = benchmark ? LONG(height) : y+1,
            };
            list->RSSetViewports(1,&viewport); list->RSSetScissorRects(1,&scissor);
            list->SetGraphicsRootConstantBufferView(0,cb->GetGPUVirtualAddress()+i*sizeof(Constants));
            list->SetGraphicsRootConstantBufferView(1,cb->GetGPUVirtualAddress()+i*sizeof(Constants)+256);
            if (benchmark)
            {
                // Warm each path, then time production draws on the GPU; exclude
                // CPU submission, resource setup, transfers, and readback.
                for (UINT j=0;j<32;++j) list->DrawInstanced(3,1,static_cast<UINT>(i*3),0);
                list->EndQuery(timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,static_cast<UINT>(i*2));
            }
            for (UINT j=0;j<(benchmark ? repetitions : 1u);++j)
                list->DrawInstanced(3,1,static_cast<UINT>(i*3),0);
            if (benchmark) list->EndQuery(timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,static_cast<UINT>(i*2+1));
        }
        if (benchmark) list->ResolveQueryData(timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,4,timestampReadback.Get(),0);
        const D3D12_RESOURCE_BARRIER barrier{
            .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
            .Transition = {
                .pResource = target.Get(),
                .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                .StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET,
                .StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE,
            },
        };
        list->ResourceBarrier(1,&barrier);
        const D3D12_TEXTURE_COPY_LOCATION source{
            .pResource = target.Get(),
            .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
            .SubresourceIndex = 0,
        };
        const D3D12_TEXTURE_COPY_LOCATION destination{
            .pResource = readback.Get(),
            .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
            .PlacedFootprint = footprint,
        };
        list->CopyTextureRegion(&destination,0,0,0,&source,nullptr);
        Check(list->Close(),"Close list");
        ID3D12CommandList* lists[]{list.Get()}; queue->ExecuteCommandLists(1,lists);
        ComPtr<ID3D12Fence> fence;
        Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"Create fence");
        Check(queue->Signal(fence.Get(),1),"Signal fence");
        const HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        Require(event!=nullptr,"Create fence event failed");
        Check(fence->SetEventOnCompletion(1,event),"Arm fence");
        const DWORD wait=WaitForSingleObject(event,60000); CloseHandle(event);
        Require(wait==WAIT_OBJECT_0,"GPU test fence timeout");

        if (benchmark)
        {
            void* ticksData=nullptr; const D3D12_RANGE ticksRange{.Begin = 0, .End = 4*sizeof(UINT64)};
            Check(timestampReadback->Map(0,&ticksRange,&ticksData),"Map timestamps");
            const auto* ticks=static_cast<const UINT64*>(ticksData);
            UINT64 frequency=0; Check(queue->GetTimestampFrequency(&frequency),"Timestamp frequency");
            for (unsigned i=0;i<2;++i)
                std::cout << (i==0 ? "Zero shift" : "Shift 0.6") << ": " <<
                    1000.0*double(ticks[i*2+1]-ticks[i*2])/double(frequency)/repetitions <<
                    " ms per 1024x1024 draw (128 draws, constant normal, float4 RTV).\n";
            const D3D12_RANGE noWrite{.Begin = 0, .End = 0}; timestampReadback->Unmap(0,&noWrite);
            return 0;
        }

        void* mapped=nullptr; const D3D12_RANGE readRange{.Begin = 0, .End = static_cast<SIZE_T>(readSize)};
        Check(readback->Map(0,&readRange,&mapped),"Read pixels");
        double maxLinear=0,maxSrgb=0;
        for (std::size_t i=0;i<cases.size();++i)
        {
            const auto* pixel=reinterpret_cast<const float*>(static_cast<const std::byte*>(mapped)+
                footprint.Offset+(i/width)*footprint.Footprint.RowPitch+(i%width)*16);
            const auto reference=PaintTest::Reference(cases[i]);
            Require(pixel[3]==1.0f,"Production pixel shader alpha mismatch or missing draw");
            for (unsigned c=0;c<3;++c)
            {
                if (!(std::isfinite(pixel[c]) && pixel[c]>=0 && pixel[c]<=1))
                {
                    std::cerr << "case=" << i << " channel=" << c << " value=" << std::setprecision(17) << pixel[c] << '\n';
                    throw std::runtime_error("GPU output is not finite in [0,1]");
                }
                const double linearError=std::abs(pixel[c]-reference[c]);
                const double srgbError=std::abs(PaintTest::Srgb(pixel[c])-PaintTest::Srgb(reference[c]));
                maxLinear=std::max(maxLinear,linearError); maxSrgb=std::max(maxSrgb,srgbError);
                if (linearError>0.001 || srgbError>0.001)
                    throw std::runtime_error("GPU reference mismatch at case "+std::to_string(i)+
                        ", linear error="+std::to_string(linearError)+", sRGB error="+std::to_string(srgbError));
            }
        }
        const D3D12_RANGE noWrite{.Begin = 0, .End = 0}; readback->Unmap(0,&noWrite);
        if (info)
            for (UINT64 i=0;i<info->GetNumStoredMessages();++i)
            {
                SIZE_T size=0; Check(info->GetMessage(i,nullptr,&size),"Get debug message size");
                std::vector<std::byte> storage(size); auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());
                Check(info->GetMessage(i,message,&size),"Get debug message");
                if (message->Severity<=D3D12_MESSAGE_SEVERITY_WARNING)
                    throw std::runtime_error(std::string("DX12 validation: ")+message->pDescription);
            }
        std::cout << cases.size() << " production VS/PS cases passed; max linear error=" << maxLinear
            << ", max sRGB error=" << maxSrgb << "; debug layer=" << (info ? "checked" : "unavailable") << '\n';
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
