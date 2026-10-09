#include "DisplayTracker.h"
#include "Measurement.h"
#include "PresentMonTraceConsumer.hpp"
#include "ProviderConfiguration.h"
#include "PresentMonWarnings.h"
#include "ETW/Microsoft_Windows_DXGI.h"
#include "ETW/Microsoft_Windows_DxgKrnl.h"
#include "ETW/Microsoft_Windows_DxgKrnl_Win7.h"
#include "ETW/Microsoft_Windows_Dwm_Core.h"
#include "ETW/Microsoft_Windows_Dwm_Core_Win7.h"
#include "ETW/Microsoft_Windows_Win32k.h"
#include "ETW/Microsoft_Windows_D3D9.h"
#include "ETW/Microsoft_Windows_EventMetadata.h"
#include "ETW/Microsoft_Windows_Kernel_Process.h"
#include "ETW/NV_DD.h"
#include <chrono>
#include <condition_variable>
#include <format>
#include <fstream>
#include <objbase.h>
#include <thread>

namespace
{
constexpr std::size_t FrameCapacity=8192,CompletionCapacity=32768;
struct SessionProperties { EVENT_TRACE_PROPERTIES properties{}; wchar_t name[128]{}; };
std::uint64_t Counter() noexcept { LARGE_INTEGER value{}; return QueryPerformanceCounter(&value) ? static_cast<std::uint64_t>(value.QuadPart) : 0; }
}
struct DisplayTracker::Implementation
{
    explicit Implementation(std::filesystem::path directory) : logDirectory(std::move(directory)),consumer(8192) {}
    std::filesystem::path logDirectory;
    PMTraceConsumer consumer;
    SessionProperties properties;
    TRACEHANDLE session{},trace{INVALID_PROCESSTRACE_HANDLE};
    std::wstring name;
    std::uint64_t frequency{},warningBaseline{};
    std::uint32_t process{GetCurrentProcessId()};
    std::uint64_t (*clock)(void*){};
    bool (*inputLossReader)(void*){};
    void* clockContext{};
    DisplayInputLossLatch inputLoss;
    std::atomic<bool> active{},stopping{},started{};
    std::atomic<std::uint32_t> error{};
    std::atomic<std::uint64_t> submissionsLost{},completionsLost{};
    SingleProducerQueue<DisplayFrameSubmission,256> submissions;
    SingleProducerQueue<DisplayCompletion,8192> completions;
    std::thread controller,decoder;
    std::vector<std::shared_ptr<PresentEvent>> decoded;
    std::vector<DisplayFrameSubmission> frames;
    std::vector<DisplayCompletion> events;
    std::vector<DisplayClockAnchor> anchors;
    DisplayCaptureHealth health;
    mutable std::mutex snapshotMutex;
    DisplayTrackingSnapshot snapshot;
    std::mutex waitMutex;
    std::condition_variable wake;
    void Error(std::uint32_t code) noexcept { std::uint32_t expected=0; error.compare_exchange_strong(expected,code); }
    void ObserveClock() { const auto first=Counter(); const auto value=clock(clockContext); const auto last=Counter(); anchors.push_back({first,last,value}); }
    static void CALLBACK Record(EVENT_RECORD* event) noexcept
    {
        auto& self=*static_cast<Implementation*>(event->UserContext);
        try
        {
            PMTraceConsumer::EventProcessingScope scope(self.consumer); if(!scope) return;
            const auto& provider=event->EventHeader.ProviderId;
            if(provider==Microsoft_Windows_DxgKrnl::GUID) self.consumer.HandleDXGKEvent(event);
            else if(provider==Microsoft_Windows_DXGI::GUID) self.consumer.HandleDXGIEvent(event);
            else if(provider==Microsoft_Windows_Win32k::GUID) self.consumer.HandleWin32kEvent(event);
            else if(provider==Microsoft_Windows_Dwm_Core::GUID || provider==Microsoft_Windows_Dwm_Core::Win7::GUID) self.consumer.HandleDWMEvent(event);
            else if(provider==Microsoft_Windows_D3D9::GUID) self.consumer.HandleD3D9Event(event);
            else if(provider==Microsoft_Windows_EventMetadata::GUID) self.consumer.HandleMetadataEvent(event);
            else if(provider==Microsoft_Windows_Kernel_Process::GUID) self.consumer.HandleProcessEvent(event);
            else if(provider==NvidiaDisplayDriver_Events::GUID) self.consumer.HandleNvidiaDisplayDriverEvent(event);
            else if(provider==Microsoft_Windows_DxgKrnl::Win7::PRESENTHISTORY_GUID) self.consumer.HandleWin7DxgkPresentHistory(event);
            else if(provider==Microsoft_Windows_DxgKrnl::Win7::BLT_GUID) self.consumer.HandleWin7DxgkBlt(event);
            else if(provider==Microsoft_Windows_DxgKrnl::Win7::FLIP_GUID) self.consumer.HandleWin7DxgkFlip(event);
            else if(provider==Microsoft_Windows_DxgKrnl::Win7::QUEUEPACKET_GUID) self.consumer.HandleWin7DxgkQueuePacket(event);
            else if(provider==Microsoft_Windows_DxgKrnl::Win7::VSYNCDPC_GUID) self.consumer.HandleWin7DxgkVSyncDPC(event);
            else if(provider==Microsoft_Windows_DxgKrnl::Win7::MMIOFLIP_GUID) self.consumer.HandleWin7DxgkMMIOFlip(event);
            self.consumer.DequeuePresentEvents(self.decoded);
            for(const auto& present:self.decoded)
            {
                if(present->ProcessId!=self.process || present->Runtime!=Runtime::DXGI) continue;
                DisplayCompletion item{present->PresentStartTime,0,present->SwapChainAddress,present->ThreadId,static_cast<std::uint32_t>(present->PresentMode),present->FinalState==PresentResult::Presented,present->IsLost};
                for(const auto& display:present->Displayed) if(display.second && (!item.displayQpc || display.second<item.displayQpc)) item.displayQpc=display.second;
                if(!self.completions.Push(item)) ++self.completionsLost;
            }
            self.decoded.clear();
        }
        catch(...) { ++PresentMonWarningCount; }
    }
    bool StartSession()
    {
        LARGE_INTEGER value{};
        if(!QueryPerformanceFrequency(&value) || value.QuadPart<=0) { Error(ERROR_INVALID_DATA); return false; }
        frequency=static_cast<std::uint64_t>(value.QuadPart); name=std::format(L"Veehiicuul2.DisplayLatency.{}",process);
        auto& configuration=properties.properties;
        configuration.Wnode.BufferSize=sizeof(properties); configuration.Wnode.Flags=WNODE_FLAG_TRACED_GUID; configuration.Wnode.ClientContext=1;
        if(FAILED(CoCreateGuid(&configuration.Wnode.Guid))) { Error(ERROR_INVALID_DATA); return false; }
        configuration.BufferSize=64; configuration.MinimumBuffers=8; configuration.MaximumBuffers=64;
        configuration.FlushTimer=1; configuration.LogFileMode=EVENT_TRACE_REAL_TIME_MODE; configuration.LoggerNameOffset=offsetof(SessionProperties,name);
        const auto status=StartTraceW(&session,name.c_str(),&configuration);
        if(status!=ERROR_SUCCESS) { session=0; Error(status); return false; }
        { std::lock_guard guard(snapshotMutex); snapshot.sessionStopped=false; }
        consumer.mFilteredEvents=true; consumer.mFilteredProcessIds=true; consumer.mDeferralTimeLimit=frequency*2; consumer.AddTrackedProcessForFiltering(process);
        const auto enabled=EnableProvidersListing(session,&configuration.Wnode.Guid,&consumer,true,true);
        if(enabled!=ERROR_SUCCESS) { Error(enabled); return false; }
        EVENT_TRACE_LOGFILEW logfile{}; logfile.LoggerName=name.data(); logfile.Context=this;
        logfile.ProcessTraceMode=PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD | PROCESS_TRACE_MODE_RAW_TIMESTAMP; logfile.EventRecordCallback=Record;
        trace=OpenTraceW(&logfile); if(trace==INVALID_PROCESSTRACE_HANDLE) { Error(GetLastError()); return false; }
        decoder=std::thread([this]
        {
            SetThreadDescription(GetCurrentThread(),L"Veehiicuul2 optional display decoder");
            const auto result=ProcessTrace(&trace,1,nullptr,nullptr);
            if(result!=ERROR_SUCCESS && result!=ERROR_CANCELLED) Error(result);
            active.store(false,std::memory_order_release); wake.notify_all();
        });
        return true;
    }
    void StopSession() noexcept
    {
        bool traceClosed=false;
        bool stopped=false; { std::lock_guard guard(snapshotMutex); stopped=snapshot.sessionStopped; }
        if(session)
        {
            auto finalProperties=properties;
            const auto status=ControlTraceW(session,name.c_str(),&finalProperties.properties,EVENT_TRACE_CONTROL_STOP);
            stopped=status==ERROR_SUCCESS || status==ERROR_WMI_INSTANCE_NOT_FOUND;
            if(status==ERROR_SUCCESS)
            {
                health.eventsLost=finalProperties.properties.EventsLost;
                health.buffersLost=std::uint64_t(finalProperties.properties.LogBuffersLost)+finalProperties.properties.RealTimeBuffersLost;
                health.lossStatisticsKnown=true;
            }
            else { Error(status); health.lossStatisticsKnown=false; }
            session=0;
            if(!stopped && trace!=INVALID_PROCESSTRACE_HANDLE) { CloseTrace(trace); traceClosed=true; }
        }
        if(decoder.joinable()) decoder.join();
        if(trace!=INVALID_PROCESSTRACE_HANDLE) { if(!traceClosed) CloseTrace(trace); trace=INVALID_PROCESSTRACE_HANDLE; }
        std::lock_guard guard(snapshotMutex); snapshot.sessionStopped=stopped;
    }
    void Drain()
    {
        DisplayFrameSubmission frame;
        while(submissions.Pop(frame))
        {
            if(frames.size()==FrameCapacity) { health.capacityExceeded=true; continue; }
            frames.push_back(frame); anchors.push_back(ClockAnchor(frame)); health.inputLoss|=frame.inputOverflow;
        }
        DisplayCompletion event;
        while(completions.Pop(event)) { if(events.size()==CompletionCapacity) { health.capacityExceeded=true; continue; } events.push_back(event); }
    }
    void WriteFinal()
    {
        std::sort(anchors.begin(),anchors.end(),[](const auto& a,const auto& b) { return a.firstQpc<b.firstQpc; });
        health.error=error.load(); health.submissionsLost=submissionsLost.load(); health.completionsLost=completionsLost.load();
        health.decoderWarnings=PresentMonWarningCount.load()-warningBaseline; health.decoderOverflows=consumer.GetNumOverflowedPresents();
        inputLoss.Apply(health,inputLossReader && inputLossReader(clockContext));
        const auto result=FinalizeDisplayCapture(frames,events,anchors,frequency,health);
        DisplayTrackingSnapshot value;
        { std::lock_guard guard(snapshotMutex); value.sessionStopped=snapshot.sessionStopped; }
        value.finished=true; value.error=health.error; value.submitted=frames.size(); value.discarded=result.discarded;
        value.unmatched=result.unmatched; value.invalidClocks=result.invalidClocks; value.invalidInputs=result.invalidInputs; value.lostEvents=health.eventsLost; value.lostBuffers=health.buffersLost;
        value.droppedSubmissions=health.submissionsLost; value.droppedCompletions=health.completionsLost;
        value.decoderWarnings=health.decoderWarnings; value.decoderOverflows=health.decoderOverflows;
        const auto framePath=logDirectory/L"DisplayFrames.csv.partial",readingPath=logDirectory/L"DisplayReadings.csv.partial",diagnosticPath=logDirectory/L"DisplayDiagnostics.txt.partial";
        std::ofstream frameFile(framePath),readings(readingPath);
        frameFile.exceptions(std::ios::failbit | std::ios::badbit); readings.exceptions(std::ios::failbit | std::ios::badbit);
        frameFile<<"Frame,Thread,SwapChain,PresentFirstQpc,PresentEndQpc,EtwPresentQpc,DisplayQpc,DisplayGameInputUs,ClockUncertaintyUs,PresentMode,Status,CaptureReliable\n";
        readings<<"Device,Generation,Frame,ReadingSerial,ReadingTimestampUs,SampleTimestampUs,Sources,DisplayQpc,DisplayTimestampUs,ReadingToDisplayUs,ClockUncertaintyUs,FirstDisplayForReading\n";
        for(std::size_t i=0;i<frames.size();++i)
        {
            const auto& frame=frames[i]; const auto& item=result.frames[i];
            if(item.status=="Displayed") ++value.displayed; else if(item.status!="Discarded") ++value.unresolved;
            frameFile<<frame.frame<<','<<frame.thread<<','<<frame.swapChain<<','<<frame.presentFirstQpc<<','<<frame.presentEndQpc<<','<<item.completion.presentQpc<<','<<item.completion.displayQpc<<',';
            if(item.display) { frameFile<<item.display->microseconds<<','<<item.display->uncertaintyMicroseconds; value.maximumClockUncertainty=std::max(value.maximumClockUncertainty,item.display->uncertaintyMicroseconds); } else frameFile<<',';
            frameFile<<','<<item.completion.mode<<','<<item.status<<','<<result.reliable<<'\n';
        }
        Statistics statistics;
        for(const auto& item:result.readings)
        {
            const auto& frame=frames[item.frameIndex]; const auto& input=frame.inputs[item.inputIndex]; const auto& displayed=result.frames[item.frameIndex];
            readings<<input.device<<','<<input.generation<<','<<frame.frame<<','<<input.serial<<','<<input.reading<<','<<input.sampled<<','<<input.sources<<','<<displayed.completion.displayQpc<<','<<displayed.display->microseconds<<','<<item.duration<<','<<displayed.display->uncertaintyMicroseconds<<','<<item.firstDisplay<<'\n';
            if(item.firstDisplay) { statistics.Add(item.duration); ++value.validReadings; }
        }
        const auto stats=statistics.Snapshot(); value.reliable=result.reliable && value.displayed>0;
        if(value.error==ERROR_ACCESS_DENIED || value.error==ERROR_PRIVILEGE_NOT_HELD) value.status=std::format("Display capture unavailable: ETW permission error {}. No elevation or security changes were attempted.",value.error);
        else if(value.error) value.status=std::format("Display capture unavailable: Windows error {}. See DisplayDiagnostics.txt.",value.error);
        else if(!result.reliable) value.status="Display capture rejected: lost records, uncertain clocks or incomplete capture. No valid durations exported.";
        else if(!value.validReadings) value.status="Display capture finished: no matched GameInput reading durations. See DisplayDiagnostics.txt.";
        else value.status=std::format("Final reading-to-ETW-display: {} samples; mean {:.3f} ms; p95 {:.3f} ms. See session CSV files.",value.validReadings,stats.mean/1000,stats.percentile95/1000);
        std::ofstream diagnostics(diagnosticPath); diagnostics.exceptions(std::ios::failbit | std::ios::badbit);
        diagnostics<<"FinalizedOutput=1\nEndpoint=Exact GameInput reading to PresentMon Windows display event; no optical/pre-reading device delay\nSession="<<std::format("Veehiicuul2.DisplayLatency.{}",process)<<"\nTargetProcess="<<process<<"\nDurationLimitSeconds=1..60; one opt-in capture per launch\nQpcFrequency="<<frequency
            <<"\nStartOrProcessingError="<<value.error<<"\nSessionStopped="<<value.sessionStopped<<"\nFinalLossStatisticsKnown="<<health.lossStatisticsKnown<<"\nCaptureReliable="<<result.reliable<<"\nSubmittedFrames="<<value.submitted<<"\nDisplayedFrames="<<value.displayed<<"\nDiscardedFrames="<<value.discarded<<"\nUnresolvedFrames="<<value.unresolved<<"\nUnmatchedEvents="<<value.unmatched<<"\nAmbiguousEvents="<<result.ambiguous
            <<"\nEtwEventsLost="<<value.lostEvents<<"\nEtwBuffersLost="<<value.lostBuffers<<"\nDroppedSubmissions="<<value.droppedSubmissions<<"\nDroppedCompletions="<<value.droppedCompletions<<"\nCapacityExceeded="<<health.capacityExceeded<<"\nInputLoss="<<health.inputLoss<<"\nDecoderWarnings="<<value.decoderWarnings<<"\nDecoderOverflows="<<value.decoderOverflows<<"\nInvalidClocks="<<value.invalidClocks<<"\nInvalidInputs="<<value.invalidInputs<<"\nMaximumClockUncertaintyUs="<<value.maximumClockUncertainty<<"\nFirstDisplayedReadings="<<value.validReadings;
        if(value.validReadings) diagnostics<<"\nMeanUs="<<stats.mean<<"\nMinUs="<<stats.minimum<<"\nMaxUs="<<stats.maximum<<"\nMedianUs="<<stats.median<<"\nP95Us="<<stats.percentile95<<"\nP99Us="<<stats.percentile99<<"\nPercentileWindow="<<stats.percentileCount;
        else diagnostics<<"\nReadingLatencyStatistics=Unavailable";
        diagnostics<<"\nStatus="<<value.status<<'\n';
        frameFile.flush(); readings.flush(); diagnostics.flush(); frameFile.close(); readings.close(); diagnostics.close();
        std::filesystem::rename(framePath,logDirectory/L"DisplayFrames.csv");
        std::filesystem::rename(readingPath,logDirectory/L"DisplayReadings.csv");
        // The diagnostics rename is the completion marker for the entire output set.
        std::filesystem::rename(diagnosticPath,logDirectory/L"DisplayDiagnostics.txt");
        std::lock_guard guard(snapshotMutex); snapshot=std::move(value);
    }
    void Run(unsigned seconds) noexcept
    {
        SetThreadDescription(GetCurrentThread(),L"Veehiicuul2 optional display capture controller");
        try
        {
            frames.reserve(FrameCapacity); events.reserve(CompletionCapacity); anchors.reserve(FrameCapacity+2);
            warningBaseline=PresentMonWarningCount.load(); health.lossStatisticsKnown=false;
            if(!clock(clockContext)) Error(ERROR_NOT_READY);
            if(!error.load() && StartSession())
            {
                ObserveClock(); active.store(true,std::memory_order_release);
                const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);
                while(!stopping.load() && !error.load() && active.load() && std::chrono::steady_clock::now()<end)
                {
                    Drain();
                    { std::lock_guard guard(snapshotMutex); snapshot.active=true; snapshot.submitted=frames.size(); snapshot.status="Optional display capture running; durations withheld until final ETW loss checks."; }
                    std::unique_lock guard(waitMutex); wake.wait_for(guard,std::chrono::milliseconds(20),[this] { return stopping.load() || error.load() || !active.load(); });
                }
                active.store(false,std::memory_order_release);
                std::unique_lock guard(waitMutex); wake.wait_for(guard,std::chrono::milliseconds(200));
            }
            StopSession(); Drain(); if(clock(clockContext)) ObserveClock(); WriteFinal();
        }
        catch(...)
        {
            active.store(false,std::memory_order_release); StopSession();
            std::lock_guard guard(snapshotMutex); snapshot.active=false; snapshot.finished=true; snapshot.reliable=false; snapshot.loggingFailed=true; snapshot.error=error.load(); snapshot.status="Optional display capture failed; no valid measurement is claimed.";
        }
    }
};
DisplayTracker::DisplayTracker(const std::filesystem::path& directory) : implementation(std::make_unique<Implementation>(directory)) {}
DisplayTracker::~DisplayTracker() { Stop(); }
void DisplayTracker::Start(unsigned seconds,std::uint64_t (*clock)(void*),void* context,bool (*inputLoss)(void*))
{
    if(seconds<1 || seconds>60 || !clock) throw std::invalid_argument("Display capture duration must be 1..60 seconds with a GameInput clock.");
    bool expected=false; if(!implementation->started.compare_exchange_strong(expected,true)) return;
    implementation->clock=clock; implementation->clockContext=context; implementation->inputLossReader=inputLoss;
    try { implementation->controller=std::thread([self=implementation.get(),seconds] { self->Run(seconds); }); }
    catch(const std::exception& failure)
    {
        std::lock_guard guard(implementation->snapshotMutex); auto& value=implementation->snapshot;
        value.finished=true; value.loggingFailed=true; value.error=ERROR_NOT_ENOUGH_MEMORY;
        value.status=std::format("Optional display controller creation failed: {}. No valid measurement is claimed.",failure.what());
        std::ofstream(implementation->logDirectory/L"DisplayCaptureFailure.txt")<<value.status<<'\n';
    }
}
void DisplayTracker::RequestStop() noexcept { implementation->stopping.store(true); implementation->wake.notify_all(); }
void DisplayTracker::ReportInputLoss(bool loss) noexcept { implementation->inputLoss.Report(loss); }
void DisplayTracker::Stop() noexcept { RequestStop(); if(implementation->controller.joinable()) implementation->controller.join(); }
void DisplayTracker::Submit(const DisplayFrameSubmission& frame) noexcept { ReportInputLoss(frame.inputOverflow); if(Active() && !implementation->submissions.Push(frame)) ++implementation->submissionsLost; }
bool DisplayTracker::Active() const noexcept { return implementation->active.load(std::memory_order_acquire); }
DisplayTrackingSnapshot DisplayTracker::Snapshot() const { std::lock_guard guard(implementation->snapshotMutex); return implementation->snapshot; }
