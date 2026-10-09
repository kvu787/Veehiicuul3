#pragma once
#include "DisplayCorrelation.h"
#include <filesystem>
#include <memory>
#include <string>

struct DisplayTrackingSnapshot
{
    bool active{},finished{},reliable{},loggingFailed{},sessionStopped=true;
    std::uint32_t error{};
    std::uint64_t submitted{}, displayed{}, discarded{}, unresolved{}, unmatched{}, lostEvents{}, lostBuffers{},
        droppedSubmissions{}, droppedCompletions{}, invalidClocks{}, invalidInputs{}, decoderWarnings{}, decoderOverflows{},validReadings{};
    double maximumClockUncertainty{};
    std::string status;
};

class DisplayTracker final
{
public:
    explicit DisplayTracker(const std::filesystem::path& logDirectory);
    ~DisplayTracker();
    void Start(unsigned seconds,std::uint64_t (*clock)(void*),void* context,bool (*inputLoss)(void*)=nullptr);
    void ReportInputLoss(bool loss=true) noexcept;
    void RequestStop() noexcept;
    void Stop() noexcept;
    void Submit(const DisplayFrameSubmission& frame) noexcept;
    bool Active() const noexcept;
    DisplayTrackingSnapshot Snapshot() const;
private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation;
};
