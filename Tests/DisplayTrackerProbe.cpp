#include "DisplayTracker.h"
#include "GamepadInput.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
int wmain(int count,wchar_t** arguments)
{
    try
    {
        Require(count==3,"Expected output and bundled-runtime directories.");
        const auto directory=std::filesystem::path(arguments[1])/std::to_string(GetCurrentProcessId()); std::filesystem::create_directories(directory);
        GamepadInput runtime; Require(runtime.Initialize(arguments[2]),"Bundled GameInput clock initialization failed.");
        DisplayTracker tracker(directory);
        Require(!tracker.Active() && !tracker.Snapshot().finished && !std::filesystem::exists(directory/L"DisplayReadings.csv"),"The tracker must remain inert until explicitly started.");
        const auto clock=[](void* context) { return static_cast<GamepadInput*>(context)->Now(); };
        for(unsigned seconds:{0u,61u}) { bool rejected=false; try { tracker.Start(seconds,clock,&runtime); } catch(const std::invalid_argument&) { rejected=true; } Require(rejected,"Out-of-budget captures must be rejected before creating a session."); }
        const auto start=std::chrono::steady_clock::now(); tracker.Start(1,clock,&runtime);
        while(!tracker.Snapshot().finished && std::chrono::steady_clock::now()-start<std::chrono::seconds(8)) std::this_thread::sleep_for(std::chrono::milliseconds(25));
        Require(tracker.Snapshot().finished,"A one-second capture did not finalize within its lifecycle budget."); tracker.Stop();
        const auto result=tracker.Snapshot(); Require(result.sessionStopped && !tracker.Active() && !result.loggingFailed,"Owned ETW session cleanup/output failed.");
        Require(result.validReadings==0,"A non-rendering probe must never invent reading-to-display latency.");
        std::ifstream readings(directory/L"DisplayReadings.csv"); std::string line; Require(bool(std::getline(readings,line)) && !std::getline(readings,line),"No measurement rows may be emitted without a displayed frame.");
        std::ifstream diagnostics(directory/L"DisplayDiagnostics.txt"); std::string details((std::istreambuf_iterator<char>(diagnostics)),{});
        Require(details.starts_with("FinalizedOutput=1\n") && details.find("ReadingLatencyStatistics=Unavailable")!=std::string::npos,"Completed outputs must explicitly mark finalization and withhold unavailable latency statistics.");
        Require(!std::filesystem::exists(directory/L"DisplayFrames.csv.partial") && !std::filesystem::exists(directory/L"DisplayReadings.csv.partial") && !std::filesystem::exists(directory/L"DisplayDiagnostics.txt.partial"),"Successful output finalization must leave no partial files.");
        std::cout<<result.status<<"\nDiagnostics: "<<directory.string()<<'\n';
        if(result.error==ERROR_ACCESS_DENIED || result.error==ERROR_PRIVILEGE_NOT_HELD || result.error==ERROR_NOT_SUPPORTED) { std::cout<<"BLOCKED: existing ETW permissions/provider support; no elevation or security changes attempted.\n"; return 77; }
        Require(result.error==0,"Unexpected ETW provider/controller error requires investigation.");
        tracker.Stop(); tracker.Start(1,clock,&runtime);
        Require(tracker.Snapshot().finished && !tracker.Active() && tracker.Snapshot().submitted==result.submitted,"Repeated stop must be safe and a completed once-per-launch tracker must not restart.");
        const auto earlyDirectory=directory/L"EarlyStop"; std::filesystem::create_directories(earlyDirectory);
        DisplayTracker early(earlyDirectory); const auto earlyStart=std::chrono::steady_clock::now(); early.Start(60,clock,&runtime); early.RequestStop(); early.Stop();
        const auto earlyResult=early.Snapshot();
        Require(std::chrono::steady_clock::now()-earlyStart<std::chrono::seconds(8) && earlyResult.finished && earlyResult.sessionStopped && !earlyResult.error && !earlyResult.loggingFailed && !early.Active() && !earlyResult.validReadings,"An early stop must finalize its owned session promptly, independent of the sixty-second duration budget.");
        for(bool finalReader:{false,true})
        {
            const auto lossDirectory=directory/(finalReader ? L"FinalReaderLoss" : L"ImmediateExitLoss"); std::filesystem::create_directories(lossDirectory);
            DisplayTracker loss(lossDirectory); loss.Start(60,clock,&runtime,finalReader ? +[](void*) { return true; } : nullptr);
            if(!finalReader) loss.ReportInputLoss();
            loss.RequestStop(); loss.Stop(); const auto lossResult=loss.Snapshot();
            Require(lossResult.finished && lossResult.sessionStopped && !lossResult.error && !lossResult.loggingFailed && !lossResult.reliable && lossResult.submitted==0 && lossResult.validReadings==0,"A capture-wide loss signal/final reader must survive immediate exit without a submitted frame.");
            std::ifstream lossDiagnostics(lossDirectory/L"DisplayDiagnostics.txt"); const std::string lossDetails((std::istreambuf_iterator<char>(lossDiagnostics)),{});
            Require(lossDetails.find("InputLoss=1\n")!=std::string::npos && lossDetails.find("CaptureReliable=0\n")!=std::string::npos,"Final real ETW diagnostics must persist input loss independently of frame delivery.");
        }
        const auto missingClockDirectory=directory/L"MissingClock"; std::filesystem::create_directories(missingClockDirectory);
        const auto unavailableClock=[](void*)->std::uint64_t { return 0; };
        DisplayTracker missingClock(missingClockDirectory); missingClock.Start(1,unavailableClock,nullptr); missingClock.Stop(); const auto missingResult=missingClock.Snapshot();
        Require(missingResult.finished && missingResult.sessionStopped && missingResult.error==ERROR_NOT_READY && !missingResult.reliable && !missingResult.validReadings && !missingResult.loggingFailed,"A missing portable runtime clock must report its blocker and finalize without creating measurements.");
        std::ifstream missingReadings(missingClockDirectory/L"DisplayReadings.csv"); Require(bool(std::getline(missingReadings,line)) && !std::getline(missingReadings,line),"A missing runtime clock must leave measurement CSV header-only.");
        const auto failedDirectory=directory/L"AbsentDirectory"/L"NotCreated";
        DisplayTracker failedOutput(failedDirectory); failedOutput.Start(1,unavailableClock,nullptr); failedOutput.Stop(); const auto failedResult=failedOutput.Snapshot();
        Require(failedResult.finished && failedResult.sessionStopped && failedResult.loggingFailed && !failedResult.reliable && !failedResult.validReadings && !std::filesystem::exists(failedDirectory/L"DisplayDiagnostics.txt"),"Output failure must not publish a final completion marker or claim a valid result.");
        std::cout<<"PASS: bounded opt-in ETW start/finalization/stop, immediate cancellation, capture-wide/final-reader input loss without submitted frames, no restart, missing clock, output failure and empty-measurement integrity. Display and physical input correlation require a rendering check.\n"; return 0;
    }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
