// Copyright (C) 2017-2024 Intel Corporation
// Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved
// SPDX-License-Identifier: MIT

#include "Debug.hpp"
#include "PresentMonTraceConsumer.hpp"
#include "ProviderConfiguration.h"
#include "NvidiaTraceConsumer.hpp"

#include "ETW/Microsoft_Windows_D3D9.h"
#include "ETW/Microsoft_Windows_Direct3D12.h"
#include "ETW/Microsoft_Windows_Dwm_Core.h"
#include "ETW/Microsoft_Windows_Dwm_Core_Win7.h"
#include "ETW/Microsoft_Windows_DXGI.h"
#include "ETW/Microsoft_Windows_DxgKrnl.h"
#include "ETW/Microsoft_Windows_DxgKrnl_Win7.h"
#include "ETW/Microsoft_Windows_EventMetadata.h"
#include "ETW/Microsoft_Windows_Kernel_Process.h"
#include "ETW/Microsoft_Windows_Win32k.h"
#include "ETW/NT_Process.h"
#include "ETW/Intel_PresentMon.h"
#include "ETW/NV_DD.h"
#include "ETW/Nvidia_PCL.h"
#include <format>

namespace {
struct TraceProperties : public EVENT_TRACE_PROPERTIES {
    wchar_t mSessionName[MAX_PATH];
};

template<typename T> void PatchKeyword(uint64_t*) {}
template<typename T> void PatchPreWin11Keyword(uint64_t*) {}

// Win11 changed some Microsoft-Windows-Dwm-Core event kewords from Composition to Scheduling:
template<> void PatchPreWin11Keyword<Microsoft_Windows_Dwm_Core::SCHEDULE_PRESENT_Start>     (uint64_t* k) { *k = (*k & ~(uint64_t) Microsoft_Windows_Dwm_Core::Keyword::Scheduling) | (uint64_t) Microsoft_Windows_Dwm_Core::Keyword::Composition; }
template<> void PatchPreWin11Keyword<Microsoft_Windows_Dwm_Core::SCHEDULE_SURFACEUPDATE_Info>(uint64_t* k) { *k = (*k & ~(uint64_t) Microsoft_Windows_Dwm_Core::Keyword::Scheduling) | (uint64_t) Microsoft_Windows_Dwm_Core::Keyword::Composition; }

// Win11 added a Present keyword to some Microsoft-Windows-DxgKrnl events:
template<> void PatchPreWin11Keyword<Microsoft_Windows_DxgKrnl::BlitCancel_Info>               (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Present; }
template<> void PatchPreWin11Keyword<Microsoft_Windows_DxgKrnl::Blit_Info>                     (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Present; }
template<> void PatchPreWin11Keyword<Microsoft_Windows_DxgKrnl::FlipMultiPlaneOverlay_Info>    (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Present; }
template<> void PatchPreWin11Keyword<Microsoft_Windows_DxgKrnl::Flip_Info>                     (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Present; }
template<> void PatchPreWin11Keyword<Microsoft_Windows_DxgKrnl::HSyncDPCMultiPlane_Info>       (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Present; }
template<> void PatchPreWin11Keyword<Microsoft_Windows_DxgKrnl::MMIOFlipMultiPlaneOverlay_Info>(uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Present; }
template<> void PatchPreWin11Keyword<Microsoft_Windows_DxgKrnl::MMIOFlip_Info>                 (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Present; }
template<> void PatchPreWin11Keyword<Microsoft_Windows_DxgKrnl::PresentHistoryDetailed_Start>  (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Present; }
template<> void PatchPreWin11Keyword<Microsoft_Windows_DxgKrnl::PresentHistory_Info>           (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Present; }
template<> void PatchPreWin11Keyword<Microsoft_Windows_DxgKrnl::PresentHistory_Start>          (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Present; }
template<> void PatchPreWin11Keyword<Microsoft_Windows_DxgKrnl::VSyncDPCMultiPlane_Info>       (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Present; }
template<> void PatchPreWin11Keyword<Microsoft_Windows_DxgKrnl::VSyncDPC_Info>                 (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Present; }

// Never filter DxgKrnl events using the Performance keyword, as that can have side-effects with
// negative performance impact on some versions of Windows.
template<> void PatchKeyword<Microsoft_Windows_DxgKrnl::PresentHistory_Start>           (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Microsoft_Windows_DxgKrnl_Performance; }
template<> void PatchKeyword<Microsoft_Windows_DxgKrnl::Blit_Info>                      (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Microsoft_Windows_DxgKrnl_Performance; }
template<> void PatchKeyword<Microsoft_Windows_DxgKrnl::BlitCancel_Info>                (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Microsoft_Windows_DxgKrnl_Performance; }
template<> void PatchKeyword<Microsoft_Windows_DxgKrnl::Flip_Info>                      (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Microsoft_Windows_DxgKrnl_Performance; }
template<> void PatchKeyword<Microsoft_Windows_DxgKrnl::IndependentFlip_Info>           (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Microsoft_Windows_DxgKrnl_Performance; }
template<> void PatchKeyword<Microsoft_Windows_DxgKrnl::FlipMultiPlaneOverlay_Info>     (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Microsoft_Windows_DxgKrnl_Performance; }
template<> void PatchKeyword<Microsoft_Windows_DxgKrnl::HSyncDPCMultiPlane_Info>        (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Microsoft_Windows_DxgKrnl_Performance; }
template<> void PatchKeyword<Microsoft_Windows_DxgKrnl::VSyncDPCMultiPlane_Info>        (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Microsoft_Windows_DxgKrnl_Performance; }
template<> void PatchKeyword<Microsoft_Windows_DxgKrnl::MMIOFlip_Info>                  (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Microsoft_Windows_DxgKrnl_Performance; }
template<> void PatchKeyword<Microsoft_Windows_DxgKrnl::MMIOFlipMultiPlaneOverlay_Info> (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Microsoft_Windows_DxgKrnl_Performance; }
template<> void PatchKeyword<Microsoft_Windows_DxgKrnl::Present_Info>                   (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Microsoft_Windows_DxgKrnl_Performance; }
template<> void PatchKeyword<Microsoft_Windows_DxgKrnl::PresentHistory_Info>            (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Microsoft_Windows_DxgKrnl_Performance; }
template<> void PatchKeyword<Microsoft_Windows_DxgKrnl::PresentHistoryDetailed_Start>   (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Microsoft_Windows_DxgKrnl_Performance; }
template<> void PatchKeyword<Microsoft_Windows_DxgKrnl::QueuePacket_Start>              (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Microsoft_Windows_DxgKrnl_Performance; }
template<> void PatchKeyword<Microsoft_Windows_DxgKrnl::QueuePacket_Start_2>            (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Microsoft_Windows_DxgKrnl_Performance; }
template<> void PatchKeyword<Microsoft_Windows_DxgKrnl::QueuePacket_Stop>               (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Microsoft_Windows_DxgKrnl_Performance; }
template<> void PatchKeyword<Microsoft_Windows_DxgKrnl::VSyncDPC_Info>                  (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Microsoft_Windows_DxgKrnl_Performance; }
template<> void PatchKeyword<Microsoft_Windows_DxgKrnl::Context_DCStart>                (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Microsoft_Windows_DxgKrnl_Performance; }
template<> void PatchKeyword<Microsoft_Windows_DxgKrnl::Context_Start>                  (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Microsoft_Windows_DxgKrnl_Performance; }
template<> void PatchKeyword<Microsoft_Windows_DxgKrnl::Context_Stop>                   (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Microsoft_Windows_DxgKrnl_Performance; }
template<> void PatchKeyword<Microsoft_Windows_DxgKrnl::Device_DCStart>                 (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Microsoft_Windows_DxgKrnl_Performance; }
template<> void PatchKeyword<Microsoft_Windows_DxgKrnl::Device_Start>                   (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Microsoft_Windows_DxgKrnl_Performance; }
template<> void PatchKeyword<Microsoft_Windows_DxgKrnl::Device_Stop>                    (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Microsoft_Windows_DxgKrnl_Performance; }
template<> void PatchKeyword<Microsoft_Windows_DxgKrnl::HwQueue_DCStart>                (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Microsoft_Windows_DxgKrnl_Performance; }
template<> void PatchKeyword<Microsoft_Windows_DxgKrnl::HwQueue_Start>                  (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Microsoft_Windows_DxgKrnl_Performance; }
template<> void PatchKeyword<Microsoft_Windows_DxgKrnl::DmaPacket_Info>                 (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Microsoft_Windows_DxgKrnl_Performance; }
template<> void PatchKeyword<Microsoft_Windows_DxgKrnl::DmaPacket_Start>                (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Microsoft_Windows_DxgKrnl_Performance; }
template<> void PatchKeyword<Microsoft_Windows_DxgKrnl::NodeMetadata_Info>              (uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Microsoft_Windows_DxgKrnl_Performance; }
template<> void PatchKeyword<Microsoft_Windows_DxgKrnl::MMIOFlipMultiPlaneOverlay3_Info>(uint64_t* k) { *k &= ~(uint64_t) Microsoft_Windows_DxgKrnl::Keyword::Microsoft_Windows_DxgKrnl_Performance; }


struct FilteredProvider {
    EVENT_FILTER_DESCRIPTOR filterDesc_;
    ENABLE_TRACE_PARAMETERS params_;
    uint64_t anyKeywordMask_;
    uint64_t allKeywordMask_;
    uint8_t maxLevel_;
    bool isWin11OrGreater_;
    bool isDryRun_;
    std::shared_ptr<IFilterBuildListener> pListener_;

    FilteredProvider(
        const GUID* pSessionGuid,
        bool filterEventIds,
        bool isWin11OrGreater,
        std::shared_ptr<IFilterBuildListener> pListener)
    {
        memset(&filterDesc_, 0, sizeof(filterDesc_));
        memset(&params_,     0, sizeof(params_));

        anyKeywordMask_ = 0;
        allKeywordMask_ = 0;
        maxLevel_ = 0;
        isWin11OrGreater_ = isWin11OrGreater;
        isDryRun_ = pSessionGuid == nullptr;
        pListener_ = std::move(pListener);

        if (filterEventIds && !isDryRun_) {
            static_assert(MAX_EVENT_FILTER_EVENT_ID_COUNT >= ANYSIZE_ARRAY, "Unexpected MAX_EVENT_FILTER_EVENT_ID_COUNT");
            auto memorySize = sizeof(EVENT_FILTER_EVENT_ID) + sizeof(USHORT) * (MAX_EVENT_FILTER_EVENT_ID_COUNT - ANYSIZE_ARRAY);
            void* memory = _aligned_malloc(memorySize, alignof(USHORT));
            if (memory != nullptr) {
                auto filteredEventIds = (EVENT_FILTER_EVENT_ID*) memory;
                filteredEventIds->FilterIn = TRUE;
                filteredEventIds->Reserved = 0;
                filteredEventIds->Count = 0;

                filterDesc_.Ptr = (ULONGLONG) filteredEventIds;
                filterDesc_.Size = (ULONG) memorySize;
                filterDesc_.Type = EVENT_FILTER_TYPE_EVENT_ID;

                params_.Version = ENABLE_TRACE_PARAMETERS_VERSION_2;
                params_.EnableProperty = EVENT_ENABLE_PROPERTY_IGNORE_KEYWORD_0;
                params_.SourceId = *pSessionGuid;
                params_.EnableFilterDesc = &filterDesc_;
                params_.FilterDescCount = 1;
            }
        }
    }

    ~FilteredProvider()
    {
        if (filterDesc_.Ptr != 0) {
            auto memory = (void*) filterDesc_.Ptr;
            _aligned_free(memory);
        }
    }

    FilteredProvider(const FilteredProvider&) = delete;
    FilteredProvider & operator=(const FilteredProvider&) = delete;
    FilteredProvider(FilteredProvider&&) = delete;
    FilteredProvider & operator=(FilteredProvider&&) = delete;

    void ClearFilter()
    {
        if (filterDesc_.Ptr != 0) {
            auto filteredEventIds = (EVENT_FILTER_EVENT_ID*) filterDesc_.Ptr;
            filteredEventIds->Count = 0;
        }

        anyKeywordMask_ = 0;
        allKeywordMask_ = 0;
        maxLevel_ = 0;
    }

    void AddKeyword(uint64_t keyword)
    {
        if (anyKeywordMask_ == 0) {
            anyKeywordMask_ = keyword;
            allKeywordMask_ = keyword;
        } else {
            anyKeywordMask_ |= keyword;
            allKeywordMask_ &= keyword;
        }
    }

    template<typename T>
    void AddEvent()
    {
        if (pListener_) {
            pListener_->EventAdded(T::Id);
        }

        uint64_t keyword = (uint64_t)T::Keyword;
        PatchKeyword<T>(&keyword);
        if (!isWin11OrGreater_) {
            PatchPreWin11Keyword<T>(&keyword);
        }
        AddKeyword(keyword);

        maxLevel_ = std::max(maxLevel_, T::Level);

        if (filterDesc_.Ptr != 0 && !isDryRun_) {
            auto filteredEventIds = (EVENT_FILTER_EVENT_ID*) filterDesc_.Ptr;
            assert(filteredEventIds->Count < MAX_EVENT_FILTER_EVENT_ID_COUNT);
            filteredEventIds->Events[filteredEventIds->Count++] = T::Id;
        }
    }

    ULONG Enable(
        TRACEHANDLE sessionHandle,
        GUID const& providerGuid,
        ULONG controlCode = EVENT_CONTROL_CODE_ENABLE_PROVIDER)
    {
        if (pListener_) {
            pListener_->ProviderEnabled(providerGuid, anyKeywordMask_, allKeywordMask_, maxLevel_, controlCode);
        }

        if (!isDryRun_) {
            ENABLE_TRACE_PARAMETERS* pparams = nullptr;
            if (filterDesc_.Ptr != 0) {
                pparams = &params_;

                // EnableTraceEx2() fails unless Size agrees with Count.
                auto filterEventIds = (EVENT_FILTER_EVENT_ID*)filterDesc_.Ptr;
                filterDesc_.Size = sizeof(EVENT_FILTER_EVENT_ID) + sizeof(USHORT) * (filterEventIds->Count - ANYSIZE_ARRAY);
            }

            return EnableTraceEx2(sessionHandle, &providerGuid, controlCode,
                maxLevel_, anyKeywordMask_, allKeywordMask_, 0, pparams);
        }
        return ERROR_SUCCESS;
    }

    ULONG EnableWithoutFiltering(
        TRACEHANDLE sessionHandle,
        GUID const& providerGuid,
        UCHAR maxLevel)
    {
        if (pListener_) {
            pListener_->ProviderEnabled(providerGuid, 0, 0, maxLevel, EVENT_CONTROL_CODE_ENABLE_PROVIDER);
        }

        if (!isDryRun_) {
            return EnableTraceEx2(sessionHandle, &providerGuid, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                maxLevel, 0, 0, 0, nullptr);
        }
        return ERROR_SUCCESS;
    }
};

} // namespace

ULONG EnableProvidersListing(
    TRACEHANDLE sessionHandle,
    const GUID* pSessionGuid,
    const PMTraceConsumer* pmConsumer,
    bool filterEventIds,
    bool isWin11OrGreater,
    std::shared_ptr<IFilterBuildListener> pListener)
{
    // Start backend providers first to reduce Presents being queued up before
    // we can track them.
    FilteredProvider provider(pSessionGuid, filterEventIds, isWin11OrGreater, std::move(pListener));

    // Microsoft_Windows_Kernel_Process
    //
    provider.ClearFilter();
    provider.AddEvent<Microsoft_Windows_Kernel_Process::ProcessStart_Start>();
    provider.AddEvent<Microsoft_Windows_Kernel_Process::ProcessStop_Stop>();
    provider.AddEvent<Microsoft_Windows_Kernel_Process::ProcessRundown_Info>();
    auto status = provider.Enable(sessionHandle, Microsoft_Windows_Kernel_Process::GUID);
    if (status != ERROR_SUCCESS && status != ERROR_ACCESS_DENIED) return status;
    // additionally, request a rundown if we have kproc access and the pertinent consumer flag is set
    if (pmConsumer->mTrackProcessState && status == ERROR_SUCCESS) {
        status = provider.Enable(sessionHandle, Microsoft_Windows_Kernel_Process::GUID,
            EVENT_CONTROL_CODE_CAPTURE_STATE);
        if (status != ERROR_SUCCESS && status != ERROR_ACCESS_DENIED) return status;
    }

    // Microsoft_Windows_DxgKrnl
    //
    // WARNING: When adding a DxgKrnl event, make sure to patch it's Performance keyword (see
    // above).
    provider.ClearFilter();
    provider.AddEvent<Microsoft_Windows_DxgKrnl::PresentHistory_Start>();
    if (pmConsumer->mTrackDisplay) {
        provider.AddEvent<Microsoft_Windows_DxgKrnl::Blit_Info>();
        provider.AddEvent<Microsoft_Windows_DxgKrnl::BlitCancel_Info>();
        provider.AddEvent<Microsoft_Windows_DxgKrnl::Flip_Info>();
        provider.AddEvent<Microsoft_Windows_DxgKrnl::IndependentFlip_Info>();
        provider.AddEvent<Microsoft_Windows_DxgKrnl::FlipMultiPlaneOverlay_Info>();
        provider.AddEvent<Microsoft_Windows_DxgKrnl::HSyncDPCMultiPlane_Info>();
        provider.AddEvent<Microsoft_Windows_DxgKrnl::VSyncDPCMultiPlane_Info>();
        provider.AddEvent<Microsoft_Windows_DxgKrnl::MMIOFlip_Info>();
        provider.AddEvent<Microsoft_Windows_DxgKrnl::MMIOFlipMultiPlaneOverlay_Info>();
        provider.AddEvent<Microsoft_Windows_DxgKrnl::Present_Info>();
        provider.AddEvent<Microsoft_Windows_DxgKrnl::PresentHistory_Info>();
        provider.AddEvent<Microsoft_Windows_DxgKrnl::PresentHistoryDetailed_Start>();
        provider.AddEvent<Microsoft_Windows_DxgKrnl::QueuePacket_Start>();
        provider.AddEvent<Microsoft_Windows_DxgKrnl::QueuePacket_Start_2>();
        provider.AddEvent<Microsoft_Windows_DxgKrnl::QueuePacket_Stop>();
        provider.AddEvent<Microsoft_Windows_DxgKrnl::VSyncDPC_Info>();
    }
    if (pmConsumer->mTrackGPU) {
        provider.AddEvent<Microsoft_Windows_DxgKrnl::Context_DCStart>();
        provider.AddEvent<Microsoft_Windows_DxgKrnl::Context_Start>();
        provider.AddEvent<Microsoft_Windows_DxgKrnl::Context_Stop>();
        provider.AddEvent<Microsoft_Windows_DxgKrnl::Device_DCStart>();
        provider.AddEvent<Microsoft_Windows_DxgKrnl::Device_Start>();
        provider.AddEvent<Microsoft_Windows_DxgKrnl::Device_Stop>();
        provider.AddEvent<Microsoft_Windows_DxgKrnl::HwQueue_DCStart>();
        provider.AddEvent<Microsoft_Windows_DxgKrnl::HwQueue_Start>();
        provider.AddEvent<Microsoft_Windows_DxgKrnl::DmaPacket_Info>();
        provider.AddEvent<Microsoft_Windows_DxgKrnl::DmaPacket_Start>();
    }
    if (pmConsumer->mTrackGPUVideo) {
        provider.AddEvent<Microsoft_Windows_DxgKrnl::NodeMetadata_Info>();
    }
    if (pmConsumer->mTrackFrameType) {
        provider.AddEvent<Microsoft_Windows_DxgKrnl::MMIOFlipMultiPlaneOverlay3_Info>();
    }
    status = provider.Enable(sessionHandle, Microsoft_Windows_DxgKrnl::GUID);
    if (status != ERROR_SUCCESS) return status;

    // we call Enable here once more to capture initial state of the device contexts
    if (pmConsumer->mTrackGPU) {
        provider.ClearFilter();
        provider.AddEvent<Microsoft_Windows_DxgKrnl::Context_DCStart>();
        provider.AddEvent<Microsoft_Windows_DxgKrnl::Device_DCStart>();
        status = provider.Enable(sessionHandle, Microsoft_Windows_DxgKrnl::GUID, EVENT_CONTROL_CODE_CAPTURE_STATE);
        if (status != ERROR_SUCCESS) return status;
    }

    // enable dxgkrnl win7 provider directly (no filtering because win7 doesn't support it anyways)
    status = provider.EnableWithoutFiltering(sessionHandle, Microsoft_Windows_DxgKrnl::Win7::GUID, TRACE_LEVEL_INFORMATION);
    if (status != ERROR_SUCCESS) return status;


    // Microsoft_Windows_Win32k
    //
    if (pmConsumer->mTrackDisplay || pmConsumer->mTrackInput) {
        provider.ClearFilter();
        if (pmConsumer->mTrackDisplay) {
            provider.AddEvent<Microsoft_Windows_Win32k::TokenCompositionSurfaceObject_Info>();
            provider.AddEvent<Microsoft_Windows_Win32k::TokenStateChanged_Info>();
        }
        if (pmConsumer->mTrackInput) {
            provider.AddEvent<Microsoft_Windows_Win32k::InputDeviceRead_Stop>();
            provider.AddEvent<Microsoft_Windows_Win32k::RetrieveInputMessage_Info>();
            provider.AddEvent<Microsoft_Windows_Win32k::OnInputXformUpdate_Info>();
        }
        status = provider.Enable(sessionHandle, Microsoft_Windows_Win32k::GUID);
        if (status != ERROR_SUCCESS) return status;
    }


    // Microsoft_Windows_Dwm_Core
    //
    if (pmConsumer->mTrackDisplay) {
        provider.ClearFilter();
        provider.AddEvent<Microsoft_Windows_Dwm_Core::MILEVENT_MEDIA_UCE_PROCESSPRESENTHISTORY_GetPresentHistory_Info>();
        provider.AddEvent<Microsoft_Windows_Dwm_Core::SCHEDULE_PRESENT_Start>();
        provider.AddEvent<Microsoft_Windows_Dwm_Core::SCHEDULE_SURFACEUPDATE_Info>();
        provider.AddEvent<Microsoft_Windows_Dwm_Core::FlipChain_Pending>();
        provider.AddEvent<Microsoft_Windows_Dwm_Core::FlipChain_Complete>();
        provider.AddEvent<Microsoft_Windows_Dwm_Core::FlipChain_Dirty>();
        status = provider.Enable(sessionHandle, Microsoft_Windows_Dwm_Core::GUID);
        if (status != ERROR_SUCCESS) return status;

        // enable dwm_core win7 provider directly (no filtering because win7 doesn't support it anyways)
        status = provider.EnableWithoutFiltering(sessionHandle, Microsoft_Windows_Dwm_Core::Win7::GUID, TRACE_LEVEL_VERBOSE);
        if (status != ERROR_SUCCESS) return status;
    }


    // Microsoft_Windows_DXGI
    //
    provider.ClearFilter();
    provider.AddEvent<Microsoft_Windows_DXGI::Present_Start>();
    provider.AddEvent<Microsoft_Windows_DXGI::Present_Stop>();
    provider.AddEvent<Microsoft_Windows_DXGI::PresentMultiplaneOverlay_Start>();
    provider.AddEvent<Microsoft_Windows_DXGI::PresentMultiplaneOverlay_Stop>();
    if (pmConsumer->mTrackHybridPresent) {
        provider.AddEvent<Microsoft_Windows_DXGI::SwapChain_Start>();
        provider.AddEvent<Microsoft_Windows_DXGI::ResizeBuffers_Start>();
    }
    status = provider.Enable(sessionHandle, Microsoft_Windows_DXGI::GUID);
    if (status != ERROR_SUCCESS) return status;


    // Microsoft_Windows_D3D9
    //
    provider.ClearFilter();
    provider.AddEvent<Microsoft_Windows_D3D9::Present_Start>();
    provider.AddEvent<Microsoft_Windows_D3D9::Present_Stop>();
    status = provider.Enable(sessionHandle, Microsoft_Windows_D3D9::GUID);
    if (status != ERROR_SUCCESS) return status;

    if (pmConsumer->mTrackD3D12ShaderCompilation) {
        provider.ClearFilter();
        provider.AddEvent<Microsoft_Windows_Direct3D12::CreatePipelineStateObject_Start>();
        provider.AddEvent<Microsoft_Windows_Direct3D12::CreatePipelineStateObject_Stop>();
        status = provider.Enable(sessionHandle, Microsoft_Windows_Direct3D12::GUID);
        if (status != ERROR_SUCCESS) return status;
    }


    // Intel_PresentMon
    //
    if (pmConsumer->mTrackFrameType || pmConsumer->mTrackPMMeasurements || pmConsumer->mTrackAppTiming) {
        provider.ClearFilter();
        if (pmConsumer->mTrackFrameType) {
            provider.AddEvent<Intel_PresentMon::PresentFrameType_Info>();
            provider.AddEvent<Intel_PresentMon::PresentFrameType_Info_2>();
            provider.AddEvent<Intel_PresentMon::FlipFrameType_Info>();
            provider.AddEvent<Intel_PresentMon::FlipFrameType_Info_2>();
        }
        if (pmConsumer->mTrackPMMeasurements) {
            provider.AddEvent<Intel_PresentMon::MeasuredInput_Info>();
            provider.AddEvent<Intel_PresentMon::MeasuredScreenChange_Info>();
        }
        if (pmConsumer->mTrackAppTiming) {
            provider.AddEvent<Intel_PresentMon::AppInputSample_Info>();
            provider.AddEvent<Intel_PresentMon::AppPresentStart_Info>();
            provider.AddEvent<Intel_PresentMon::AppPresentEnd_Info>();
            provider.AddEvent<Intel_PresentMon::AppSimulationStart_Info>();
            provider.AddEvent<Intel_PresentMon::AppSimulationEnd_Info>();
            provider.AddEvent<Intel_PresentMon::AppRenderSubmitStart_Info>();
            provider.AddEvent<Intel_PresentMon::AppRenderSubmitEnd_Info>();
            provider.AddEvent<Intel_PresentMon::AppSleepStart_Info>();
            provider.AddEvent<Intel_PresentMon::AppSleepEnd_Info>();
        }
        status = provider.Enable(sessionHandle, Intel_PresentMon::GUID);
        if (status != ERROR_SUCCESS) return status;
    }

    // Nvidia_DisplayDriver
    //
    provider.ClearFilter();
    provider.AddEvent<NvidiaDisplayDriver_Events::FlipRequest>();
    status = provider.Enable(sessionHandle, NvidiaDisplayDriver_Events::GUID);
    if (status != ERROR_SUCCESS) return status;

    if (pmConsumer->mTrackPcLatency) {
        provider.ClearFilter();
        status = provider.EnableWithoutFiltering(sessionHandle, Nvidia_PCL::GUID, TRACE_LEVEL_VERBOSE);
        if (status != ERROR_SUCCESS) return status;
    }

    return ERROR_SUCCESS;
}
